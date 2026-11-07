#include "rtsfr_trace.h"
#include "mock_runtime.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    rtsfr_trace_event_t in = {0}, out;
    unsigned int i;
    mock_reset(); rtsfr_trace_init();
    assert(!rtsfr_trace_pop(NULL)); assert(!rtsfr_trace_pop(&out));
    rtsfr_trace_event(NULL);
    for (i = 0U; i < RTSFR_TRACE_CAPACITY; ++i) {
        in.event_id = i + 1U; rtsfr_trace_event(&in);
    }
    rtsfr_trace_event_from_isr(&in); assert(rtsfr_trace_dropped() == 1U);
    for (i = 0U; i < RTSFR_TRACE_CAPACITY; ++i) {
        assert(rtsfr_trace_pop(&out)); assert(out.event_id == i + 1U);
    }
    assert(!rtsfr_trace_pop(&out));
    for (i = 0U; i < 1000U; ++i) {
        in.event_id = i; rtsfr_trace_event_from_isr(&in);
        assert(rtsfr_trace_pop(&out)); assert(out.event_id == i);
    }
    mock_enter(); rtsfr_m5_lifecycle((void *)(uintptr_t)9U, 1U); mock_exit();
    assert(rtsfr_trace_pop(&out)); assert(out.value == 9U && out.event == M5_TASK_CREATE);
    assert(mock_depth == 0U);
    puts("PASS trace: FIFO, overflow count, wrap, nested kernel hook, ISR entry");
    return 0;
}
