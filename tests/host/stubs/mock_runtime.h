#ifndef MOCK_RUNTIME_H
#define MOCK_RUNTIME_H
#include "task.h"
extern TickType_t mock_tick;
extern TaskHandle_t mock_current;
extern unsigned int mock_depth, mock_creates, mock_deletes, mock_suspends, mock_resumes;
extern BaseType_t mock_create_result;
void mock_reset(void);
#endif
