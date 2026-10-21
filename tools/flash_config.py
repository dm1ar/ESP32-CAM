"""Generate and flash only private NVS settings; manual download mode required."""
import argparse
from pathlib import Path
import subprocess
import sys
from wifi_config import CONFIG_OFFSET, generate

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='/dev/ttyUSB0')
    parser.add_argument('--baud', type=int, default=115200)
    parser.add_argument('--source', type=Path, default=Path('wifi.toml'))
    parser.add_argument('--output', type=Path, default=Path('build/private_config/config.bin'))
    args = parser.parse_args()
    try:
        generate(args.source, args.output)
    except (ValueError, OSError):
        parser.exit(1, 'Configuration generation failed. Check wifi.toml and the Python NVS generator installation.\n')
    result = subprocess.run([sys.executable, '-m', 'esptool', '--chip', 'esp32', '--port', args.port,
                             '--baud', str(args.baud), '--before', 'no-reset', '--after', 'no-reset',
                             'write-flash', hex(CONFIG_OFFSET), str(args.output)])
    sys.exit(result.returncode)
