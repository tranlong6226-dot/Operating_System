#include "cycle_counter.h"
#include "stm32f1xx.h"
static bool ready;
bool rtsfr_cycles_init(void)
{
    uint32_t before;
    unsigned int i;
    if ((DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk) != 0U) { ready = false; return false; }
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U; DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    before = DWT->CYCCNT;
    for (i = 0U; i < 8U; ++i) __NOP();
    ready = (DWT->CYCCNT != before);
    return ready;
}
bool rtsfr_cycles_ready(void) { return ready; }
uint32_t rtsfr_cycles_now(void) { return DWT->CYCCNT; }
uint32_t rtsfr_cycles_elapsed(uint32_t start, uint32_t end) { return end - start; }
