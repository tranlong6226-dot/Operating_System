/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications (Baseline FreeRTOS)
  ******************************************************************************
  */
/* USER CODE END Header */

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"

void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);
void vApplicationMallocFailedHook(void);

void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName)
{
   (void)xTask;
   (void)pcTaskName;
   taskDISABLE_INTERRUPTS();
   for (;;)
   {
   }
}

void vApplicationMallocFailedHook(void)
{
   taskDISABLE_INTERRUPTS();
   for (;;)
   {
   }
}
