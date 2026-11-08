"""Build the checked-in Keil target using local Arm Compiler CLI; never flash."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import shutil
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=['M5_DEBUG_TEST', 'M5_RELEASE', 'M5_BENCHMARK'], default='M5_DEBUG_TEST')
    parser.add_argument('--compiler-dir', type=Path, default=Path(os.environ.get('LOCALAPPDATA', '')) / 'Keil_v5/ARM/ARMCLANG/bin')
    args = parser.parse_args()
    compiler = args.compiler_dir
    for tool in ['armclang.exe', 'armasm.exe', 'armlink.exe', 'fromelf.exe']:
        if not (compiler / tool).is_file():
            parser.error(f'Missing {tool}; pass --compiler-dir. No build result claimed.')
    if not str(ROOT).isascii():
        # armasm/armlink use legacy ANSI paths, including their current directory.
        # An isolated ASCII scratch copy avoids changing the user's checkout.
        with tempfile.TemporaryDirectory(prefix='rtsfr_m5_') as scratch:
            shadow = Path(scratch) / 'project'
            shutil.copytree(ROOT, shadow, ignore=shutil.ignore_patterns('.git', 'build', '__pycache__'))
            proc = subprocess.run([sys.executable, str(shadow / 'tools/build_firmware.py'),
                '--profile', args.profile, '--compiler-dir', str(compiler)], cwd=shadow)
            artifacts = shadow / 'build' / args.profile
            if artifacts.exists():
                shutil.copytree(artifacts, ROOT / 'build' / args.profile, dirs_exist_ok=True)
            sys.exit(proc.returncode)
    project_dir = ROOT / 'firmware/MDK-ARM'
    tree = ET.parse(project_dir / 'Operating_System_Project.uvprojx')
    target = next(t for t in tree.findall('./Targets/Target') if t.findtext('TargetName') == args.profile)
    controls = target.find('./TargetOption/TargetArmAds/Cads/VariousControls')
    includes = [(project_dir / x.replace('\\', '/')).resolve() for x in controls.findtext('IncludePath').split(';') if x]
    defines = [x.strip() for x in controls.findtext('Define').split(',') if x.strip()]
    output = ROOT / 'build' / args.profile
    output.mkdir(parents=True, exist_ok=True)
    log = []
    def run(cmd):
        # Legacy armasm cannot open Unicode absolute paths. Relative project paths
        # keep this build usable in a Vietnamese-named workspace as well.
        def argument(value):
            if isinstance(value, Path):
                try:
                    return str(value.relative_to(ROOT))
                except ValueError:
                    pass
            return str(value)
        proc = subprocess.run([argument(x) for x in cmd], cwd=ROOT, capture_output=True, text=True, encoding='utf-8', errors='replace')
        log.append(' '.join(str(x) for x in cmd) + '\n' + proc.stdout + proc.stderr)
        if proc.returncode:
            (output / 'build.log').write_text('\n'.join(log), encoding='utf-8')
            raise RuntimeError(f'{Path(str(cmd[0])).name} exit {proc.returncode}:\n{proc.stdout}{proc.stderr}')
        return proc.stdout + proc.stderr
    version = run([compiler / 'armclang.exe', '--version'])
    objects = []
    for node in target.findall('./Groups/Group/Files/File'):
        if node.findtext('./FileOption/CommonProperty/IncludeInBuild') == '0':
            continue
        source = (project_dir / node.findtext('FilePath').replace('\\', '/')).resolve()
        if source.suffix.lower() not in ['.c', '.s']:
            continue
        if not source.is_file():
            raise RuntimeError(f'Missing source: {source}; no stub generated')
        obj = output / (str(len(objects)) + '_' + source.stem + '.o')
        if source.suffix.lower() == '.s':
            run([compiler / 'armasm.exe', '--cpu', 'Cortex-M3', '--apcs=interwork', '-g', '-o', obj, source])
        else:
            cmd = [compiler / 'armclang.exe', '--target=arm-arm-none-eabi', '-mcpu=cortex-m3', '-mthumb', '-mfloat-abi=soft',
                   '-std=c99', '-Oz', '-g', '-ffunction-sections', '-fdata-sections', '-fshort-enums', '-fshort-wchar',
                   '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-missing-field-initializers']
            cmd += ['-D' + x for x in defines] + ['-I' + str(x.relative_to(ROOT)) for x in includes]
            run(cmd + ['-c', source, '-o', obj])
        objects.append(obj)
    scatter = output / 'firmware.sct'
    scatter.write_text('LR_IROM1 0x08000000 0x10000 {\n ER_IROM1 0x08000000 0x10000 { *.o (RESET, +First) *(InRoot$$Sections) .ANY (+RO) }\n RW_IRAM1 0x20000000 0x5000 { .ANY (+RW +ZI) }\n}\n', encoding='ascii')
    axf = output / 'firmware.axf'
    report = run([compiler / 'armlink.exe', '--cpu=Cortex-M3', '--strict', '--scatter', scatter, '--map', '--info=sizes',
                  '--list', output / 'firmware.map', '--output', axf, *objects])
    run([compiler / 'fromelf.exe', '--i32combined', '--output', output / 'firmware.hex', axf])
    (output / 'build.log').write_text('\n'.join(log), encoding='utf-8')
    map_text = (output / 'firmware.map').read_text(encoding='utf-8', errors='replace')
    sizes = {}
    for key, label in [('flash_bytes', r'Total\s+ROM\s+Size'), ('ram_bytes', r'Total\s+RW\s+Size')]:
        match = re.search(label + r'\s*\([^\r\n]*?\)\s*(\d+)', map_text)
        sizes[key] = int(match[1]) if match else None
    summary = {'profile': args.profile, 'status': 'BUILD_PASS', 'hardware_status': 'NOT_RUN',
               'compiler': version.strip(), 'optimization': '-Oz', 'sizes': sizes,
               'note': 'CLI linker/build flags are documented; do not compare to different Keil settings as equivalent measurements.'}
    (output / 'build_summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf-8')
    print(args.profile + ': BUILD_PASS; hardware NOT_RUN')
    print(json.dumps(sizes))

if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, StopIteration) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
