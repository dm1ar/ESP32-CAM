"""Check the actual public image never erases the separate private settings."""
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from prepare_web_flash import prepare
from wifi_config import CONFIG_OFFSET,CONFIG_SIZE

class WebPackageTests(unittest.TestCase):
    def test_real_package_layout_and_preservation(self):
        with tempfile.TemporaryDirectory() as folder:
            target=Path(folder)/'public'
            prepare(ROOT/'build',target,'test-version')
            self.assertEqual(sorted(p.name for p in target.iterdir()),
                             ['firmware.bin','index.html','installer.mjs','manifest.json'])
            manifest=json.loads((target/'manifest.json').read_text())
            self.assertTrue(manifest['new_install_prompt_erase'])
            merged=(target/'firmware.bin').read_bytes()
            self.assertLessEqual(len(merged),CONFIG_OFFSET)
            metadata=json.loads((ROOT/'build/flasher_args.json').read_text())
            for offset,path in metadata['flash_files'].items():
                data=(ROOT/'build'/path).read_bytes()
                # esptool patches bootloader headers/digest if needed; the app
                # and partition table must be byte-exact in the release image.
                if int(offset,0)!=0x1000:
                    self.assertEqual(merged[int(offset,0):int(offset,0)+len(data)],data)
            flash=bytearray(b'\xaa'*(4*1024*1024))
            private=b'\x55'*CONFIG_SIZE
            flash[CONFIG_OFFSET:CONFIG_OFFSET+CONFIG_SIZE]=private
            sector_end=(len(merged)+4095)//4096*4096
            flash[:sector_end]=b'\xff'*sector_end
            flash[:len(merged)]=merged
            self.assertEqual(flash[CONFIG_OFFSET:CONFIG_OFFSET+CONFIG_SIZE],private)
            before=flash[:CONFIG_OFFSET]
            flash[CONFIG_OFFSET:CONFIG_OFFSET+CONFIG_SIZE]=b'\x66'*CONFIG_SIZE
            self.assertEqual(flash[:CONFIG_OFFSET],before)
            (target/'config.bin').write_bytes(private)
            with self.assertRaises(ValueError):
                prepare(ROOT/'build',target,'must-refuse-private-output')

if __name__=='__main__':
    unittest.main()
