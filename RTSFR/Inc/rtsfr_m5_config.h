#ifndef RTSFR_M5_CONFIG_H
#define RTSFR_M5_CONFIG_H

/* Override through build defines. Unknown board/experiment values stay unset. */
#ifndef configRTSFR_M5_ENABLE
#define configRTSFR_M5_ENABLE 1
#endif
#ifndef RTSFR_M5_TEST_BUILD
#define RTSFR_M5_TEST_BUILD 0
#endif
#define RTSFR_ENABLE_FAULT_INJECTION RTSFR_M5_TEST_BUILD
#ifndef RTSFR_M5_MEASURE_TRACE
#define RTSFR_M5_MEASURE_TRACE 0
#endif
#ifndef RTSFR_M5_VERSION
#define RTSFR_M5_VERSION "M5_ONLY"
#endif
#ifndef RTSFR_TRACE_CAPACITY
#define RTSFR_TRACE_CAPACITY 32U
#endif
#define RTSFR_VERIFY_MAX_TASKS 3U
#define RTSFR_VERIFY_MAX_RESOURCES 8U
#define RTSFR_HEALTH_TIMEOUT_MS 500U
#define RTSFR_RX_CAPACITY 128U
#define RTSFR_COMMAND_CAPACITY 96U
/* IWDG is started only by an explicit command after configuration/health checks. */
#ifndef RTSFR_IWDG_CONFIG_CONFIRMED
#define RTSFR_IWDG_CONFIG_CONFIRMED 0
#endif
#ifndef RTSFR_IWDG_PRESCALER_CODE
#define RTSFR_IWDG_PRESCALER_CODE 6U /* /256; proposed, NOT board-validated */
#endif
#ifndef RTSFR_IWDG_RELOAD
#define RTSFR_IWDG_RELOAD 624U /* 4 s at nominal 40 kHz, NOT measured */
#endif
#endif
