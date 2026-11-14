#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#include "FreeRTOS.h"
#include "task.h"
#include "rtsfr_types.h"
#include "rtsfr_resource.h"

extern uint16_t rtsfr_resource_get_anomaly_count(void);

static int fake_tcb_1 = 100;
static int fake_tcb_2 = 200;
static int fake_mutex_A = 10;
static int fake_mutex_B = 20;
static int fake_mutex_C = 30;
static int fake_mutex_D = 40;
static int fake_mutex_E = 50;

uint8_t rtsfr_detect_task_id_from_handle(const void *task_handle)
{
    if (task_handle == &fake_tcb_1) return 1U;
    if (task_handle == &fake_tcb_2) return 2U;
    return RTSFR_TASK_ID_NONE;
}

void test_1_register_mutex(void)
{
    uint16_t id_A, id_B;
    rtsfr_status_t st;
    st = rtsfr_resource_register_mutex(&fake_mutex_A, &id_A);
    assert(st == RTSFR_OK);
    st = rtsfr_resource_register_mutex(&fake_mutex_B, &id_B);
    assert(st == RTSFR_OK);
    printf("[OK] Test 1: register mutex\n");
}

void test_2_take_give_snapshot(void)
{
    rtsfr_resource_summary_t sum;
    rtsfr_resource_on_new_generation(1U, 1U);
    rtsfr_resource_hook_mutex_taken(&fake_mutex_A, &fake_tcb_1);
    assert(rtsfr_resource_snapshot(1U, 1U, &sum) == RTSFR_OK);
    assert(sum.mutex_held_count == 1U);
    assert(sum.can_reclaim_all == false);

    rtsfr_resource_hook_mutex_given(&fake_mutex_A, &fake_tcb_1);
    assert(rtsfr_resource_snapshot(1U, 1U, &sum) == RTSFR_OK);
    assert(sum.mutex_held_count == 0U);
    assert(sum.can_reclaim_all == true);
    printf("[OK] Test 2: take give snapshot\n");
}

void test_3_anomaly_detection(void)
{
    uint16_t old_anomalies = rtsfr_resource_get_anomaly_count();
    rtsfr_resource_hook_mutex_given(&fake_mutex_A, NULL);
    assert(rtsfr_resource_get_anomaly_count() == (uint16_t)(old_anomalies + 1U));
    printf("[OK] Test 3: anomaly detection\n");
}

void test_4_register_edge_cases(void)
{
    uint16_t id, idC, idD, idE;
    rtsfr_status_t st;
    st = rtsfr_resource_register_mutex(&fake_mutex_A, &id); /* duplicate registration */
    assert(st == RTSFR_OK && id == 0U);
    st = rtsfr_resource_register_mutex(NULL, &id);
    assert(st == RTSFR_ERR_INVALID);
    st = rtsfr_resource_register_mutex(&fake_mutex_A, NULL);
    assert(st == RTSFR_ERR_INVALID);
    st = rtsfr_resource_register_mutex(&fake_mutex_C, &idC);
    assert(st == RTSFR_OK);
    st = rtsfr_resource_register_mutex(&fake_mutex_D, &idD);
    assert(st == RTSFR_OK);
    st = rtsfr_resource_register_mutex(&fake_mutex_E, &idE); /* capacity reached (4 mutexes) */
    assert(st == RTSFR_ERR_NO_RESOURCE);
    printf("[OK] Test 4: register edge cases\n");
}

void test_5_unregister_and_cleanup_refuse(void)
{
    rtsfr_resource_summary_t s;
    rtsfr_status_t st;
    rtsfr_resource_hook_mutex_taken(&fake_mutex_B, &fake_tcb_1);
    assert(rtsfr_resource_unregister_mutex(&fake_mutex_B) == RTSFR_ERR_BUSY); /* currently held */
    st = rtsfr_resource_cleanup_safe(1U, 1U);
    assert(st == RTSFR_ERR_UNSAFE); /* cannot reclaim while held */
    assert(rtsfr_resource_snapshot(1U, 1U, &s) == RTSFR_OK);
    assert(s.mutex_held_count == 1U); /* does NOT force unlock */

    rtsfr_resource_hook_mutex_given(&fake_mutex_B, &fake_tcb_1);
    assert(rtsfr_resource_unregister_mutex(&fake_mutex_B) == RTSFR_OK);
    assert(rtsfr_resource_unregister_mutex(&fake_mutex_B) == RTSFR_ERR_NOT_FOUND); /* second unregister */
    printf("[OK] Test 5: unregister and cleanup refuse\n");
}

void test_6_stale_and_invalid(void)
{
    rtsfr_resource_summary_t s;
    assert(rtsfr_resource_snapshot(1U, 99U, &s) == RTSFR_ERR_STALE);
    assert(rtsfr_resource_cleanup_safe(1U, 99U) == RTSFR_ERR_STALE);
    assert(rtsfr_resource_snapshot(RTSFR_TASK_ID_NONE, 1U, &s) == RTSFR_ERR_INVALID);
    assert(rtsfr_resource_snapshot(0U, 1U, &s) == RTSFR_ERR_INVALID);
    assert(rtsfr_resource_snapshot((uint8_t)(configRTSFR_MAX_TRACKED_TASKS + 1U), 1U, &s) == RTSFR_ERR_INVALID);
    assert(rtsfr_resource_snapshot(1U, 1U, NULL) == RTSFR_ERR_INVALID);
    printf("[OK] Test 6: stale generation and invalid task ID\n");
}

void test_7_release_by_wrong_task_is_unknown(void)
{
    rtsfr_resource_summary_t s1, s2;
    rtsfr_resource_on_new_generation(2U, 1U);
    rtsfr_resource_hook_mutex_taken(&fake_mutex_A, &fake_tcb_1); /* task 1 holds A */
    rtsfr_resource_hook_mutex_given(&fake_mutex_A, &fake_tcb_2); /* task 2 gives A: anomaly */
    assert(rtsfr_resource_snapshot(2U, 1U, &s2) == RTSFR_OK);
    assert(s2.unknown_resource_count == 1U && s2.can_reclaim_all == false);
    assert(rtsfr_resource_snapshot(1U, 1U, &s1) == RTSFR_OK);
    assert(s1.mutex_held_count == 1U); /* task 1 is still owner */
    rtsfr_resource_hook_mutex_given(&fake_mutex_A, &fake_tcb_1); /* clean up */
    printf("[OK] Test 7: release by wrong task is unknown\n");
}

void test_8_buffer_pool(void)
{
    rtsfr_resource_summary_t s;
    uint8_t idx[8], extra, i;
    rtsfr_resource_on_new_generation(3U, 1U);
    for (i = 0U; i < 8U; i++)
    {
        assert(rtsfr_buffer_acquire(3U, &idx[i]) == RTSFR_OK);
    }
    assert(rtsfr_buffer_acquire(3U, &extra) == RTSFR_ERR_NO_RESOURCE); /* pool exhausted */
    assert(rtsfr_resource_snapshot(3U, 1U, &s) == RTSFR_OK);
    assert(s.managed_buffers_owned == 8U && s.can_reclaim_all == true);

    assert(rtsfr_buffer_release(2U, idx[0]) == RTSFR_ERR_INVALID); /* wrong owner */
    assert(rtsfr_buffer_release(3U, idx[0]) == RTSFR_OK);
    assert(rtsfr_buffer_release(3U, idx[0]) == RTSFR_ERR_INVALID); /* double release */
    assert(rtsfr_resource_cleanup_safe(3U, 1U) == RTSFR_OK);

    assert(rtsfr_resource_snapshot(3U, 1U, &s) == RTSFR_OK);
    assert(s.managed_buffers_owned == 0U);
    assert(rtsfr_buffer_acquire(3U, &extra) == RTSFR_OK); /* reusable */
    printf("[OK] Test 8: buffer pool acquire/release/cleanup\n");
}

void test_9_unknown_persists_after_new_generation(void)
{
    rtsfr_resource_summary_t s;
    rtsfr_resource_on_new_generation(2U, 2U); /* task 2 recreated */
    assert(rtsfr_resource_snapshot(2U, 2U, &s) == RTSFR_OK);
    assert(s.unknown_resource_count == 0U && s.can_reclaim_all == true); /* clean new generation */
    printf("[OK] Test 9: new_generation clears unknown of old instance\n");
}

int main(void)
{
    printf("=========================================\n");
    printf("  RTSFR Module 3 - Host Unit Test Runner \n");
    printf("=========================================\n");

    rtsfr_resource_init();

    test_1_register_mutex();
    test_2_take_give_snapshot();
    test_3_anomaly_detection();
    test_4_register_edge_cases();
    test_5_unregister_and_cleanup_refuse();
    test_6_stale_and_invalid();
    test_7_release_by_wrong_task_is_unknown();
    test_8_buffer_pool();
    test_9_unknown_persists_after_new_generation();

    printf("=========================================\n");
    printf("Summary: 9 / 9 tests passed (100%%)\n");
    printf("=========================================\n");
    return 0;
}
