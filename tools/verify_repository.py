"""Check the hardware target, source dependencies and public repository hygiene."""
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / 'MDK-ARM/PF-MCS-freeRTOS.uvprojx'
IGNORED = {'.git', 'build', '__pycache__'}


def main():
    failures = []
    required = ('README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md', '.gitignore',
                '.gitattributes', '.editorconfig', 'docs/HARDWARE.md',
                'docs/VALIDATION.md', 'docs/GITHUB_UPLOAD.md',
                '.github/workflows/source-check.yml')
    for name in required:
        if not (ROOT / name).is_file():
            failures.append(f'Missing {name}')
    targets = list((ROOT / 'MDK-ARM').glob('*.uvprojx'))
    if targets != [PROJECT]:
        failures.append('Expected exactly one hardware project')
    tree = ET.parse(PROJECT)
    if tree.findtext('.//TargetName') != 'PF-MCS-freeRTOS':
        failures.append('Unexpected target name')
    if tree.findtext('.//Device') != 'STM32G431RBTx':
        failures.append('Unexpected device')
    definitions = tree.findtext('.//Cads/VariousControls/Define', '')
    if set(definitions.split(',')) != {'USE_HAL_DRIVER', 'STM32G431xx'}:
        failures.append('Unexpected hardware definitions')
    sources = []
    for element in tree.findall('.//Groups/Group/Files/File/FilePath'):
        path = (PROJECT.parent / element.text.replace('\\', '/')).resolve()
        if not path.is_relative_to(ROOT) or not path.is_file():
            failures.append(f'Missing or external source: {element.text}')
        sources.append(path)
    for name in tree.findtext('.//Cads/VariousControls/IncludePath', '').split(';'):
        path = (PROJECT.parent / name.replace('\\', '/')).resolve()
        if not path.is_relative_to(ROOT) or not path.is_dir():
            failures.append(f'Missing or external include directory: {name}')
    c_sources = {p for p in ROOT.rglob('*.c') if not set(p.relative_to(ROOT).parts) & IGNORED}
    if c_sources != {p for p in sources if p.suffix == '.c'}:
        failures.append('C files do not exactly match the hardware target')
    forbidden = {'.o', '.obj', '.d', '.dep', '.crf', '.axf', '.hex', '.elf', '.bin',
                 '.log', '.htm', '.uvoptx', '.zip', '.bak'}
    for path in ROOT.rglob('*'):
        relative = path.relative_to(ROOT)
        if not path.is_file() or set(relative.parts) & IGNORED:
            continue
        if path.suffix.lower() in forbidden or '.uvguix.' in path.name:
            failures.append(f'Generated or local file outside build/: {relative.as_posix()}')
        text = path.read_text(encoding='utf-8', errors='replace')
        if path.suffix in {'.c', '.h', '.s', '.uvprojx'} and re.search(r'\b(RTOS_SIM|g_sim_\w+)\b', text):
            failures.append(f'Virtual firmware code retained: {relative.as_posix()}')
        if re.search(r'(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,}|LIC=[A-Z0-9-]{15,})', text):
            failures.append(f'Potential credential in {relative.as_posix()}')
        if re.search(r'(?i)[A-Z]:[\\/]Users[\\/]|F:[\\/]RTOS|F:[\\/]keilstm32', text):
            failures.append(f'Personal local path in {relative.as_posix()}')
        if path.suffix == '.md':
            for link in re.findall(r'\]\(([^)\s]+)\)', text):
                if re.match(r'(?:[a-z]+:|#)', link):
                    continue
                destination = link.split('#')[0]
                if destination and not (path.parent / destination).exists():
                    failures.append(f'Broken link in {relative.as_posix()}: {link}')
    report = ROOT / 'docs/data/software-tests.json'
    if report.exists():
        result = json.loads(report.read_text(encoding='utf-8'))
        actual = hashlib.sha256((ROOT / 'Src/app.c').read_bytes()).hexdigest()
        if result.get('source_sha256') != actual or result.get('passed') is not True:
            failures.append('Saved software result does not identify the current passing app.c')
    else:
        failures.append('Missing saved software validation result')
    build_report = ROOT / 'docs/data/build-verification.json'
    if build_report.exists():
        build = json.loads(build_report.read_text(encoding='utf-8'))
        for name, expected in build.get('source_sha256', {}).items():
            path = ROOT / name
            if not path.is_file() or hashlib.sha256(path.read_bytes().replace(b'\r\n', b'\n')).hexdigest() != expected:
                failures.append(f'Source changed since native build: {name}')
    else:
        failures.append('Missing saved native build verification result')
    if failures:
        for failure in failures:
            print('FAIL:', failure)
        raise SystemExit(1)
    print(f'PASS: one STM32G431 hardware target, {len(c_sources)} C sources, '
          f'{len(sources)} build entries; dependency paths, links and repository hygiene checked.')


if __name__ == '__main__':
    main()
