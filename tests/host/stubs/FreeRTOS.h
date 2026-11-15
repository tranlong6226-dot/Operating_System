#ifndef HOST_FREERTOS_H
#define HOST_FREERTOS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef uint32_t TickType_t;
typedef unsigned int UBaseType_t;
typedef int BaseType_t;

#define portMAX_DELAY (0xFFFFFFFFUL)
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0

#define configSTACK_DEPTH_TYPE uint16_t
#define configMAX_TASK_NAME_LEN 16
#define configMAX_PRIORITIES 56
#define configTICK_RATE_HZ 1000
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

#ifndef taskENTER_CRITICAL
#define taskENTER_CRITICAL() ((void)0)
#endif

#ifndef taskEXIT_CRITICAL
#define taskEXIT_CRITICAL() ((void)0)
#endif

#ifndef taskENTER_CRITICAL_FROM_ISR
#define taskENTER_CRITICAL_FROM_ISR() (0U)
#endif

#ifndef taskEXIT_CRITICAL_FROM_ISR
#define taskEXIT_CRITICAL_FROM_ISR(mask) ((void)(mask))
#endif

#ifndef taskYIELD
#define taskYIELD() ((void)0)
#endif

#endif /* HOST_FREERTOS_H */
