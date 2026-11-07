#ifndef RTSFR_BOARD_IWDG_H
#define RTSFR_BOARD_IWDG_H
#include "rtsfr_types.h"
/* Capture once before flags are cleared, typically before osKernelInitialize. */
void rtsfr_iwdg_capture_reset_cause(void);
uint32_t rtsfr_iwdg_reset_cause(void);
bool rtsfr_iwdg_was_reset(void);
/* Only this task becomes the refresh owner; health and confirmed config required. */
rtsfr_status_t rtsfr_iwdg_start(void);
bool rtsfr_iwdg_poll(void);
bool rtsfr_iwdg_started(void);
/* TEST only: starts hardware then stops all IRQs; never a simulated TC12 pass. */
rtsfr_status_t rtsfr_iwdg_board_reset_test(void);
#endif
