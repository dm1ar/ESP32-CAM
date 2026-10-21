"""Use dummy credentials to validate TOML and round-trip official NVS images."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
from zlib import crc32

spec = importlib.util.spec_from_file_location('wifi_config', Path(__file__).resolve().parents[1] / 'tools/wifi_config.py')
wifi_config = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wifi_config)


def entries(image):
    result = {}
    page = image[:4096]
    assert struct.unpack_from('<I', page, 28)[0] == crc32(page[4:28], 0xffffffff)
    position = 64
    while page[position] != 0xff:
        item = page[position:position+32]
        assert struct.unpack_from('<I', item, 4)[0] == crc32(item[:4]+item[8:], 0xffffffff)
        key = item[8:24].rstrip(b'\0').decode()
        if item[1] == 0x21:
            length = struct.unpack_from('<H', item, 24)[0]
            data = page[position+32:position+32+length]
            assert struct.unpack_from('<I', item, 28)[0] == crc32(data, 0xffffffff)
            assert data[-1] == 0
            result[key] = data[:-1].decode()
        else:
            length = item[1] & 0xf
            result[key] = int.from_bytes(item[24:24+length], 'little')
        position += item[2]*32
    return result


class WifiConfigTests(unittest.TestCase):
    def source(self, folder, **changes):
        settings = dict(ssid='test-network', password='dummy-password')
        settings.update(changes)
        path = Path(folder) / 'wifi.toml'
        lines = ['[wifi]']
        for key,value in settings.items():
            lines.append(f'{key} = {json.dumps(value)}')
        path.write_text('\n'.join(lines)+'\n')
        return path

    def test_default_and_boundaries(self):
        with tempfile.TemporaryDirectory() as folder:
            source = self.source(folder)
            destination = Path(folder) / 'config.bin'
            capture = io.StringIO()
            with contextlib.redirect_stdout(capture), contextlib.redirect_stderr(capture):
                wifi_config.generate(source, destination)
            self.assertEqual(capture.getvalue(), '')
            self.assertEqual(destination.stat().st_size, 0x6000)
            self.assertEqual(destination.stat().st_mode & 0o777, 0o600)
            values = entries(destination.read_bytes())
            self.assertEqual(values, dict(camera=1, schema=1, ssid='test-network', password='dummy-password',
                                          node_name='ESP32-CAM01', auto_off=1, minutes=20))
            for ssid,password in [('s'*32,'a'*64), ('cäm', ' commas,"quotes"\n# and spaces ')]:
                source = self.source(folder, ssid=ssid, password=password, node_name='office-cam-03',
                                     auto_off=False, auto_off_minutes=120)
                wifi_config.generate(source,destination)
                values = entries(destination.read_bytes())
                self.assertEqual(values['ssid'],ssid)
                self.assertEqual(values['password'],password)
                self.assertEqual(values['node_name'],'office-cam-03')
                self.assertEqual(values['auto_off'],0)
                self.assertEqual(values['minutes'],120)
            self.assertEqual(sorted(p.name for p in Path(folder).iterdir()), ['config.bin','wifi.toml'])

    def test_validation_and_output_preservation(self):
        invalid = [('node_name',value) for value in ['', '-camera','camera-','cam_01','cam.local','cäm','x'*33,123,False]]
        invalid += [('auto_off',value) for value in ['true',1,0,1.0,[]]]
        invalid += [('auto_off_minutes',value) for value in [True,False,4,121,0,-1,'20',20.0,[]]]
        invalid += [('ssid',value) for value in ['', 's'*33, 'a\0b', 123]]
        invalid += [('password',value) for value in ['short','x'*64,'a'*65,'dummy\0password',False]]
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)/'config.bin'
            output.write_bytes(b'previous')
            for key,value in invalid:
                with self.subTest(key=key,value=value):
                    source = self.source(folder,**{key:value})
                    with self.assertRaises(ValueError) as error:
                        wifi_config.generate(source,output)
                    self.assertNotIn('dummy',str(error.exception))
                    self.assertEqual(output.read_bytes(), b'previous')
            source = self.source(folder)
            with self.assertRaises(ValueError):
                wifi_config.generate(source,source)
            self.assertEqual(wifi_config.load(source)['password'],'dummy-password')

    def test_redacted_generator_failure(self):
        with tempfile.TemporaryDirectory() as folder:
            source = self.source(folder)
            with patch.object(wifi_config.subprocess, 'run') as run:
                run.return_value.returncode = 1
                run.return_value.stdout = b'dummy-password'
                with self.assertRaises(ValueError) as error:
                    wifi_config.generate(source,Path(folder)/'config.bin')
            self.assertNotIn('dummy',str(error.exception))


if __name__ == '__main__':
    unittest.main()
