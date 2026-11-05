/* Integration test: M1 (that, Viet Anh) + M3 (that) cung chay tren PC */
#include <stdio.h>
#include <assert.h>
#include "rtsfr_detect.h"
#include "rtsfr_resource.h"

TickType_t g_mock_tick = 0;                         /* M1 can bien nay o che do host */
static int h0, h1, h2, h1_new, h_unknown, mtxA;     /* handle gia */

static void ev(rtsfr_fault_event_t *e){ assert(rtsfr_detect_pop_event(e)); }

int main(void)
{
    rtsfr_fault_event_t e; rtsfr_resource_summary_t s; uint16_t rid;
    rtsfr_task_config_t c0 = {0,"t0",10,1}, c1 = {1,"t1",10,1}, c2 = {2,"t2",10,1};

    rtsfr_detect_init();
    assert(rtsfr_register_task(&c0,&h0)==RTSFR_OK);
    assert(rtsfr_register_task(&c1,&h1)==RTSFR_OK);
    assert(rtsfr_register_task(&c2,&h2)==RTSFR_OK);
    assert(rtsfr_resource_register_mutex(&mtxA,&rid)==RTSFR_OK);

    /* 1. lookup cua M1 hoat dong, M3 dung duoc */
    assert(rtsfr_detect_task_id_from_handle(&h0)==0);
    assert(rtsfr_detect_task_id_from_handle(&h2)==2);
    assert(rtsfr_detect_task_id_from_handle(&h_unknown)==RTSFR_TASK_ID_NONE);
    assert(rtsfr_detect_task_id_from_handle(NULL)==RTSFR_TASK_ID_NONE);

    /* 2. generation ban dau do M1 day sang M3 (khong can goi tay) */
    assert(rtsfr_resource_snapshot(0,1,&s)==RTSFR_OK);

    /* 3. task 0 giu mutex, task 1 giu buffer, task 2 khong giu gi */
    rtsfr_resource_hook_mutex_taken(&mtxA,&h0);
    { uint8_t b; assert(rtsfr_buffer_acquire(1,&b)==RTSFR_OK); }
    rtsfr_resource_hook_mutex_taken(&mtxA,&h_unknown);       /* task ngoai RTSFR: bo qua */
    rtsfr_resource_snapshot(0,1,&s); assert(s.mutex_held_count==1 && !s.can_reclaim_all);

    /* 4. ca 3 task ngung bao tien do -> M1 phat 3 su kien */
    g_mock_tick = 100; rtsfr_detect_scan(g_mock_tick);
    ev(&e); assert(e.task_id==0 && e.generation==1 && e.fault_type==RTSFR_FAULT_NO_PROGRESS);
    assert(rtsfr_resource_snapshot(e.task_id,e.generation,&s)==RTSFR_OK && !s.can_reclaim_all);
    assert(rtsfr_resource_cleanup_safe(e.task_id,e.generation)==RTSFR_ERR_UNSAFE);   /* SAFE_MODE */
    rtsfr_resource_snapshot(0,1,&s); assert(s.mutex_held_count==1);                   /* khong tu unlock */

    ev(&e); assert(e.task_id==1);
    assert(rtsfr_resource_snapshot(1,e.generation,&s)==RTSFR_OK && s.can_reclaim_all && s.managed_buffers_owned==1);
    assert(rtsfr_resource_cleanup_safe(1,e.generation)==RTSFR_OK);                    /* thu hoi buffer */
    rtsfr_resource_snapshot(1,1,&s); assert(s.managed_buffers_owned==0);

    /* 5. M4 restart task 1: generation 2, handle moi */
    rtsfr_detect_on_new_generation(1,&h1_new,2);
    assert(rtsfr_resource_snapshot(1,1,&s)==RTSFR_ERR_STALE);       /* su kien/gen cu bi tu choi */
    assert(rtsfr_resource_snapshot(1,2,&s)==RTSFR_OK);
    assert(rtsfr_detect_task_id_from_handle(&h1_new)==1);
    assert(rtsfr_detect_task_id_from_handle(&h1)==RTSFR_TASK_ID_NONE);   /* handle cu khong con khop */

    /* 6. task 2 (id lon nhat) cung duoc theo doi dung, owner khong lan voi task 0 */
    rtsfr_resource_snapshot(2,1,&s); assert(s.mutex_held_count==0 && s.can_reclaim_all);
    rtsfr_resource_snapshot(0,1,&s); assert(s.mutex_held_count==1);

    /* 7. release dung chu */
    rtsfr_resource_hook_mutex_given(&mtxA,&h0);
    rtsfr_resource_snapshot(0,1,&s); assert(s.mutex_held_count==0 && s.can_reclaim_all);

    printf("INTEGRATION M1+M3: ALL PASSED\n");
    return 0;
}
