#ifndef M5_COMMAND_H
#define M5_COMMAND_H
#include "rtsfr_types.h"
void rtsfr_m5_command_feed(char byte);
void rtsfr_m5_service_init(void);
void rtsfr_m5_service_poll(void);
#endif
