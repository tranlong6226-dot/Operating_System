#ifndef M5_TEST_RUNNER_H
#define M5_TEST_RUNNER_H
#include "rtsfr_types.h"
rtsfr_status_t rtsfr_m5_test_start(uint8_t test_id);
/* Only a real test implementation calls finish after checking all expectations. */
rtsfr_status_t rtsfr_m5_test_finish(uint8_t test_id, bool passed, uint16_t reason);
void rtsfr_m5_test_poll(void);
uint8_t rtsfr_m5_test_active(void);
#endif
