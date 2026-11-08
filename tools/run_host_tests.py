"""Compile real M5/M4 C against RTOS doubles; do not label these as target tests."""
from pathlib import Path
import argparse
import json
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='gcc')
    args = parser.parse_args()
    cc = shutil.which(args.cc)
    if not cc:
        parser.error('Host C compiler missing; no test pass claimed')
    if not str(ROOT).isascii():
        with tempfile.TemporaryDirectory(prefix='rtsfr_host_') as scratch:
            shadow = Path(scratch) / 'project'
            shutil.copytree(ROOT, shadow, ignore=shutil.ignore_patterns('.git', 'build', '__pycache__'))
            proc = subprocess.run([sys.executable, str(shadow / 'tools/run_host_tests.py'), '--cc', cc], cwd=shadow)
            if (shadow / 'build/host').exists():
                shutil.copytree(shadow / 'build/host', ROOT / 'build/host', dirs_exist_ok=True)
            sys.exit(proc.returncode)
    output = ROOT / 'build/host'
    output.mkdir(parents=True, exist_ok=True)
    inc = ['tests/host/stubs', 'firmware/RTSFR/Inc', 'firmware/App', 'firmware/tests/target']
    trace = ['firmware/RTSFR/Src/rtsfr_trace.c']
    adapter = ['firmware/RTSFR/Src/rtsfr_m5_adapter.c', 'firmware/RTSFR/Src/rtsfr_verify.c', 'firmware/RTSFR/Src/rtsfr_health.c']
    cases = [
        ('trace', 'tests/host/test_trace.c', trace, 1),
        ('verify', 'tests/host/test_verify.c', trace + ['firmware/RTSFR/Src/rtsfr_verify.c'], 1),
        ('health_gate', 'tests/host/test_health_gate.c', ['firmware/RTSFR/Src/rtsfr_health.c'], 1),
        ('recovery_contract', 'tests/host/test_recovery_contract.c', trace + adapter + ['firmware/RTSFR/Src/rtsfr_recovery.c', 'firmware/RTSFR/Src/rtsfr_safe_mode.c'], 1),
        ('faults_test', 'tests/host/test_faults.c', trace + adapter + ['firmware/App/fault_scenarios.c', 'firmware/tests/target/m5_test_runner.c'], 1),
        ('faults_release', 'tests/host/test_faults.c', trace + adapter + ['firmware/App/fault_scenarios.c', 'firmware/tests/target/m5_test_runner.c'], 0),
    ]
    results = []
    for name, test, sources, test_build in cases:
        exe = output / (name + '.exe')
        command = [cc, '-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic', '-g',
                   '-DRTSFR_M5_TEST_BUILD=' + str(test_build)]
        command += ['-I' + x for x in inc]
        command += [test, 'tests/host/stubs/mock_runtime.c', *sources, '-o', str(exe)]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(exe)], cwd=ROOT, check=True)
        results.append({'suite': name, 'status': 'HOST_PASS', 'hardware_status': 'NOT_RUN'})
    subprocess.run([sys.executable, '-m', 'unittest', 'discover', '-s', 'tests/host', '-p', 'test_tools.py', '-v'], cwd=ROOT, check=True)
    results.append({'suite': 'python_tools', 'status': 'HOST_PASS', 'hardware_status': 'NOT_RUN'})
    (output / 'test_results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    print(f'{len(results)} host suites passed. TC01-TC14 on board remain NOT_RUN.')

if __name__ == '__main__':
    main()
