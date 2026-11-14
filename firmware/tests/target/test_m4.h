#ifndef TEST_M4_H
#define TEST_M4_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    RTSFR_M4_TEST_NOT_RUN = 0,
    RTSFR_M4_TEST_RUNNING,
    RTSFR_M4_TEST_PASS,
    RTSFR_M4_TEST_FAIL
} rtsfr_m4_test_result_t;

extern volatile rtsfr_m4_test_result_t g_rtsfr_m4_test_result;
extern volatile unsigned long g_rtsfr_m4_test_progress_count;
extern volatile unsigned short g_rtsfr_m4_test_reason;

/* Runs the selected test and then blinks forever with the result pattern. */
void rtsfr_m4_target_test_run(void);

#ifdef __cplusplus
}
#endif

#endif /* TEST_M4_H */
