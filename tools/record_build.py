"""Record a native Keil build without publishing raw logs or machine details."""
import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import json
from pathlib import Path
import re
import struct
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


def read_hex(path):
    image = {}
    base = 0
    eof = False
    records = 0
    for text in path.read_text(encoding='ascii').splitlines():
        raw = bytes.fromhex(text.removeprefix(':'))
        if not text.startswith(':') or len(raw) != raw[0] + 5 or sum(raw) % 256:
            raise ValueError('Invalid Intel HEX record or checksum')
        if eof:
            raise ValueError('Intel HEX has records after EOF')
        address = int.from_bytes(raw[1:3], 'big')
        kind, data = raw[3], raw[4:-1]
        if kind == 0:
            for index, value in enumerate(data):
                absolute = base + address + index
                if absolute in image:
                    raise ValueError('Overlapping Intel HEX data')
                image[absolute] = value
        elif kind == 1:
            eof = True
        elif kind == 4:
            base = int.from_bytes(data, 'big') << 16
        elif kind == 2:
            base = int.from_bytes(data, 'big') << 4
        elif kind not in (3, 5):
            raise ValueError('Unsupported Intel HEX record')
        records += 1
    if not eof or not image:
        raise ValueError('Intel HEX missing data or EOF')
    return image, records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compare-hex', type=Path, help='Optional reference HEX; its path is not saved')
    args = parser.parse_args()
    artifact = ROOT / 'build/PF-MCS-freeRTOS.hex'
    image, records = read_hex(artifact)
    if min(image) < 0x08000000 or max(image) >= 0x08020000:
        raise ValueError('HEX outside configured STM32G431 Flash')
    sp, reset = struct.unpack('<II', bytes(image[i] for i in range(0x08000000, 0x08000008)))
    if not 0x20000000 <= sp <= 0x20008000 or not reset & 1:
        raise ValueError('Invalid initial vector')
    if not 0x08000000 <= (reset & ~1) <= max(image):
        raise ValueError('Reset handler outside programmed Flash')
    log = (ROOT / 'build/keil-build.log').read_text(errors='replace')
    html = (ROOT / 'build/PF-MCS-freeRTOS.build_log.htm').read_text(errors='replace')
    sizes = re.search(r'Code=(\d+) RO-data=(\d+) RW-data=(\d+) ZI-data=(\d+)', log)
    compiler = re.search(r'C Compiler:\s*([^<\r\n]+)', html)
    ide = re.search(r'IDE-Version:\s*([^<\r\n]+)', html)
    if not sizes or '0 Error(s), 0 Warning(s)' not in log or not compiler or not ide:
        raise ValueError('Missing successful native build or toolchain identification')
    code, ro, rw, zi = map(int, sizes.groups())
    tree = ET.parse(ROOT / 'MDK-ARM/PF-MCS-freeRTOS.uvprojx')
    source_hashes = {
        p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes().replace(b'\r\n', b'\n')).hexdigest()
        for p in sorted(ROOT.rglob('*'))
        if p.is_file() and p.suffix in ('.c', '.h', '.s', '.uvprojx')
    }
    result = {
        'date': datetime.now(timezone(timedelta(hours=8))).date().isoformat(),
        'target': 'STM32G431RBTx / CT117E-M4',
        'project': 'MDK-ARM/PF-MCS-freeRTOS.uvprojx',
        'toolchain': compiler.group(1).strip(),
        'ide': 'Keil uVision ' + re.search(r'V\d+(?:\.\d+)+', ide.group(1)).group(0),
        'device_pack': tree.findtext('.//PackID'),
        'rebuild': True, 'errors': 0, 'warnings': 0,
        'c_translation_units': len(tree.findall('.//Groups/Group/Files/File[FileType="1"]')),
        'assembly_units': len(tree.findall('.//Groups/Group/Files/File[FileType="2"]')),
        'size_bytes': {'code': code, 'ro_data': ro, 'rw_data': rw, 'zi_data': zi,
                       'flash_payload_code_ro_rw': code + ro + rw, 'ram_static_rw_zi': rw + zi},
        'hex': {'path_when_built': 'build/PF-MCS-freeRTOS.hex',
                'bytes': artifact.stat().st_size,
                'sha256': hashlib.sha256(artifact.read_bytes()).hexdigest(),
                'all_record_checksums_valid': True, 'record_count': records,
                'programmed_bytes': len(image),
                'first_address': f'0x{min(image):08X}', 'last_address': f'0x{max(image):08X}',
                'initial_stack_pointer': f'0x{sp:08X}', 'reset_vector': f'0x{reset:08X}'},
        'hardware_flashed': False, 'hardware_measured': False,
        'source_hash_normalization': 'SHA-256 of bytes with CRLF converted to LF',
        'source_sha256': source_hashes
    }
    if args.compare_hex:
        reference, _ = read_hex(args.compare_hex)
        result['hex']['same_programmed_bytes_as_original_hardware_hex'] = image == reference
    output = ROOT / 'docs/data/build-verification.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    print(f'PASS: native build recorded; {len(image)} programmed bytes, '
          f'{records} valid HEX records; SHA-256 {result["hex"]["sha256"]}')


if __name__ == '__main__':
    main()
