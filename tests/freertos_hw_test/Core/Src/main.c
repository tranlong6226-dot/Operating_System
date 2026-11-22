/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Dual-Mode Hardware Test (Vanilla FreeRTOS vs FreeRTOS+RTSFR)
  *                   Chuyển đổi linh hoạt bằng biến #define USE_RTSFR trong main.h
  ******************************************************************************
  * SƠ ĐỒ ĐẤU NỐI PHẦN CỨNG (Không cần cổng UART, quan sát 100% bằng LED):
  * - PB0  : LED Đỏ (SensorTask - Metronome nhịp 1Hz 500ms ON / 500ms OFF)
  * - PB1  : LED Xanh lá (CommTask - Chịu sự cố từ dây PA1)
  * - PC13 : LED On-board (Chỉ báo Supervisor & Safe Mode)
  * - PA1  : Dây Jumper cắm test (Cấu hình Input Pull-Down)
  *          + Cắm 3.3V : Trạng thái bình thường
  *          + Rút ra   : Giả lập ngoại vi kẹt / mất tín hiệu (0V)
  *
  * HÀNH VI QUAN SÁT THEO CHẾ ĐỘ:
  * 1. KHI USE_RTSFR == 1 (Hệ thống tự phục hồi RTSFR):
  *    - LED Xanh chớp theo số thế hệ Generation:
  *        + Gen 1: Chớp đơn (1 nhịp rồi nghỉ 800ms)
  *        + Gen 2: Chớp kép (2 nhịp Tách-Tách rồi nghỉ 800ms)
  *        + Gen 3: Chớp ba (3 nhịp Tách-Tách-Tách rồi nghỉ 800ms)
  *    - Rút dây PA1: LED Xanh tắt. Sau 2s Timeout, LED PC13 chớp Strobe nhanh
  *      báo hiệu Supervisor đang tiêu hủy task cũ và tái sinh task thế hệ mới.
  *    - Cắm lại dây: LED Xanh sống lại với nhịp chớp KÉP (Gen 2)!
  *    - Rút dây vĩnh viễn: Sau 3 lần retry, kích hoạt SAFE MODE -> LED PC13 SÁNG ĐỨNG.
  *
  * 2. KHI USE_RTSFR == 0 (FreeRTOS nguyên bản):
  *    - Rút dây PA1: LED Xanh tắt lịm, FreeRTOS không phát hiện, không phục hồi.
  *    - Cắm lại dây: Chỉ chớp đều 500ms như cũ (hoàn toàn không có đếm thế hệ).
  *    - Rút dây vĩnh viễn: Task chết mãi mãi, PC13 không bao giờ vào Safe Mode.
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#if (USE_RTSFR == 1)
#include "rtsfr_types.h"
#include "rtsfr_detect.h"
#include "rtsfr_policy.h"
#include "rtsfr_resource.h"
#include "rtsfr_recovery.h"
#include "rtsfr_safe_mode.h"
#endif

/* Private defines -----------------------------------------------------------*/
#define RTSFR_SENSOR_TASK_ID   1U
#define RTSFR_COMM_TASK_ID     2U
#define RTSFR_CONTROL_TASK_ID  3U

#define RTSFR_TASK_STACK_WORDS 160U
#define RTSFR_PROGRESS_TIMEOUT pdMS_TO_TICKS(2000U)
#define RTSFR_VERIFY_TIMEOUT   pdMS_TO_TICKS(500U)

#define SENSOR_TASK_PRIORITY   (tskIDLE_PRIORITY + 1U)
#define COMM_TASK_PRIORITY     (tskIDLE_PRIORITY + 2U)
#define CONTROL_TASK_PRIORITY  (tskIDLE_PRIORITY + 3U)
#define SUPERVISOR_PRIORITY    (tskIDLE_PRIORITY + 4U)

/* Context structure for demo tasks */
typedef struct
{
    uint8_t task_id;
    volatile uint16_t generation;
    TaskHandle_t handle;
} rtsfr_demo_context_t;

/* Global variables for system monitoring */
volatile rtsfr_demo_context_t g_contexts[3];
volatile bool g_safe_output_active = false;
volatile bool g_task_isolated[3] = {false, false, false};

#if (USE_RTSFR == 1)
volatile rtsfr_recovery_result_t g_last_recovery;
TaskHandle_t g_supervisor_handle = NULL;
#endif

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart1;

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);

void SensorTask(void *argument);
void CommTask(void *argument);
void ControlTask(void *argument);

#if (USE_RTSFR == 1)
void SupervisorTask(void *argument);

/* RTSFR Recovery Hooks ------------------------------------------------------*/
static rtsfr_status_t prv_prepare_delete(uint8_t task_id, uint16_t generation, TaskHandle_t handle)
{
    rtsfr_task_record_t record;
    if ((rtsfr_detect_get_record(task_id, &record) != RTSFR_OK) ||
        (record.generation != generation) ||
        (record.task_handle != handle))
    {
        return RTSFR_ERR_STALE;
    }
    rtsfr_detect_on_task_delete(handle);
    return RTSFR_OK;
}

static rtsfr_status_t prv_publish_generation(uint8_t task_id, uint16_t generation, TaskHandle_t new_handle)
{
    if ((task_id == 0U) || (task_id > configRTSFR_MAX_TASKS) || (new_handle == NULL))
    {
        return RTSFR_ERR_INVALID;
    }
    g_contexts[task_id - 1U].handle = new_handle;
    g_contexts[task_id - 1U].generation = generation;
    rtsfr_detect_on_new_generation(task_id, new_handle, generation);
    return RTSFR_OK;
}

static rtsfr_status_t prv_create_task(TaskFunction_t entry, const char *name,
                                      configSTACK_DEPTH_TYPE stack_depth, void *argument,
                                      UBaseType_t priority, TaskHandle_t *out_handle)
{
    return (xTaskCreate(entry, name, stack_depth, argument, priority, out_handle) == pdPASS)
               ? RTSFR_OK
               : RTSFR_ERR_NO_RESOURCE;
}

static rtsfr_status_t prv_reset_context(uint8_t task_id, uint16_t new_generation, void *argument)
{
    rtsfr_demo_context_t *ctx = argument;
    if ((ctx == NULL) || (ctx->task_id != task_id))
    {
        return RTSFR_ERR_INVALID;
    }
    ctx->generation = new_generation;
    return RTSFR_OK;
}

static void prv_record_result(const rtsfr_recovery_result_t *result)
{
    if (result != NULL)
    {
        g_last_recovery = *result;
        rtsfr_policy_on_recovery_result(result);
    }
}

static void prv_safe_output(const rtsfr_safe_mode_info_t *info)
{
    g_safe_output_active = true;
    uint8_t failed_id = (info != NULL) ? info->task_id : 0U;

    if ((failed_id >= 1U) && (failed_id <= configRTSFR_MAX_TASKS))
    {
        g_task_isolated[failed_id - 1U] = true;

        if (failed_id == RTSFR_COMM_TASK_ID)
        {
            HAL_GPIO_WritePin(COMM_LED_GPIO_Port, COMM_LED_Pin, GPIO_PIN_RESET);
        }
        else if (failed_id == RTSFR_SENSOR_TASK_ID)
        {
            HAL_GPIO_WritePin(SENSOR_LED_GPIO_Port, SENSOR_LED_Pin, GPIO_PIN_RESET);
        }

        if (g_contexts[failed_id - 1U].handle != NULL)
        {
            TaskHandle_t h = g_contexts[failed_id - 1U].handle;
            vTaskSuspend(h);
            rtsfr_detect_on_task_delete(h);
        }
    }

    /* Đèn PC13 sáng đứng cố định (PC13 active LOW: RESET = SÁNG) */
    HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, GPIO_PIN_RESET);
}

static void prv_request_reset(uint16_t reason_code)
{
    (void)reason_code;
    g_safe_output_active = true;
    HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, GPIO_PIN_RESET);
}

/**
  * @brief  Khởi tạo hệ thống RTSFR và đăng ký 3 Task
  */
static void rtsfr_demo_init(void)
{
    rtsfr_recovery_hooks_t hooks;
    memset(&hooks, 0, sizeof(hooks));
    hooks.resource_snapshot = rtsfr_resource_snapshot;
    hooks.resource_cleanup = rtsfr_resource_cleanup_safe;
    hooks.prepare_delete = prv_prepare_delete;
    hooks.publish_generation = prv_publish_generation;
    hooks.create_task = prv_create_task;
    hooks.record_result = prv_record_result;
    hooks.request_reset = prv_request_reset;
    hooks.apply_safe_output = prv_safe_output;

    rtsfr_resource_init();
    (void)rtsfr_detect_init();
    rtsfr_policy_init();
    rtsfr_recovery_init(&hooks);

    for (uint8_t id = 1U; id <= configRTSFR_MAX_TASKS; id++)
    {
        rtsfr_task_config_t detect_cfg;
        rtsfr_recovery_task_config_t rec_cfg;
        TaskFunction_t entry_fn = NULL;
        const char *name = NULL;
        UBaseType_t prio = tskIDLE_PRIORITY + 1U;
        TickType_t timeout = RTSFR_PROGRESS_TIMEOUT;

        g_contexts[id - 1U].task_id = id;
        g_contexts[id - 1U].generation = 1U;

        if (id == RTSFR_SENSOR_TASK_ID)
        {
            entry_fn = SensorTask;
            name = "SensorTask";
            prio = SENSOR_TASK_PRIORITY;
            timeout = pdMS_TO_TICKS(1500U);
        }
        else if (id == RTSFR_COMM_TASK_ID)
        {
            entry_fn = CommTask;
            name = "CommTask";
            prio = COMM_TASK_PRIORITY;
            timeout = RTSFR_PROGRESS_TIMEOUT; /* 2000ms */
        }
        else
        {
            entry_fn = ControlTask;
            name = "ControlTask";
            prio = CONTROL_TASK_PRIORITY;
            timeout = pdMS_TO_TICKS(3500U);
        }

        TaskHandle_t created_handle = NULL;
        (void)xTaskCreate(entry_fn, name, RTSFR_TASK_STACK_WORDS,
                          (void *)&g_contexts[id - 1U], prio,
                          &created_handle);
        g_contexts[id - 1U].handle = created_handle;

        detect_cfg.task_id = id;
        detect_cfg.task_name = name;
        detect_cfg.timeout_ticks = timeout;
        detect_cfg.criticality = (id == RTSFR_CONTROL_TASK_ID) ? RTSFR_CRIT_HIGH : RTSFR_CRIT_MEDIUM;
        (void)rtsfr_register_task(&detect_cfg, g_contexts[id - 1U].handle);

        (void)rtsfr_policy_register_task(id, 1U, detect_cfg.criticality);

        memset(&rec_cfg, 0, sizeof(rec_cfg));
        rec_cfg.task_id = id;
        rec_cfg.initial_generation = 1U;
        rec_cfg.entry = entry_fn;
        rec_cfg.argument = (void *)&g_contexts[id - 1U];
        rec_cfg.stack_depth = RTSFR_TASK_STACK_WORDS;
        rec_cfg.priority = prio;
        rec_cfg.name = name;
        rec_cfg.verify_timeout_ticks = RTSFR_VERIFY_TIMEOUT;
        rec_cfg.reset_context = prv_reset_context;
        (void)rtsfr_recovery_register_task(&rec_cfg, g_contexts[id - 1U].handle);
    }
}
#endif /* USE_RTSFR == 1 */

/**
  * @brief  Entry point
  */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART1_UART_Init();

#if (USE_RTSFR == 1)
    const char *banner = "\r\n========================================\r\n"
                         "  RTSFR Hardware Test [MODE: 1 - RTSFR ON]\r\n"
                         "  - PB0: Sensor (1Hz Metronome)\r\n"
                         "  - PB1: Comm (PA1 test, Pulse = Gen)\r\n"
                         "  - PC13: Supervisor / Safe Mode Indicator\r\n"
                         "========================================\r\n";
    HAL_UART_Transmit(&huart1, (uint8_t *)banner, (uint16_t)strlen(banner), 500);

    /* Khởi tạo hệ thống RTSFR */
    rtsfr_demo_init();

    /* Tạo Supervisor Task */
    (void)xTaskCreate(SupervisorTask, "Supervisor", 256U, NULL,
                      SUPERVISOR_PRIORITY, &g_supervisor_handle);
#else
    const char *banner = "\r\n========================================\r\n"
                         "  RTSFR Hardware Test [MODE: 0 - VANILLA]\r\n"
                         "  - PB0: Sensor (1Hz Metronome)\r\n"
                         "  - PB1: Comm (PA1 test, No Recovery)\r\n"
                         "  - PC13: Regular Slow Blink\r\n"
                         "========================================\r\n";
    HAL_UART_Transmit(&huart1, (uint8_t *)banner, (uint16_t)strlen(banner), 500);

    /* Tạo các Task FreeRTOS thông thường (không dùng RTSFR) */
    (void)xTaskCreate(SensorTask, "SensorTask", RTSFR_TASK_STACK_WORDS, NULL,
                      SENSOR_TASK_PRIORITY, NULL);
    (void)xTaskCreate(CommTask, "CommTask", RTSFR_TASK_STACK_WORDS, NULL,
                      COMM_TASK_PRIORITY, NULL);
    (void)xTaskCreate(ControlTask, "ControlTask", RTSFR_TASK_STACK_WORDS, NULL,
                      CONTROL_TASK_PRIORITY, NULL);
#endif

    /* Bắt đầu FreeRTOS Scheduler */
    vTaskStartScheduler();

    while (1)
    {
    }
}

/**
  * @brief  Task 1: SensorTask (PB0) - Metronome 1Hz
  *         Chứng minh FreeRTOS Kernel và các Task độc lập luôn chạy bình thường.
  */
void SensorTask(void *argument)
{
#if (USE_RTSFR == 1)
    rtsfr_demo_context_t *ctx = (rtsfr_demo_context_t *)argument;
#else
    (void)argument;
#endif
    for (;;)
    {
#if (USE_RTSFR == 1)
        if (ctx != NULL)
        {
            (void)rtsfr_report_progress(ctx->task_id);
            rtsfr_recovery_on_first_progress(ctx->task_id, ctx->generation);
        }
#endif
        HAL_GPIO_TogglePin(SENSOR_LED_GPIO_Port, SENSOR_LED_Pin);
        vTaskDelay(pdMS_TO_TICKS(500U));
    }
}

/**
  * @brief  Task 2: CommTask (PB1) - Target Task bị lỗi bởi chân PA1
  */
void CommTask(void *argument)
{
#if (USE_RTSFR == 1)
    rtsfr_demo_context_t *ctx = (rtsfr_demo_context_t *)argument;
    for (;;)
    {
        /* --- GIẢ LẬP LỖI PHẦN CỨNG: Rút dây PA1 ra (mức 0) --- */
        if (HAL_GPIO_ReadPin(FAULT_INJECT_GPIO_Port, FAULT_INJECT_Pin) == GPIO_PIN_RESET)
        {
            /* Mô phỏng lỗi ngoại vi kẹt cứng (Deadlock): Task kẹt vô tận, KHÔNG BÁO TIẾN ĐỘ.
             * Trong RTSFR: Module 1 sẽ bắt timeout, Module 4 tiêu diệt task kẹt này
             * và tái sinh Task thế hệ mới! */
            for (;;)
            {
                vTaskDelay(pdMS_TO_TICKS(50U));
            }
        }

        /* Khi dây PA1 cắm 3.3V (mức 1): Báo tiến độ */
        if (rtsfr_report_progress(ctx->task_id) == RTSFR_OK)
        {
            rtsfr_recovery_on_first_progress(ctx->task_id, ctx->generation);

            if (!g_task_isolated[ctx->task_id - 1U])
            {
                /* MÃ HÓA THẾ HỆ GENERATION RA SỐ NHỊP CHỚP CỦA LED XANH:
                 * Gen 1: Chớp 1 cái rồi nghỉ 800ms
                 * Gen 2: Chớp 2 cái (Tách - Tách) rồi nghỉ 800ms
                 * Gen 3: Chớp 3 cái (Tách - Tách - Tách) rồi nghỉ 800ms
                 */
                uint16_t pulses = (ctx->generation > 4U) ? 4U : ctx->generation;
                for (uint16_t i = 0; i < pulses; i++)
                {
                    HAL_GPIO_WritePin(COMM_LED_GPIO_Port, COMM_LED_Pin, GPIO_PIN_SET);
                    vTaskDelay(pdMS_TO_TICKS(80U));
                    HAL_GPIO_WritePin(COMM_LED_GPIO_Port, COMM_LED_Pin, GPIO_PIN_RESET);
                    vTaskDelay(pdMS_TO_TICKS(120U));
                }
                vTaskDelay(pdMS_TO_TICKS(800U));
            }
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(100U));
        }
    }
#else /* USE_RTSFR == 0 (Vanilla FreeRTOS chuẩn) */
    (void)argument;
    for (;;)
    {
        /* Kiểm tra chân PA1: Rút dây ra (mức 0) */
        if (HAL_GPIO_ReadPin(FAULT_INJECT_GPIO_Port, FAULT_INJECT_Pin) == GPIO_PIN_RESET)
        {
            /* MÔ PHỎNG NGOẠI VI KẸT CỨNG (DEADLOCK / DRIVER CRASH THỰC TẾ):
             * Task bị rơi vào vòng lặp lỗi không lối thoát (như kẹt I2C BUSY hoặc Error_Handler).
             * Trong FreeRTOS chuẩn không có RTSFR: Task CHẾT CỨNG TẠI ĐÂY!
             * DÙ NGƯỜI DÙNG CÓ CẮM LẠI DÂY PA1 THÌ TASK CŨNG KHÔNG THỂ TỰ PHỤC HỒI! */
            for (;;)
            {
                vTaskDelay(portMAX_DELAY);
            }
        }

        /* Chớp đơn giản 500ms (Không có đếm thế hệ, không bao giờ đổi nhịp) */
        HAL_GPIO_TogglePin(COMM_LED_GPIO_Port, COMM_LED_Pin);
        vTaskDelay(pdMS_TO_TICKS(500U));
    }
#endif
}

/**
  * @brief  Task 3: ControlTask
  */
void ControlTask(void *argument)
{
#if (USE_RTSFR == 1)
    rtsfr_demo_context_t *ctx = (rtsfr_demo_context_t *)argument;
    for (;;)
    {
        /* Báo tiến độ định kỳ cho Module 1 */
        if (rtsfr_report_progress(ctx->task_id) == RTSFR_OK)
        {
            rtsfr_recovery_on_first_progress(ctx->task_id, ctx->generation);
        }
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
#else /* USE_RTSFR == 0 */
    (void)argument;
    for (;;)
    {
        /* FreeRTOS chuẩn: Chớp chậm 1000ms trên PC13, không giám sát gì */
        HAL_GPIO_TogglePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin);
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
#endif
}

#if (USE_RTSFR == 1)
/**
  * @brief  Supervisor Task: Quét phát hiện sự cố và chỉ đạo phục hồi
  */
void SupervisorTask(void *argument)
{
    (void)argument;
    rtsfr_fault_event_t fault;
    rtsfr_recovery_state_t rec_state;
    TickType_t last_heartbeat = 0;
    uint8_t strobe_divider = 0;

    for (;;)
    {
        /* 1. Module 1 quét phát hiện lỗi quá hạn tiến độ */
        (void)rtsfr_detect_scan(xTaskGetTickCount());

        /* 2. Lấy các sự kiện lỗi từ Module 1 đưa sang Module 2 & 3 */
        while (rtsfr_detect_pop_event(&fault))
        {
            if ((fault.task_id >= 1U) && (fault.task_id <= configRTSFR_MAX_TASKS) &&
                g_task_isolated[fault.task_id - 1U])
            {
                continue;
            }

            rtsfr_resource_summary_t resource;
            if (rtsfr_resource_snapshot(fault.task_id, fault.generation, &resource) != RTSFR_OK)
            {
                memset(&resource, 0, sizeof(resource));
                resource.task_id = fault.task_id;
                resource.generation = fault.generation;
                resource.can_reclaim_all = true;
            }

            rtsfr_decision_t decision = rtsfr_policy_evaluate(&fault, &resource);
            if (rtsfr_policy_should_invoke_recovery(&decision))
            {
                (void)rtsfr_recovery_request(&decision);
            }
        }

        /* 3. Module 4 xử lý cỗ máy trạng thái khôi phục (Recreate task thế hệ mới) */
        rec_state = rtsfr_recovery_get_state();
        if ((rec_state >= RTSFR_REC_STATE_QUIESCING) &&
            (rec_state <= RTSFR_REC_STATE_VERIFYING))
        {
            (void)rtsfr_recovery_process();

            /* Trong lúc đang khôi phục: Chớp Strobe nhanh trên LED Bo PC13 (~12Hz) */
            if (++strobe_divider >= 4U)
            {
                strobe_divider = 0;
                HAL_GPIO_TogglePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin);
            }
        }
        else
        {
            strobe_divider = 0;
            if (g_safe_output_active || rtsfr_safe_mode_is_active())
            {
                /* Safe Mode: LED PC13 SÁNG ĐỨNG CỐ ĐỊNH (RESET = SÁNG trên Blue Pill) */
                HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, GPIO_PIN_RESET);
            }
            else
            {
                /* Bình thường: Heartbeat nhấp nháy 1 lần 50ms mỗi 1000ms */
                TickType_t now = xTaskGetTickCount();
                if ((now - last_heartbeat) >= pdMS_TO_TICKS(1000U))
                {
                    last_heartbeat = now;
                    HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, GPIO_PIN_RESET); /* Sáng 50ms */
                }
                else if ((now - last_heartbeat) >= pdMS_TO_TICKS(50U))
                {
                    HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, GPIO_PIN_SET);   /* Tắt */
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10U));
    }
}
#endif /* USE_RTSFR == 1 */

/**
  * @brief System Clock Configuration
  */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                                |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief USART1 Initialization Function
  */
static void MX_USART1_UART_Init(void)
{
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief GPIO Initialization Function
  */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* Đặt mức ban đầu: LEDs tắt */
    HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, SENSOR_LED_Pin | COMM_LED_Pin, GPIO_PIN_RESET);

    /* Cấu hình PC13 (STATUS_LED_Pin) */
    GPIO_InitStruct.Pin = STATUS_LED_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(STATUS_LED_GPIO_Port, &GPIO_InitStruct);

    /* Cấu hình PB0 (SENSOR_LED_Pin) & PB1 (COMM_LED_Pin) */
    GPIO_InitStruct.Pin = SENSOR_LED_Pin | COMM_LED_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* Cấu hình PA1 (FAULT_INJECT_Pin) làm INPUT PULLDOWN */
    GPIO_InitStruct.Pin = FAULT_INJECT_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(FAULT_INJECT_GPIO_Port, &GPIO_InitStruct);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM4)
    {
        HAL_IncTick();
    }
}

void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
    }
}
