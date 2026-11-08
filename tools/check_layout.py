"""Validate Keil/CubeMX paths after moving all embedded source into firmware/."""
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]

def main():
    errors = []
    firmware = ROOT / 'firmware'
    project_dir = firmware / 'MDK-ARM'
    expected = ['App', 'board', 'Core', 'Drivers', 'Middlewares', 'RTSFR', 'MDK-ARM', 'tests/target']
    for name in expected:
        if not (firmware / name).is_dir():
            errors.append('Missing firmware/' + name)
        if (ROOT / name).exists():
            errors.append('Old source location still exists: ' + name)
    for name in ['tests/host', 'tests/fixtures', 'tools', 'docs', 'results']:
        if not (ROOT / name).is_dir():
            errors.append('Missing repository folder: ' + name)
    tree = ET.parse(project_dir / 'Operating_System_Project.uvprojx')
    targets = tree.findall('./Targets/Target')
    active_files = 0
    excluded = []
    for target in targets:
        name = target.findtext('TargetName')
        for node in target.findall('./Groups/Group/Files/File'):
            text = node.findtext('FilePath')
            path = (project_dir / text.replace('\\', '/')).resolve()
            if node.findtext('./FileOption/CommonProperty/IncludeInBuild') == '0':
                excluded.append(name + ': ' + text)
                continue
            if not path.is_file() or not path.is_relative_to(firmware.resolve()):
                errors.append(name + ': invalid active source ' + text)
            active_files += 1
        for node in target.findall('.//VariousControls/IncludePath'):
            for text in (node.text or '').split(';'):
                if not text:
                    continue
                path = (project_dir / text.replace('\\', '/')).resolve()
                if not path.is_dir() or not path.is_relative_to(firmware.resolve()):
                    errors.append(name + ': invalid include ' + text)
        for field in ['OutputDirectory', 'ListingPath']:
            text = target.findtext('./TargetOption/TargetCommonOption/' + field) or ''
            path = (project_dir / text.replace('\\', '/')).resolve()
            if not text or not path.is_relative_to((ROOT / 'build').resolve()):
                errors.append(name + ': generated ' + field + ' is outside root build/')
    ioc = firmware / 'Operating_System_Project.ioc'
    if not ioc.is_file() or not (firmware / '.mxproject').is_file():
        errors.append('Missing relocated CubeMX configuration')
    else:
        settings = dict(line.split('=', 1) for line in ioc.read_text(encoding='utf-8').splitlines() if '=' in line)
        location = settings.get('ProjectManager.MainLocation', '')
        if not location or not (firmware / location).is_dir():
            errors.append('CubeMX MainLocation does not resolve from firmware/')
    if errors:
        raise SystemExit('\n'.join(errors))
    print(f'LAYOUT_PASS: {len(targets)} Keil targets, {active_files} active source references, include/output paths and CubeMX locations checked.')
    print(f'Explicitly excluded unavailable upstream test_m4.c references: {len(excluded)}; no fake source created.')
    print('CubeMX regeneration, Keil GUI and hardware tests: NOT_RUN.')

if __name__ == '__main__':
    main()
