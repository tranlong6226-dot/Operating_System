#include "rtsfr_safe_mode.h"

static bool g_safe_mode_active;
static rtsfr_safe_mode_info_t g_safe_mode_info;
static rtsfr_safe_output_fn g_safe_output_hook;

void rtsfr_safe_mode_init(rtsfr_safe_output_fn output_hook)
{
    taskENTER_CRITICAL();
    {
        g_safe_mode_active = false;
        g_safe_mode_info.event_id = 0U;
        g_safe_mode_info.task_id = 0U;
        g_safe_mode_info.generation = 0U;
        g_safe_mode_info.reason_code = 0U;
        g_safe_mode_info.entered_tick = 0U;
        g_safe_output_hook = output_hook;
    }
    taskEXIT_CRITICAL();
}

rtsfr_status_t rtsfr_safe_mode_enter(uint32_t event_id,
                                      uint8_t task_id,
                                      uint16_t generation,
                                      uint16_t reason_code)
{
    rtsfr_safe_output_fn output_hook;
    rtsfr_safe_mode_info_t info;

    taskENTER_CRITICAL();
    {
        if (!g_safe_mode_active)
        {
            g_safe_mode_info.event_id = event_id;
            g_safe_mode_info.task_id = task_id;
            g_safe_mode_info.generation = generation;
            g_safe_mode_info.reason_code = reason_code;
            g_safe_mode_info.entered_tick = xTaskGetTickCount();
            g_safe_mode_active = true;
        }

        info = g_safe_mode_info;
        output_hook = g_safe_output_hook;
    }
    taskEXIT_CRITICAL();

    if (output_hook != NULL)
    {
        output_hook(&info);
    }

    return RTSFR_OK;
}

bool rtsfr_safe_mode_is_active(void)
{
    bool active;

    taskENTER_CRITICAL();
    {
        active = g_safe_mode_active;
    }
    taskEXIT_CRITICAL();

    return active;
}

rtsfr_status_t rtsfr_safe_mode_get_info(rtsfr_safe_mode_info_t *out)
{
    if (out == NULL)
    {
        return RTSFR_ERR_INVALID;
    }

    taskENTER_CRITICAL();
    {
        if (!g_safe_mode_active)
        {
            taskEXIT_CRITICAL();
            return RTSFR_ERR_NOT_READY;
        }

        *out = g_safe_mode_info;
    }
    taskEXIT_CRITICAL();

    return RTSFR_OK;
}
