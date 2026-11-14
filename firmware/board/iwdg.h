#ifndef RTSFR_BOARD_IWDG_H
#define RTSFR_BOARD_IWDG_H

#include "rtsfr_types.h"

void rtsfr_iwdg_capture_reset_cause(void);
uint32_t rtsfr_iwdg_reset_cause(void);
bool rtsfr_iwdg_was_reset(void);
bool rtsfr_iwdg_test12_passed(void);
rtsfr_status_t rtsfr_iwdg_start(void);
bool rtsfr_iwdg_poll(void);
bool rtsfr_iwdg_started(void);
rtsfr_status_t rtsfr_iwdg_board_reset_test(void);

#endif /* RTSFR_BOARD_IWDG_H */
