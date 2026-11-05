#ifndef RTSFR_SAFE_MODE_H
#define RTSFR_SAFE_MODE_H

#include <stdbool.h>
#include <stdint.h>

#include "rtsfr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint32_t event_id;
    uint8_t task_id;
    uint16_t generation;
    uint16_t reason_code;
    TickType_t entered_tick;
} rtsfr_safe_mode_info_t;

typedef void (*rtsfr_safe_output_fn)(const rtsfr_safe_mode_info_t *info);

void rtsfr_safe_mode_init(rtsfr_safe_output_fn output_hook);
rtsfr_status_t rtsfr_safe_mode_enter(uint32_t event_id,
                                      uint8_t task_id,
                                      uint16_t generation,
                                      uint16_t reason_code);
bool rtsfr_safe_mode_is_active(void);
rtsfr_status_t rtsfr_safe_mode_get_info(rtsfr_safe_mode_info_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RTSFR_SAFE_MODE_H */
