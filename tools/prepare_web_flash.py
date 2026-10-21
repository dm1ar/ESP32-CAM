"""Package a credential-free ESP Web Tools site from an existing IDF build."""
import argparse
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys

from wifi_config import CONFIG_OFFSET, CONFIG_SIZE


def prepare(build: Path, output: Path, version: str) -> None:
    build, output = build.resolve(), output.resolve()
    table = (build / 'partition_table/partition-table.bin').read_bytes()
    partitions = {}
    for position in range(0, len(table) - 31, 32):
        magic, kind, subtype, offset, size, label, _ = struct.unpack_from('<HBBII16sI', table, position)
        if magic != 0x50AA:
            break
        partitions[label.rstrip(b'\0').decode()] = (kind, subtype, offset, size)
    if partitions.get('config') != (1, 2, CONFIG_OFFSET, CONFIG_SIZE):
        raise ValueError('Build does not contain the expected config NVS partition; rebuild first.')
    if partitions.get('factory') != (0, 0, 0x10000, 0x177000):
        raise ValueError('Unexpected application partition layout.')
    metadata = json.loads((build / 'flasher_args.json').read_text())
    inputs = metadata['flash_files']
    if {int(offset, 0) for offset in inputs} != {0x1000, 0x8000, 0x10000}:
        raise ValueError('Expected only public bootloader, partition table and app images.')
    files = []
    for offset, filename in inputs.items():
        source = (build / filename).resolve()
        if not source.is_relative_to(build) or int(offset, 0) + source.stat().st_size > CONFIG_OFFSET:
            raise ValueError('Image is outside the build or overlaps private settings.')
        if int(offset, 0) == 0x10000 and b'Settings missing or invalid.' not in source.read_bytes():
            raise ValueError('App does not identify as the NVS configuration build; rebuild first.')
        files += [offset, str(source)]
    output.mkdir(parents=True, exist_ok=True)
    allowed = {'firmware.bin', 'index.html', 'installer.mjs', 'manifest.json'}
    if any(path.name not in allowed for path in output.iterdir()):
        raise ValueError('Output contains unrelated or private files; choose a clean installer directory.')
    target = output / 'firmware.bin'
    subprocess.run([sys.executable, '-m', 'esptool', '--chip', 'esp32', 'merge-bin',
                    '--flash-mode', 'dio', '--flash-freq', '40m', '--flash-size', '4MB',
                    '-o', str(target), *files], check=True, capture_output=True)
    if target.stat().st_size > CONFIG_OFFSET:
        target.unlink()
        raise ValueError('Merged image would overwrite the private config partition.')
    source = Path(__file__).resolve().parents[1] / 'web-flash'
    for filename in ('index.html', 'installer.mjs'):
        shutil.copyfile(source / filename, output / filename)
    manifest = {'name': 'ESP32-CAM', 'version': version, 'new_install_prompt_erase': True,
                'new_install_improv_wait_time': 0,
                'builds': [{'chipFamily': 'ESP32', 'parts': [{'path': 'firmware.bin', 'offset': 0}]}]}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=Path('build'))
    parser.add_argument('--output', type=Path, default=Path('build/web-flash'))
    parser.add_argument('--version', default='development')
    args = parser.parse_args()
    try:
        prepare(args.build_dir, args.output, args.version)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError):
        parser.exit(1, 'Could not package web firmware. Build the NVS firmware first; check paths and esptool installation.\n')
    print('Web installer prepared. Publish only this output directory; never copy config.bin into it.')
