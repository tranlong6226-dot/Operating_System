#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#define configRTSFR_ENABLE 1
#define configRTSFR_MAX_TRACKED_MUTEXES 4
#define configRTSFR_MAX_MANAGED_BUFFERS 8
#define taskENTER_CRITICAL()
#define taskEXIT_CRITICAL()
#include "rtsfr_resource.h"
extern uint16_t rtsfr_resource_get_anomaly_count(void);
int fake_tcb_1 = 100; int fake_tcb_2 = 200; int fake_mutex_A = 10; int fake_mutex_B = 20; int fake_mutex_C = 30; int fake_mutex_D = 40; int fake_mutex_E = 50;
uint8_t rtsfr_detect_task_id_from_handle(const void *task_handle) {
    if (task_handle == &fake_tcb_1) return 0;
    if (task_handle == &fake_tcb_2) return 1;
    return RTSFR_TASK_ID_NONE;
}
void test_1_register_mutex(void) {
    uint16_t id_A, id_B; rtsfr_status_t st;
    st = rtsfr_resource_register_mutex(&fake_mutex_A, &id_A); assert(st == RTSFR_OK);
    st = rtsfr_resource_register_mutex(&fake_mutex_B, &id_B); assert(st == RTSFR_OK);
    printf("[OK] Test 1\n");
}
void test_2_take_give_snapshot(void) {
    rtsfr_resource_summary_t sum;
    rtsfr_resource_on_new_generation(0, 1);
    rtsfr_resource_hook_mutex_taken(&fake_mutex_A, &fake_tcb_1);
    rtsfr_resource_snapshot(0, 1, &sum);
    assert(sum.mutex_held_count == 1); assert(sum.can_reclaim_all == false);
    rtsfr_resource_hook_mutex_given(&fake_mutex_A, &fake_tcb_1);
    rtsfr_resource_snapshot(0, 1, &sum);
    assert(sum.mutex_held_count == 0); assert(sum.can_reclaim_all == true);
    printf("[OK] Test 2\n");
}
void test_3_anomaly_detection(void) {
    uint16_t old_anomalies = rtsfr_resource_get_anomaly_count();
    rtsfr_resource_hook_mutex_given(&fake_mutex_A, NULL);
    assert(rtsfr_resource_get_anomaly_count() == old_anomalies + 1);
    printf("[OK] Test 3\n");
}

void test_4_register_edge_cases(void) {
    uint16_t id, idC, idD, idE; rtsfr_status_t st;
    st = rtsfr_resource_register_mutex(&fake_mutex_A, &id);          /* dang ky trung */
    assert(st == RTSFR_OK && id == 0);
    st = rtsfr_resource_register_mutex(NULL, &id);
    assert(st == RTSFR_ERR_INVALID);
    st = rtsfr_resource_register_mutex(&fake_mutex_A, NULL);
    assert(st == RTSFR_ERR_INVALID);
    st = rtsfr_resource_register_mutex(&fake_mutex_C, &idC); assert(st == RTSFR_OK);
    st = rtsfr_resource_register_mutex(&fake_mutex_D, &idD); assert(st == RTSFR_OK);
    st = rtsfr_resource_register_mutex(&fake_mutex_E, &idE);         /* bang day (4 muc) */
    assert(st == RTSFR_ERR_NO_RESOURCE);
    printf("[OK] Test 4: dang ky trung / NULL / bang day.\n");
}
void test_5_unregister_and_cleanup_refuse(void) {
    rtsfr_resource_summary_t s; rtsfr_status_t st;
    rtsfr_resource_hook_mutex_taken(&fake_mutex_B, &fake_tcb_1);
    assert(rtsfr_resource_unregister_mutex(&fake_mutex_B) == RTSFR_ERR_BUSY);   /* dang co chu */
    st = rtsfr_resource_cleanup_safe(0, 1);
    assert(st == RTSFR_ERR_UNSAFE);                                              /* tu choi thu hoi */
    rtsfr_resource_snapshot(0, 1, &s);
    assert(s.mutex_held_count == 1);                                             /* KHONG tu unlock */
    rtsfr_resource_hook_mutex_given(&fake_mutex_B, &fake_tcb_1);
    assert(rtsfr_resource_unregister_mutex(&fake_mutex_B) == RTSFR_OK);
    assert(rtsfr_resource_unregister_mutex(&fake_mutex_B) == RTSFR_ERR_INVALID); /* go lan 2 */
    printf("[OK] Test 5: BUSY, cleanup bi tu choi, khong tu unlock.\n");
}
void test_6_stale_and_invalid(void) {
    rtsfr_resource_summary_t s;
    assert(rtsfr_resource_snapshot(0, 99, &s) == RTSFR_ERR_STALE);
    assert(rtsfr_resource_cleanup_safe(0, 99) == RTSFR_ERR_STALE);
    assert(rtsfr_resource_snapshot(RTSFR_TASK_ID_NONE, 1, &s) == RTSFR_ERR_INVALID);
    assert(rtsfr_resource_snapshot(configRTSFR_MAX_TRACKED_TASKS, 1, &s) == RTSFR_ERR_INVALID);
    assert(rtsfr_resource_snapshot(0, 1, NULL) == RTSFR_ERR_INVALID);
    printf("[OK] Test 6: generation cu va task_id sai.\n");
}
void test_7_release_by_wrong_task_is_unknown(void) {
    rtsfr_resource_summary_t s1, s2;
    rtsfr_resource_on_new_generation(1, 1);
    rtsfr_resource_hook_mutex_taken(&fake_mutex_A, &fake_tcb_1);        /* task1 giu A */
    rtsfr_resource_hook_mutex_given(&fake_mutex_A, &fake_tcb_2);        /* task2 "nha" A: bat thuong */
    rtsfr_resource_snapshot(1, 1, &s2);
    assert(s2.unknown_resource_count == 1 && s2.can_reclaim_all == false);
    rtsfr_resource_snapshot(0, 1, &s1);
    assert(s1.mutex_held_count == 1);                                   /* task1 van la chu */
    rtsfr_resource_hook_mutex_given(&fake_mutex_A, &fake_tcb_1);        /* don dep */
    printf("[OK] Test 7: release sai chu -> UNKNOWN, owner that khong bi doi.\n");
}
void test_8_buffer_pool(void) {
    rtsfr_resource_summary_t s; uint8_t idx[8], extra, i;
    rtsfr_resource_on_new_generation(2, 1);
    for (i = 0; i < 8; i++) assert(rtsfr_buffer_acquire(2, &idx[i]) == RTSFR_OK);
    assert(rtsfr_buffer_acquire(2, &extra) == RTSFR_ERR_NO_RESOURCE);   /* het buffer */
    rtsfr_resource_snapshot(2, 1, &s);
    assert(s.managed_buffers_owned == 8 && s.can_reclaim_all == true);
    assert(rtsfr_buffer_release(1, idx[0]) == RTSFR_ERR_INVALID);       /* sai chu */
    assert(rtsfr_buffer_release(2, idx[0]) == RTSFR_OK);
    assert(rtsfr_buffer_release(2, idx[0]) == RTSFR_ERR_INVALID);       /* double release */
    assert(rtsfr_resource_cleanup_safe(2, 1) == RTSFR_OK);
    rtsfr_resource_snapshot(2, 1, &s);
    assert(s.managed_buffers_owned == 0);
    assert(rtsfr_buffer_acquire(2, &extra) == RTSFR_OK);                /* dung lai duoc */
    printf("[OK] Test 8: buffer pool acquire/release/cleanup.\n");
}
void test_9_unknown_persists_after_new_generation(void) {
    rtsfr_resource_summary_t s;
    rtsfr_resource_on_new_generation(1, 2);                              /* task 2 duoc tao lai */
    rtsfr_resource_snapshot(1, 2, &s);
    assert(s.unknown_resource_count == 0 && s.can_reclaim_all == true);   /* instance moi: bat thuong cu bi xoa */
    printf("[OK] Test 9: new_generation xoa unknown cua instance cu.\n");
}
int main(void) {
    printf("--- STARTING HOST UNIT TESTS ---\n");
    test_1_register_mutex(); test_2_take_give_snapshot(); test_3_anomaly_detection();
    test_4_register_edge_cases(); test_5_unregister_and_cleanup_refuse(); test_6_stale_and_invalid();
    test_7_release_by_wrong_task_is_unknown(); test_8_buffer_pool(); test_9_unknown_persists_after_new_generation();
    printf("--- ALL TESTS PASSED ---\n"); return 0;
}
