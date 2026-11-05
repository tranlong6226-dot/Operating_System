@echo off
echo ==============================================
echo Building and Running RTSFR Module 1 Host Tests
echo ==============================================
gcc -Wall -Wextra -I ..\..\firmware\RTSFR\Inc test_m1_detect.c ..\..\firmware\RTSFR\Src\rtsfr_detect.c -o test_m1_detect.exe
if %ERRORLEVEL% equ 0 (
    echo Build successful. Running tests...
    echo.
    test_m1_detect.exe
) else (
    echo [ERROR] Build failed!
)
pause
