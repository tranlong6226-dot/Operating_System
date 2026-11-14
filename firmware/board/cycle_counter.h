#ifndef CYCLE_COUNTER_H
#define CYCLE_COUNTER_H

#include <stdbool.h>
#include <stdint.h>

bool rtsfr_cycles_init(void);
bool rtsfr_cycles_ready(void);
uint32_t rtsfr_cycles_now(void);
uint32_t rtsfr_cycles_elapsed(uint32_t start, uint32_t end);

#endif /* CYCLE_COUNTER_H */
