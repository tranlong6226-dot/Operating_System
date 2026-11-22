/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file (Dual-mode Hardware Test).
  *                   Cho phép chuyển đổi giữa có RTSFR (1) và không RTSFR (0)
  *                   bằng duy nhất một biến #define USE_RTSFR.
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

/* ==============================================================================
 * CẤU HÌNH BẬT / TẮT HỆ THỐNG RTSFR ĐỐI CHỨNG
 * ==============================================================================
 * 1: BẬT RTSFR (Tự phục hồi, mã hóa thế hệ Gen ra nhịp chớp LED, Safe Mode)
 * 0: TẮT RTSFR (Vanilla FreeRTOS thuần, không có giám sát, không phục hồi)
 * ============================================================================== */
#define USE_RTSFR 0

void Error_Handler(void);

/* Pin Definitions */
#define STATUS_LED_Pin GPIO_PIN_13
#define STATUS_LED_GPIO_Port GPIOC

#define SENSOR_LED_Pin GPIO_PIN_0
#define SENSOR_LED_GPIO_Port GPIOB

#define COMM_LED_Pin GPIO_PIN_1
#define COMM_LED_GPIO_Port GPIOB

/* Fault injection jumper test pin: PA1 (Pull-down internal) */
#define FAULT_INJECT_Pin GPIO_PIN_1
#define FAULT_INJECT_GPIO_Port GPIOA

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
