#include "FreeRTOS.h"

#if (configRTSFR_M4_TARGET_TEST == 1)

#include "main.h"
#include "rtsfr_recovery.h"
#include "task.h"
#include "test_m4.h"

#define M4_TEST_TASK_ID              1U
#define M4_TEST_INITIAL_GENERATION   1U
#define M4_TEST_EVENT_ID             1UL
#define M4_TEST_TASK_STACK_WORDS     128U
#define M4_TEST_VERIFY_TIMEOUT       pdMS_TO_TICKS(250U)
#define M4_TEST_PROCESS_TIMEOUT      pdMS_TO_TICKS(1000U)

#define M4_TEST_SCENARIO_CLEAN_RESTART  1
#define M4_TEST_SCENARIO_UNSAFE_MUTEX   2
#define M4_TEST_SCENARIO_FALSE_ALARM    3
#define M4_TEST_SCENARIO_STALE_EVENT    4

#ifndef configRTSFR_M4_TEST_SCENARIO
#define configRTSFR_M4_TEST_SCENARIO M4_TEST_SCENARIO_CLEAN_RESTART
#endif

volatile rtsfr_m4_test_result_t g_rtsfr_m4_test_result =
    RTSFR_M4_TEST_NOT_RUN;
volatile unsigned long g_rtsfr_m4_test_progress_count;
volatile unsigned short g_rtsfr_m4_test_reason;

static volatile bool g_victim_faulted;
static volatile uint16_t g_victim_generation = M4_TEST_INITIAL_GENERATION;
static TaskHandle_t g_victim_handle;

static void prv_victim_task(void *argument)
{
    (void)argument;

    for (;;)
    {
        if (!g_victim_faulted)
        {
            g_rtsfr_m4_test_progress_count++;
            rtsfr_recovery_on_first_progress(M4_TEST_TASK_ID,
                                             g_victim_generation);
        }

        vTaskDelay(pdMS_TO_TICKS(10U));
    }
}

static rtsfr_status_t prv_snapshot(uint8_t task_id,
                                   uint16_t generation,
                                   rtsfr_resource_summary_t *out)
{
    if ((task_id != M4_TEST_TASK_ID) ||
        (generation != g_victim_generation) || (out == NULL))
    {
        return RTSFR_ERR_STALE;
    }

    out->task_id = task_id;
    out->generation = generation;
    out->mutex_held_count =
        (configRTSFR_M4_TEST_SCENARIO == M4_TEST_SCENARIO_UNSAFE_MUTEX)
            ? 1U
            : 0U;
    out->managed_buffers_owned = 0U;
    out->unknown_resource_count = 0U;
    out->has_inflight_io = false;
    out->can_reclaim_all = true;
    return RTSFR_OK;
}

static rtsfr_status_t prv_reset_context(uint8_t task_id,
                                        uint16_t new_generation,
                                        void *argument)
{
    (void)argument;

    if ((task_id != M4_TEST_TASK_ID) ||
        (new_generation != (uint16_t)(g_victim_generation + 1U)))
    {
        return RTSFR_ERR_INVALID;
    }

    g_victim_generation = new_generation;
    g_victim_faulted = false;
    return RTSFR_OK;
}

static rtsfr_status_t prv_publish_generation(uint8_t task_id,
                                              uint16_t generation,
                                              TaskHandle_t new_handle)
{
    if ((task_id != M4_TEST_TASK_ID) ||
        (generation != g_victim_generation) || (new_handle == NULL))
    {
        return RTSFR_ERR_INVALID;
    }

    g_victim_handle = new_handle;
    return RTSFR_OK;
}

static void prv_safe_output(const rtsfr_safe_mode_info_t *info)
{
    if (info != NULL)
    {
        g_rtsfr_m4_test_reason = info->reason_code;
    }

    /* Blue Pill PC13 LED is active-low: ON means safe-mode is latched. */
    HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, GPIO_PIN_RESET);
}

static bool prv_process_until_terminal(uint32_t event_id,
                                       rtsfr_recovery_result_t *result)
{
    TickType_t started = xTaskGetTickCount();

    while ((TickType_t)(xTaskGetTickCount() - started) <
           M4_TEST_PROCESS_TIMEOUT)
    {
        (void)rtsfr_recovery_process();
        if (rtsfr_recovery_get_result(event_id, result) == RTSFR_OK)
        {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(1U));
    }

    return false;
}

static bool prv_run_selected_scenario(void)
{
    rtsfr_recovery_hooks_t hooks = {0};
    rtsfr_recovery_task_config_t task_config = {0};
    rtsfr_decision_t decision = {0};
    rtsfr_recovery_result_t result;
    rtsfr_status_t status;
    unsigned long progress_before;

    hooks.resource_snapshot = prv_snapshot;
    hooks.publish_generation = prv_publish_generation;
    hooks.apply_safe_output = prv_safe_output;
    rtsfr_recovery_init(&hooks);

    if (xTaskCreate(prv_victim_task,
                    "m4Victim",
                    M4_TEST_TASK_STACK_WORDS,
                    NULL,
                    tskIDLE_PRIORITY + 1U,
                    &g_victim_handle) != pdPASS)
    {
        return false;
    }

    task_config.task_id = M4_TEST_TASK_ID;
    task_config.initial_generation = M4_TEST_INITIAL_GENERATION;
    task_config.entry = prv_victim_task;
    task_config.argument = NULL;
    task_config.stack_depth = M4_TEST_TASK_STACK_WORDS;
    task_config.priority = tskIDLE_PRIORITY + 1U;
    task_config.name = "m4Victim";
    task_config.verify_timeout_ticks = M4_TEST_VERIFY_TIMEOUT;
    task_config.reset_context = prv_reset_context;

    if (rtsfr_recovery_register_task(&task_config, g_victim_handle) !=
        RTSFR_OK)
    {
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(30U));
    progress_before = g_rtsfr_m4_test_progress_count;

    decision.event_id = M4_TEST_EVENT_ID;
    decision.task_id = M4_TEST_TASK_ID;
    decision.generation = M4_TEST_INITIAL_GENERATION;
    decision.action = RTSFR_ACT_RESTART;

    if (configRTSFR_M4_TEST_SCENARIO == M4_TEST_SCENARIO_STALE_EVENT)
    {
        decision.generation++;
        return (rtsfr_recovery_request(&decision) == RTSFR_ERR_STALE) &&
               (g_rtsfr_m4_test_progress_count >= progress_before);
    }

    if (rtsfr_recovery_request(&decision) != RTSFR_OK)
    {
        return false;
    }

    if (configRTSFR_M4_TEST_SCENARIO == M4_TEST_SCENARIO_FALSE_ALARM)
    {
        status = rtsfr_recovery_cancel_pending(decision.event_id,
                                               decision.task_id,
                                               decision.generation);
        if ((status != RTSFR_OK) ||
            !prv_process_until_terminal(decision.event_id, &result))
        {
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(30U));
        g_rtsfr_m4_test_reason = result.reason_code;
        return (result.outcome == RTSFR_RECOVERY_REJECTED) &&
               (result.reason_code == RTSFR_REASON_FALSE_ALARM_ROLLBACK) &&
               (g_rtsfr_m4_test_progress_count > progress_before);
    }

    g_victim_faulted = true;
    if (!prv_process_until_terminal(decision.event_id, &result))
    {
        return false;
    }
    g_rtsfr_m4_test_reason = result.reason_code;

    if (configRTSFR_M4_TEST_SCENARIO == M4_TEST_SCENARIO_UNSAFE_MUTEX)
    {
        return (result.outcome == RTSFR_RECOVERY_UNSAFE_RESOURCE) &&
               (result.reason_code == RTSFR_REASON_MUTEX_HELD) &&
               rtsfr_safe_mode_is_active();
    }

    return (result.outcome == RTSFR_RECOVERY_OK) &&
           (result.old_generation == M4_TEST_INITIAL_GENERATION) &&
           (result.new_generation ==
            (uint16_t)(M4_TEST_INITIAL_GENERATION + 1U)) &&
           (g_rtsfr_m4_test_progress_count > progress_before) &&
           !rtsfr_safe_mode_is_active();
}

void rtsfr_m4_target_test_run(void)
{
    TickType_t blink_delay;

    g_rtsfr_m4_test_result = RTSFR_M4_TEST_RUNNING;
    g_rtsfr_m4_test_result = prv_run_selected_scenario()
                                 ? RTSFR_M4_TEST_PASS
                                 : RTSFR_M4_TEST_FAIL;

    blink_delay = (g_rtsfr_m4_test_result == RTSFR_M4_TEST_PASS)
                      ? pdMS_TO_TICKS(500U)
                      : pdMS_TO_TICKS(100U);

    for (;;)
    {
        HAL_GPIO_TogglePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin);
        vTaskDelay(blink_delay);
    }
}

#else

/* Keep the translation unit non-empty in the normal (non-test) build. */
typedef int rtsfr_m4_target_test_disabled_t;

#endif /* configRTSFR_M4_TARGET_TEST */
