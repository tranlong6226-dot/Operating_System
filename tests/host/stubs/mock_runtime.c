#include "mock_runtime.h"
#include <assert.h>
TickType_t mock_tick;
TaskHandle_t mock_current;
unsigned int mock_depth, mock_creates, mock_deletes, mock_suspends, mock_resumes;
BaseType_t mock_create_result;
void mock_reset(void)
{
    mock_tick = mock_depth = mock_creates = mock_deletes = mock_suspends = mock_resumes = 0U;
    mock_current = (void *)(uintptr_t)1U; mock_create_result = pdPASS;
}
void mock_enter(void) { ++mock_depth; }
void mock_exit(void) { assert(mock_depth != 0U); --mock_depth; }
TickType_t xTaskGetTickCount(void) { return mock_tick; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return mock_current; }
void vTaskDelay(TickType_t ticks) { mock_tick += ticks; }
void vTaskSuspend(TaskHandle_t handle) { assert(handle != NULL); ++mock_suspends; }
void vTaskResume(TaskHandle_t handle) { assert(handle != NULL); ++mock_resumes; }
void vTaskDelete(TaskHandle_t handle) { assert(handle != NULL); ++mock_deletes; }
BaseType_t xTaskCreate(TaskFunction_t entry, const char *name, configSTACK_DEPTH_TYPE depth,
                       void *argument, UBaseType_t priority, TaskHandle_t *out)
{
    (void)entry; (void)name; (void)depth; (void)argument; (void)priority;
    ++mock_creates;
    if (mock_create_result == pdPASS) *out = (void *)(uintptr_t)(100U + mock_creates);
    return mock_create_result;
}
