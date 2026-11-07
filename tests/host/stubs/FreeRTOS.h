#ifndef HOST_FREERTOS_H
#define HOST_FREERTOS_H
#include <stdint.h>
#include <stddef.h>
typedef uint32_t TickType_t;
typedef unsigned int UBaseType_t;
typedef int BaseType_t;
#define configSTACK_DEPTH_TYPE uint16_t
#define configMAX_TASK_NAME_LEN 16
#define configMAX_PRIORITIES 56
#define configTICK_RATE_HZ 1000
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define pdPASS 1
#define pdFAIL 0
void mock_enter(void);
void mock_exit(void);
#define taskENTER_CRITICAL() mock_enter()
#define taskEXIT_CRITICAL() mock_exit()
#define taskENTER_CRITICAL_FROM_ISR() (mock_enter(), 0U)
#define taskEXIT_CRITICAL_FROM_ISR(mask) ((void)(mask), mock_exit())
#define taskYIELD() ((void)0)
#endif
