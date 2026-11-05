@echo off
setlocal
cd /d "%~dp0"
echo ==============================================
echo Building and Running RTSFR Module 1 Host Tests
echo ==============================================
gcc -Wall -Wextra -DRTSFR_HOST_TEST -I ..\..\firmware\RTSFR\Inc test_m1_detect.c ..\..\firmware\RTSFR\Src\rtsfr_detect.c -o test_m1_detect.exe
if %ERRORLEVEL% equ 0 (
    echo Build successful. Running tests...
    echo.
    test_m1_detect.exe
    set TEST_EXIT=%ERRORLEVEL%
) else (
    echo [ERROR] Build failed!
    set TEST_EXIT=1
)

if "%~1"=="--pause" pause
exit /b %TEST_EXIT%
