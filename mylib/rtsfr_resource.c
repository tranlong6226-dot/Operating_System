#include "rtsfr_resource.h"
#include "rtsfr_types.h"
#define MAXT           configRTSFR_MAX_TRACKED_TASKS
#define VALID_TASK(id) ((id) < MAXT)
#define TASK_IDX(id)   (id)
#define OWN(id)        ((uint8_t)((id) + 1u))
#define RTSFR_NO_OWNER 0u

static uint8_t  mutex_owner[configRTSFR_MAX_TRACKED_MUTEXES];
static uint16_t cur_generation[MAXT];
static uint8_t  unknown_cnt[MAXT];
typedef struct { rtsfr_buffer_state_t state; uint8_t owner_task_id; uint8_t data[64]; } managed_buffer_t;
static managed_buffer_t buffer_pool[configRTSFR_MAX_MANAGED_BUFFERS];



void rtsfr_resource_on_mutex_acquire(uint8_t task_id, uint16_t resource_id)
{
    if (!VALID_TASK(task_id) || resource_id >= configRTSFR_MAX_TRACKED_MUTEXES) return;
    taskENTER_CRITICAL();
    if (mutex_owner[resource_id] != RTSFR_NO_OWNER) { unknown_cnt[TASK_IDX(task_id)]++; }
    mutex_owner[resource_id] = OWN(task_id);
    taskEXIT_CRITICAL();
}


void rtsfr_resource_on_mutex_release(uint8_t task_id, uint16_t resource_id)
{
    if (!VALID_TASK(task_id) || resource_id >= configRTSFR_MAX_TRACKED_MUTEXES) return;
    taskENTER_CRITICAL();
    if (mutex_owner[resource_id] == OWN(task_id)) { mutex_owner[resource_id] = RTSFR_NO_OWNER; }
    else { unknown_cnt[TASK_IDX(task_id)]++; }
    taskEXIT_CRITICAL();
}


void rtsfr_resource_on_new_generation(uint8_t task_id, uint16_t generation)
{
    if (!VALID_TASK(task_id)) return;
    taskENTER_CRITICAL();
    cur_generation[TASK_IDX(task_id)] = generation;
    unknown_cnt[TASK_IDX(task_id)] = 0u;   /* instance moi: xoa bat thuong cua instance cu */
    taskEXIT_CRITICAL();
}


static void compute_summary(uint8_t task_id, rtsfr_resource_summary_t *out)
{
    uint8_t mtx = 0, bufs = 0; bool inflight = false, quarantined = false;
    for (uint8_t i = 0; i < configRTSFR_MAX_TRACKED_MUTEXES; i++)
        if (mutex_owner[i] == OWN(task_id)) mtx++;
    for (uint8_t i = 0; i < configRTSFR_MAX_MANAGED_BUFFERS; i++) {
        if (buffer_pool[i].state == BUF_FREE || buffer_pool[i].owner_task_id != OWN(task_id)) continue;
        bufs++;
        if (buffer_pool[i].state == BUF_IN_FLIGHT)   inflight = true;
        if (buffer_pool[i].state == BUF_QUARANTINED) quarantined = true;
    }
    out->task_id = task_id;
    out->generation = cur_generation[TASK_IDX(task_id)];
    out->mutex_held_count = mtx;
    out->managed_buffers_owned = bufs;
    out->unknown_resource_count = unknown_cnt[TASK_IDX(task_id)];
    out->has_inflight_io = inflight;
    out->can_reclaim_all = (mtx == 0u) && (out->unknown_resource_count == 0u) && !inflight && !quarantined;
}


rtsfr_status_t rtsfr_resource_snapshot(uint8_t task_id, uint16_t generation, rtsfr_resource_summary_t *out)
{
    if (!VALID_TASK(task_id) || out == NULL) return RTSFR_ERR_INVALID;
    taskENTER_CRITICAL();
    if (generation != cur_generation[TASK_IDX(task_id)]) { taskEXIT_CRITICAL(); return RTSFR_ERR_STALE; }
    compute_summary(task_id, out);
    taskEXIT_CRITICAL();
    return RTSFR_OK;
}


rtsfr_status_t rtsfr_resource_cleanup_safe(uint8_t task_id, uint16_t generation)
{
    rtsfr_resource_summary_t s;
    if (!VALID_TASK(task_id)) return RTSFR_ERR_INVALID;
    taskENTER_CRITICAL();
    if (generation != cur_generation[TASK_IDX(task_id)]) { taskEXIT_CRITICAL(); return RTSFR_ERR_STALE; }
    compute_summary(task_id, &s);
    if (!s.can_reclaim_all) { taskEXIT_CRITICAL(); return RTSFR_ERR_UNSAFE; }
    for (uint8_t i = 0; i < configRTSFR_MAX_MANAGED_BUFFERS; i++) {
        if (buffer_pool[i].state == BUF_OWNED && buffer_pool[i].owner_task_id == OWN(task_id)) {
            buffer_pool[i].state = BUF_FREE; buffer_pool[i].owner_task_id = RTSFR_NO_OWNER;
        }
    }
    taskEXIT_CRITICAL();
    return RTSFR_OK;
}


rtsfr_status_t rtsfr_buffer_acquire(uint8_t task_id, uint8_t *out_index)
{
    if (!VALID_TASK(task_id) || out_index == NULL) return RTSFR_ERR_INVALID;
    rtsfr_status_t r = RTSFR_ERR_NO_RESOURCE;
    taskENTER_CRITICAL();
    for (uint8_t i = 0; i < configRTSFR_MAX_MANAGED_BUFFERS; i++) {
        if (buffer_pool[i].state == BUF_FREE) {
            buffer_pool[i].state = BUF_OWNED; buffer_pool[i].owner_task_id = OWN(task_id);
            *out_index = i; r = RTSFR_OK; break;
        }
    }
    taskEXIT_CRITICAL();
    return r;
}


rtsfr_status_t rtsfr_buffer_release(uint8_t task_id, uint8_t index)
{
    rtsfr_status_t r = RTSFR_ERR_INVALID;
    if (!VALID_TASK(task_id) || index >= configRTSFR_MAX_MANAGED_BUFFERS) return r;
    taskENTER_CRITICAL();
    if (buffer_pool[index].state == BUF_OWNED && buffer_pool[index].owner_task_id == OWN(task_id)) {
        buffer_pool[index].state = BUF_FREE; buffer_pool[index].owner_task_id = RTSFR_NO_OWNER; r = RTSFR_OK;
    }
    taskEXIT_CRITICAL();
    return r;
}


static const void *mutex_handles[configRTSFR_MAX_TRACKED_MUTEXES];
static uint16_t anomaly_count;


rtsfr_status_t rtsfr_resource_register_mutex(const void *mutex_handle, uint16_t *out_resource_id)
{
    rtsfr_status_t r = RTSFR_ERR_NO_RESOURCE;
    int16_t free_slot = -1;
    if (mutex_handle == NULL || out_resource_id == NULL) return RTSFR_ERR_INVALID;
    taskENTER_CRITICAL();
    for (uint8_t i = 0; i < configRTSFR_MAX_TRACKED_MUTEXES; i++) {
        if (mutex_handles[i] == mutex_handle) { *out_resource_id = i; r = RTSFR_OK; free_slot = -2; break; }
        if (mutex_handles[i] == NULL && free_slot < 0) free_slot = i;
    }
    if (free_slot >= 0) {
        mutex_handles[free_slot] = mutex_handle; mutex_owner[free_slot] = RTSFR_NO_OWNER;
        *out_resource_id = (uint16_t)free_slot; r = RTSFR_OK;
    }
    taskEXIT_CRITICAL();
    return r;
}


rtsfr_status_t rtsfr_resource_unregister_mutex(const void *mutex_handle)
{
    rtsfr_status_t r = RTSFR_ERR_INVALID;
    taskENTER_CRITICAL();
    for (uint8_t i = 0; i < configRTSFR_MAX_TRACKED_MUTEXES; i++) {
        if (mutex_handles[i] == mutex_handle && mutex_handle != NULL) {
            if (mutex_owner[i] != RTSFR_NO_OWNER) { r = RTSFR_ERR_BUSY; }
            else { mutex_handles[i] = NULL; r = RTSFR_OK; }
            break;
        }
    }
    taskEXIT_CRITICAL();
    return r;
}


static bool find_mutex(const void *mutex_handle, uint16_t *rid)
{
    for (uint8_t i = 0; i < configRTSFR_MAX_TRACKED_MUTEXES; i++)
        if (mutex_handles[i] == mutex_handle && mutex_handle != NULL) { *rid = i; return true; }
    return false;
}
__attribute__((weak)) uint8_t rtsfr_detect_task_id_from_handle(const void *task_handle)
{ (void)task_handle; return RTSFR_TASK_ID_NONE; }
#if ( configRTSFR_ENABLE == 1 )
void rtsfr_resource_hook_mutex_taken(const void *mutex_handle, const void *holder_task_handle)
{
    uint16_t rid; uint8_t tid;
    if (!find_mutex(mutex_handle, &rid)) return;
    tid = rtsfr_detect_task_id_from_handle(holder_task_handle);
    if (tid == RTSFR_TASK_ID_NONE) return;
    rtsfr_resource_on_mutex_acquire(tid, rid);
}


void rtsfr_resource_hook_mutex_given(const void *mutex_handle, const void *holder_task_handle)
{
    uint16_t rid; uint8_t tid;
    if (!find_mutex(mutex_handle, &rid)) return;
    if (holder_task_handle == NULL) { anomaly_count++; return; }
    tid = rtsfr_detect_task_id_from_handle(holder_task_handle);
    if (tid == RTSFR_TASK_ID_NONE) return;
    rtsfr_resource_on_mutex_release(tid, rid);
}
#endif
uint16_t rtsfr_resource_get_anomaly_count(void) { return anomaly_count; }
