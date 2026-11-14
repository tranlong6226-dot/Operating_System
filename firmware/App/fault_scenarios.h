#ifndef FAULT_SCENARIOS_H
#define FAULT_SCENARIOS_H

#include "rtsfr_types.h"

typedef enum
{
    M5_FAULT_STOP_PROGRESS = 1,
    M5_FAULT_BUSY_LOOP,
    M5_FAULT_SELF_REPORT,
    M5_FAULT_EXPECTED_WAIT,
    M5_FAULT_BUFFER_HELD,
    M5_FAULT_MUTEX_HELD,
    M5_FAULT_SELF_REPORT_STALL
} rtsfr_fault_scenario_t;

typedef enum
{
    M5_POINT_WORK = 0,
    M5_POINT_BUFFER_HELD,
    M5_POINT_MUTEX_HELD
} rtsfr_fault_point_t;

rtsfr_status_t rtsfr_fault_arm(uint8_t scenario,
                               uint8_t task_id,
                               uint16_t generation);
void rtsfr_fault_disarm(void);
uint32_t rtsfr_fault_correlation(uint8_t task_id, uint16_t generation);
bool rtsfr_fault_checkpoint(uint8_t task_id,
                            uint16_t generation,
                            rtsfr_fault_point_t point);
void rtsfr_test_inject(uint8_t test_id);

#endif /* FAULT_SCENARIOS_H */
