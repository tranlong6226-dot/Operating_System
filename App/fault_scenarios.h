#ifndef FAULT_SCENARIOS_H
#define FAULT_SCENARIOS_H
#include "rtsfr_types.h"
typedef enum { M5_FAULT_STOP_PROGRESS = 1, M5_FAULT_BUSY_LOOP,
    M5_FAULT_SELF_REPORT, M5_FAULT_EXPECTED_WAIT, M5_FAULT_BUFFER_HELD,
    M5_FAULT_MUTEX_HELD } rtsfr_fault_scenario_t;
typedef enum { M5_POINT_WORK, M5_POINT_BUFFER_HELD, M5_POINT_MUTEX_HELD } rtsfr_fault_point_t;
/* Task context. Arm checks current generation through M1 adapter; release rejects. */
rtsfr_status_t rtsfr_fault_arm(uint8_t scenario, uint8_t task_id, uint16_t generation);
void rtsfr_fault_disarm(void);
/* M1 uses the reserved injection ID for correlation in a TEST build (zero otherwise). */
uint32_t rtsfr_fault_correlation(uint8_t task_id, uint16_t generation);
/* Called by the target at work/buffer/mutex checkpoints. May intentionally hang.
 * Return false means suppress progress. Expected-wait needs M1 wait contract
 * established by the target BEFORE calling; M5 never fabricates that contract. */
bool rtsfr_fault_checkpoint(uint8_t task_id, uint16_t generation, rtsfr_fault_point_t point);
/* Compatibility entry: requests a target test, logs missing integration explicitly. */
void rtsfr_test_inject(uint8_t test_id);
#endif
