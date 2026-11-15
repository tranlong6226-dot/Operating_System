# tools/run_all_host_tests.ps1
# Runs all RTSFR host unit tests sequentially with colorized output

$ErrorActionPreference = "Continue"
$RepoRoot = Resolve-Path "$PSScriptRoot/.."
Set-Location $RepoRoot

Write-Host "==============================================================================" -ForegroundColor Cyan
Write-Host "  RTSFR - RUN ALL HOST UNIT TESTS (MinGW GCC)" -ForegroundColor Cyan
Write-Host "==============================================================================" -ForegroundColor Cyan

$tests = @(
    @{
        Name = "Module 1 (Fault Detection)"
        Src = "firmware/RTSFR/Src/rtsfr_detect.c"
        Test = "tests/host/test_m1_detect.c"
        Exe = "tests/host/test_m1_detect.exe"
    },
    @{
        Name = "Module 2 (Policy & Classification)"
        Src = "firmware/RTSFR/Src/rtsfr_policy.c"
        Test = "tests/host/test_m2_policy.c"
        Exe = "tests/host/test_m2_policy.exe"
    },
    @{
        Name = "Module 3 (Resource Tracking)"
        Src = "firmware/RTSFR/Src/rtsfr_resource.c"
        Test = "tests/host/test_m3_resource.c"
        Exe = "tests/host/test_m3_resource.exe"
    }
)

$results = [ordered]@{}
$overallSuccess = $true
$testIndex = 1

foreach ($t in $tests) {
    Write-Host "`n[$testIndex/3] Building and running $($t.Name)..." -ForegroundColor Yellow
    Write-Host "------------------------------------------------------------------------------" -ForegroundColor DarkGray
    
    Remove-Item -Force $t.Exe -ErrorAction SilentlyContinue
    $compileCmd = "gcc -Wall -Wextra -Ifirmware/RTSFR/Inc -Itests/host/stubs -Itests/host $($t.Src) $($t.Test) -o $($t.Exe)"
    Invoke-Expression $compileCmd
    
    if ($LASTEXITCODE -ne 0 -or !(Test-Path $t.Exe)) {
        Write-Host "[ERROR] Compilation failed for $($t.Name)!" -ForegroundColor Red
        $results[$t.Name] = "FAIL (Build)"
        $overallSuccess = $false
    } else {
        & "./$($t.Exe)"
        if ($LASTEXITCODE -eq 0) {
            $results[$t.Name] = "PASS"
        } else {
            Write-Host "[ERROR] Test execution failed for $($t.Name) (ExitCode: $LASTEXITCODE)!" -ForegroundColor Red
            $results[$t.Name] = "FAIL (Runtime)"
            $overallSuccess = $false
        }
        Remove-Item -Force $t.Exe -ErrorAction SilentlyContinue
    }
    $testIndex++
}

Write-Host "`n==============================================================================" -ForegroundColor Cyan
Write-Host "  HOST UNIT TEST SUMMARY" -ForegroundColor Cyan
Write-Host "==============================================================================" -ForegroundColor Cyan

foreach ($name in $results.Keys) {
    $res = $results[$name]
    $color = if ($res -eq "PASS") { "Green" } else { "Red" }
    Write-Host ("  {0,-35} : [{1}]" -f $name, $res) -ForegroundColor $color
}

Write-Host "==============================================================================" -ForegroundColor Cyan
if ($overallSuccess) {
    Write-Host "  RESULT: ALL MODULE TESTS PASSED (100%)" -ForegroundColor Green
    Write-Host "==============================================================================" -ForegroundColor Cyan
    exit 0
} else {
    Write-Host "  RESULT: ONE OR MORE TESTS FAILED!" -ForegroundColor Red
    Write-Host "==============================================================================" -ForegroundColor Cyan
    exit 1
}
