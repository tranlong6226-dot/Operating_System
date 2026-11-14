#include "rtsfr_resource.h"

#include <string.h>

#include "rtsfr_detect.h"

#define RTSFR_MUTEX_RESOURCE_BASE 1U
#define RTSFR_BUFFER_RESOURCE_BASE 0x100U

typedef struct
{
    rtsfr_resource_state_t state;
    uint8_t owner_task_id;
    uint16_t owner_generation;
    uint8_t data[64];
} rtsfr_managed_buffer_t;

static const void *g_mutex_handles[configRTSFR_MAX_TRACKED_MUTEXES];
static uint8_t g_mutex_owner[configRTSFR_MAX_TRACKED_MUTEXES];
static uint16_t g_mutex_generation[configRTSFR_MAX_TRACKED_MUTEXES];
static uint16_t g_generation[configRTSFR_MAX_TRACKED_TASKS];
static uint8_t g_unknown_count[configRTSFR_MAX_TRACKED_TASKS];
static rtsfr_managed_buffer_t g_buffers[configRTSFR_MAX_MANAGED_BUFFERS];
static uint16_t g_anomaly_count;

static bool prv_valid_task(uint8_t task_id)
{
    return (task_id > 0U) && (task_id <= configRTSFR_MAX_TRACKED_TASKS);
}

static uint8_t prv_index(uint8_t task_id)
{
    return (uint8_t)(task_id - 1U);
}

void rtsfr_resource_init(void)
{
    taskENTER_CRITICAL();
    (void)memset(g_mutex_handles, 0, sizeof(g_mutex_handles));
    (void)memset(g_mutex_owner, 0, sizeof(g_mutex_owner));
    (void)memset(g_mutex_generation, 0, sizeof(g_mutex_generation));
    (void)memset(g_generation, 0, sizeof(g_generation));
    (void)memset(g_unknown_count, 0, sizeof(g_unknown_count));
    (void)memset(g_buffers, 0, sizeof(g_buffers));
    g_anomaly_count = 0U;
    taskEXIT_CRITICAL();
}

void rtsfr_resource_on_new_generation(uint8_t task_id, uint16_t generation)
{
    if (!prv_valid_task(task_id) || (generation == 0U))
    {
        return;
    }
    taskENTER_CRITICAL();
    g_generation[prv_index(task_id)] = generation;
    g_unknown_count[prv_index(task_id)] = 0U;
    taskEXIT_CRITICAL();
}

void rtsfr_resource_on_mutex_acquire(uint8_t task_id, uint16_t resource_id)
{
    if (!prv_valid_task(task_id) ||
        (resource_id >= configRTSFR_MAX_TRACKED_MUTEXES))
    {
        return;
    }
    taskENTER_CRITICAL();
    if (g_mutex_owner[resource_id] != 0U)
    {
        g_unknown_count[prv_index(task_id)]++;
        g_anomaly_count++;
    }
    g_mutex_owner[resource_id] = task_id;
    g_mutex_generation[resource_id] = g_generation[prv_index(task_id)];
    taskEXIT_CRITICAL();
}

void rtsfr_resource_on_mutex_release(uint8_t task_id, uint16_t resource_id)
{
    if (!prv_valid_task(task_id) ||
        (resource_id >= configRTSFR_MAX_TRACKED_MUTEXES))
    {
        return;
    }
    taskENTER_CRITICAL();
    if ((g_mutex_owner[resource_id] == task_id) &&
        (g_mutex_generation[resource_id] ==
         g_generation[prv_index(task_id)]))
    {
        g_mutex_owner[resource_id] = 0U;
        g_mutex_generation[resource_id] = 0U;
    }
    else
    {
        g_unknown_count[prv_index(task_id)]++;
        g_anomaly_count++;
    }
    taskEXIT_CRITICAL();
}

static void prv_compute_summary(uint8_t task_id,
                                rtsfr_resource_summary_t *out)
{
    uint8_t index;
    uint8_t task_index = prv_index(task_id);
    uint8_t mutex_count = 0U;
    uint8_t buffer_count = 0U;
    uint8_t stale_count = 0U;
    bool inflight = false;
    bool quarantined = false;

    for (index = 0U; index < configRTSFR_MAX_TRACKED_MUTEXES; index++)
    {
        if (g_mutex_owner[index] == task_id)
        {
            mutex_count++;
            if (g_mutex_generation[index] != g_generation[task_index])
            {
                stale_count++;
            }
        }
    }
    for (index = 0U; index < configRTSFR_MAX_MANAGED_BUFFERS; index++)
    {
        if ((g_buffers[index].state == RTSFR_RESOURCE_FREE) ||
            (g_buffers[index].owner_task_id != task_id))
        {
            continue;
        }
        buffer_count++;
        if (g_buffers[index].owner_generation != g_generation[task_index])
        {
            stale_count++;
        }
        if (g_buffers[index].state == RTSFR_RESOURCE_IN_FLIGHT)
        {
            inflight = true;
        }
        if (g_buffers[index].state == RTSFR_RESOURCE_QUARANTINED)
        {
            quarantined = true;
        }
    }

    out->task_id = task_id;
    out->generation = g_generation[task_index];
    out->mutex_held_count = mutex_count;
    out->managed_buffers_owned = buffer_count;
    out->unknown_resource_count =
        (uint8_t)(g_unknown_count[task_index] + stale_count);
    out->has_inflight_io = inflight;
    out->can_reclaim_all = (mutex_count == 0U) &&
                           (out->unknown_resource_count == 0U) &&
                           !inflight && !quarantined;
}

rtsfr_status_t rtsfr_resource_snapshot(uint8_t task_id,
                                       uint16_t generation,
                                       rtsfr_resource_summary_t *out)
{
    if (!prv_valid_task(task_id) || (out == NULL))
    {
        return RTSFR_ERR_INVALID;
    }
    taskENTER_CRITICAL();
    if (generation != g_generation[prv_index(task_id)])
    {
        taskEXIT_CRITICAL();
        return RTSFR_ERR_STALE;
    }
    prv_compute_summary(task_id, out);
    taskEXIT_CRITICAL();
    return RTSFR_OK;
}

rtsfr_status_t rtsfr_resource_cleanup_safe(uint8_t task_id,
                                           uint16_t generation)
{
    uint8_t index;
    rtsfr_resource_summary_t summary;

    if (!prv_valid_task(task_id))
    {
        return RTSFR_ERR_INVALID;
    }
    taskENTER_CRITICAL();
    if (generation != g_generation[prv_index(task_id)])
    {
        taskEXIT_CRITICAL();
        return RTSFR_ERR_STALE;
    }
    prv_compute_summary(task_id, &summary);
    if (!summary.can_reclaim_all)
    {
        taskEXIT_CRITICAL();
        return RTSFR_ERR_UNSAFE;
    }
    for (index = 0U; index < configRTSFR_MAX_MANAGED_BUFFERS; index++)
    {
        if ((g_buffers[index].state == RTSFR_RESOURCE_OWNED) &&
            (g_buffers[index].owner_task_id == task_id) &&
            (g_buffers[index].owner_generation == generation))
        {
            g_buffers[index].state = RTSFR_RESOURCE_FREE;
            g_buffers[index].owner_task_id = 0U;
            g_buffers[index].owner_generation = 0U;
        }
    }
    taskEXIT_CRITICAL();
    return RTSFR_OK;
}

rtsfr_status_t rtsfr_buffer_acquire(uint8_t task_id, uint8_t *out_index)
{
    uint8_t index;
    if (!prv_valid_task(task_id) || (out_index == NULL))
    {
        return RTSFR_ERR_INVALID;
    }
    taskENTER_CRITICAL();
    for (index = 0U; index < configRTSFR_MAX_MANAGED_BUFFERS; index++)
    {
        if (g_buffers[index].state == RTSFR_RESOURCE_FREE)
        {
            g_buffers[index].state = RTSFR_RESOURCE_OWNED;
            g_buffers[index].owner_task_id = task_id;
            g_buffers[index].owner_generation =
                g_generation[prv_index(task_id)];
            *out_index = index;
            taskEXIT_CRITICAL();
            return RTSFR_OK;
        }
    }
    taskEXIT_CRITICAL();
    return RTSFR_ERR_NO_RESOURCE;
}

rtsfr_status_t rtsfr_buffer_release(uint8_t task_id, uint8_t index)
{
    if (!prv_valid_task(task_id) ||
        (index >= configRTSFR_MAX_MANAGED_BUFFERS))
    {
        return RTSFR_ERR_INVALID;
    }
    taskENTER_CRITICAL();
    if ((g_buffers[index].state != RTSFR_RESOURCE_OWNED) ||
        (g_buffers[index].owner_task_id != task_id) ||
        (g_buffers[index].owner_generation !=
         g_generation[prv_index(task_id)]))
    {
        taskEXIT_CRITICAL();
        return RTSFR_ERR_INVALID;
    }
    g_buffers[index].state = RTSFR_RESOURCE_FREE;
    g_buffers[index].owner_task_id = 0U;
    g_buffers[index].owner_generation = 0U;
    taskEXIT_CRITICAL();
    return RTSFR_OK;
}

rtsfr_status_t rtsfr_resource_register_mutex(const void *mutex_handle,
                                              uint16_t *out_resource_id)
{
    uint8_t index;
    int16_t free_index = -1;
    if ((mutex_handle == NULL) || (out_resource_id == NULL))
    {
        return RTSFR_ERR_INVALID;
    }
    taskENTER_CRITICAL();
    for (index = 0U; index < configRTSFR_MAX_TRACKED_MUTEXES; index++)
    {
        if (g_mutex_handles[index] == mutex_handle)
        {
            *out_resource_id = index;
            taskEXIT_CRITICAL();
            return RTSFR_OK;
        }
        if ((g_mutex_handles[index] == NULL) && (free_index < 0))
        {
            free_index = (int16_t)index;
        }
    }
    if (free_index >= 0)
    {
        g_mutex_handles[free_index] = mutex_handle;
        g_mutex_owner[free_index] = 0U;
        g_mutex_generation[free_index] = 0U;
        *out_resource_id = (uint16_t)free_index;
        taskEXIT_CRITICAL();
        return RTSFR_OK;
    }
    taskEXIT_CRITICAL();
    return RTSFR_ERR_NO_RESOURCE;
}

rtsfr_status_t rtsfr_resource_unregister_mutex(const void *mutex_handle)
{
    uint8_t index;
    if (mutex_handle == NULL)
    {
        return RTSFR_ERR_INVALID;
    }
    taskENTER_CRITICAL();
    for (index = 0U; index < configRTSFR_MAX_TRACKED_MUTEXES; index++)
    {
        if (g_mutex_handles[index] == mutex_handle)
        {
            if (g_mutex_owner[index] != 0U)
            {
                taskEXIT_CRITICAL();
                return RTSFR_ERR_BUSY;
            }
            g_mutex_handles[index] = NULL;
            taskEXIT_CRITICAL();
            return RTSFR_OK;
        }
    }
    taskEXIT_CRITICAL();
    return RTSFR_ERR_NOT_FOUND;
}

static bool prv_find_mutex(const void *mutex_handle, uint16_t *resource_id)
{
    uint8_t index;
    for (index = 0U; index < configRTSFR_MAX_TRACKED_MUTEXES; index++)
    {
        if ((mutex_handle != NULL) &&
            (g_mutex_handles[index] == mutex_handle))
        {
            *resource_id = index;
            return true;
        }
    }
    return false;
}

void rtsfr_resource_hook_mutex_taken(const void *mutex_handle,
                                     const void *holder_task_handle)
{
    uint16_t resource_id;
    uint8_t task_id;
    if (!prv_find_mutex(mutex_handle, &resource_id))
    {
        return;
    }
    task_id = rtsfr_detect_task_id_from_handle(holder_task_handle);
    if (task_id != RTSFR_TASK_ID_NONE)
    {
        rtsfr_resource_on_mutex_acquire(task_id, resource_id);
    }
}

void rtsfr_resource_hook_mutex_given(const void *mutex_handle,
                                     const void *holder_task_handle)
{
    uint16_t resource_id;
    uint8_t task_id;
    if (!prv_find_mutex(mutex_handle, &resource_id))
    {
        return;
    }
    if (holder_task_handle == NULL)
    {
        g_anomaly_count++;
        return;
    }
    task_id = rtsfr_detect_task_id_from_handle(holder_task_handle);
    if (task_id != RTSFR_TASK_ID_NONE)
    {
        rtsfr_resource_on_mutex_release(task_id, resource_id);
    }
}

rtsfr_status_t rtsfr_resource_export(rtsfr_resource_record_t *out,
                                     uint8_t capacity,
                                     uint8_t *out_count)
{
    uint8_t count = 0U;
    uint8_t index;
    if ((out == NULL) || (out_count == NULL))
    {
        return RTSFR_ERR_INVALID;
    }
    taskENTER_CRITICAL();
    for (index = 0U; index < configRTSFR_MAX_TRACKED_MUTEXES; index++)
    {
        if (g_mutex_handles[index] == NULL)
        {
            continue;
        }
        if (count >= capacity)
        {
            taskEXIT_CRITICAL();
            return RTSFR_ERR_NO_RESOURCE;
        }
        out[count].resource_id = (uint16_t)(RTSFR_MUTEX_RESOURCE_BASE + index);
        out[count].owner_task_id = g_mutex_owner[index];
        out[count].owner_generation = g_mutex_generation[index];
        out[count].state = (g_mutex_owner[index] == 0U)
                               ? RTSFR_RESOURCE_FREE
                               : RTSFR_RESOURCE_OWNED;
        count++;
    }
    for (index = 0U; index < configRTSFR_MAX_MANAGED_BUFFERS; index++)
    {
        if (g_buffers[index].state == RTSFR_RESOURCE_FREE)
        {
            continue;
        }
        if (count >= capacity)
        {
            taskEXIT_CRITICAL();
            return RTSFR_ERR_NO_RESOURCE;
        }
        out[count].resource_id = (uint16_t)(RTSFR_BUFFER_RESOURCE_BASE + index);
        out[count].owner_task_id = g_buffers[index].owner_task_id;
        out[count].owner_generation = g_buffers[index].owner_generation;
        out[count].state = g_buffers[index].state;
        count++;
    }
    taskEXIT_CRITICAL();
    *out_count = count;
    return RTSFR_OK;
}

uint16_t rtsfr_resource_get_anomaly_count(void)
{
    uint16_t result;
    taskENTER_CRITICAL();
    result = g_anomaly_count;
    taskEXIT_CRITICAL();
    return result;
}
