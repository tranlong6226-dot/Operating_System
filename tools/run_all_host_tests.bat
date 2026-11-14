@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0\.."

echo ==============================================================================
echo   RTSFR - RUN ALL HOST UNIT TESTS (MinGW GCC)
echo ==============================================================================

set M1_STATUS=FAIL
set M2_STATUS=FAIL
set M3_STATUS=FAIL
set OVERALL=0

echo.
echo [1/3] Building and running Module 1 (Fault Detection)...
echo ------------------------------------------------------------------------------
if exist tests\host\test_m1_detect.exe del /f /q tests\host\test_m1_detect.exe
gcc -Wall -Wextra -Ifirmware/RTSFR/Inc -Itests/host/stubs -Itests/host firmware/RTSFR/Src/rtsfr_detect.c tests/host/test_m1_detect.c -o tests/host/test_m1_detect.exe
if %ERRORLEVEL% equ 0 (
    tests\host\test_m1_detect.exe
    if !ERRORLEVEL! equ 0 (
        set M1_STATUS=PASS
    ) else (
        set OVERALL=1
    )
    if exist tests\host\test_m1_detect.exe del /f /q tests\host\test_m1_detect.exe
) else (
    echo [ERROR] Module 1 compilation failed!
    set OVERALL=1
)

echo.
echo [2/3] Building and running Module 2 (Policy ^& Classification)...
echo ------------------------------------------------------------------------------
if exist tests\host\test_m2_policy.exe del /f /q tests\host\test_m2_policy.exe
gcc -Wall -Wextra -Ifirmware/RTSFR/Inc -Itests/host/stubs -Itests/host firmware/RTSFR/Src/rtsfr_policy.c tests/host/test_m2_policy.c -o tests/host/test_m2_policy.exe
if %ERRORLEVEL% equ 0 (
    tests\host\test_m2_policy.exe
    if !ERRORLEVEL! equ 0 (
        set M2_STATUS=PASS
    ) else (
        set OVERALL=1
    )
    if exist tests\host\test_m2_policy.exe del /f /q tests\host\test_m2_policy.exe
) else (
    echo [ERROR] Module 2 compilation failed!
    set OVERALL=1
)

echo.
echo [3/3] Building and running Module 3 (Resource Tracking)...
echo ------------------------------------------------------------------------------
if exist tests\host\test_m3_resource.exe del /f /q tests\host\test_m3_resource.exe
gcc -Wall -Wextra -Ifirmware/RTSFR/Inc -Itests/host/stubs -Itests/host firmware/RTSFR/Src/rtsfr_resource.c tests/host/test_m3_resource.c -o tests/host/test_m3_resource.exe
if %ERRORLEVEL% equ 0 (
    tests\host\test_m3_resource.exe
    if !ERRORLEVEL! equ 0 (
        set M3_STATUS=PASS
    ) else (
        set OVERALL=1
    )
    if exist tests\host\test_m3_resource.exe del /f /q tests\host\test_m3_resource.exe
) else (
    echo [ERROR] Module 3 compilation failed!
    set OVERALL=1
)

echo.
echo ==============================================================================
echo   HOST UNIT TEST SUMMARY
echo ==============================================================================
echo   Module 1 (Fault Detection):       [!M1_STATUS!]
echo   Module 2 (Policy Evaluation):     [!M2_STATUS!]
echo   Module 3 (Resource Tracking):     [!M3_STATUS!]
echo ==============================================================================

if !OVERALL! equ 0 (
    echo   RESULT: ALL MODULE TESTS PASSED (100%%^)
    echo ==============================================================================
    exit /b 0
) else (
    echo   RESULT: ONE OR MORE TESTS FAILED!
    echo ==============================================================================
    exit /b 1
)
