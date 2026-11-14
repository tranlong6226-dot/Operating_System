#ifndef HOST_TASK_H
#define HOST_TASK_H

#include "FreeRTOS.h"

typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

TickType_t xTaskGetTickCount(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
void vTaskDelay(TickType_t ticks);
void vTaskSuspend(TaskHandle_t handle);
void vTaskResume(TaskHandle_t handle);
void vTaskDelete(TaskHandle_t handle);
BaseType_t xTaskCreate(TaskFunction_t entry, const char *name, configSTACK_DEPTH_TYPE depth,
                       void *argument, UBaseType_t priority, TaskHandle_t *out);

#endif /* HOST_TASK_H */
