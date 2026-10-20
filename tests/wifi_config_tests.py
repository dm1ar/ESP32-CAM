"""Test generated identity and validation with dummy credentials only."""
import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "wifi_config", Path(__file__).resolve().parents[1] / "tools" / "wifi_config.py"
)
wifi_config = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wifi_config)


class WifiConfigTests(unittest.TestCase):
    def generate(self, name_line=""):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "wifi.toml"
            destination = Path(directory) / "private" / "wifi_config.h"
            source.write_text('[wifi]\nssid = "test-network"\npassword = "dummy-password"\n' + name_line)
            capture = io.StringIO()
            with contextlib.redirect_stdout(capture), contextlib.redirect_stderr(capture):
                wifi_config.generate(source, destination)
            self.assertEqual(capture.getvalue(), "")
            self.assertEqual(destination.stat().st_mode & 0o777, 0o600)
            return destination.read_text()

    def test_default_and_unique_names(self):
        self.assertIn('APP_NODE_NAME[] = "ESP32-CAM01";', self.generate())
        for name in ["ESP32-CAM02", "office-cam-03", "A", "x" * 32]:
            with self.subTest(name=name):
                header = self.generate(f'node_name = "{name}"\n')
                self.assertIn(f'APP_NODE_NAME[] = "{name}";', header)
                self.assertNotIn("dummy-password", header)

    def test_reject_bad_hostname_without_exposing_input(self):
        for value in ['""', '"-camera"', '"camera-"', '"cam_01"', '"cam.local"',
                      '"cam name"', '"cäm"', '"' + 'x' * 33 + '"', '123', 'false',
                      '"dummy-password/pasted-secret"']:
            with self.subTest(value=value):
                with self.assertRaises(ValueError) as error:
                    self.generate(f"node_name = {value}\n")
                self.assertNotIn("dummy-password", str(error.exception))

    def test_auto_off_defaults_and_custom_policy(self):
        header = self.generate()
        self.assertIn("APP_AUTO_OFF = true;", header)
        self.assertIn("APP_AUTO_OFF_MINUTES = 20U;", header)
        for enabled in ["true", "false"]:
            for minutes in [1, 7, 20, 60, 71582788]:
                with self.subTest(enabled=enabled, minutes=minutes):
                    header = self.generate(f"auto_off = {enabled}\nauto_off_minutes = {minutes}\n")
                    self.assertIn(f"APP_AUTO_OFF = {enabled};", header)
                    self.assertIn(f"APP_AUTO_OFF_MINUTES = {minutes}U;", header)

    def test_auto_off_strict_types_and_ranges(self):
        for value in ['"true"', '1', '0', '1.0', '[]', '"dummy-password"']:
            with self.subTest(auto_off=value):
                with self.assertRaises(ValueError) as error:
                    self.generate(f"auto_off = {value}\n")
                self.assertNotIn("dummy-password", str(error.exception))
        for value in ['true', 'false', '0', '-1', '71582789', '20.0', '"20"', '[]']:
            for enabled in ["true", "false"]:
                with self.subTest(minutes=value, enabled=enabled):
                    with self.assertRaises(ValueError):
                        self.generate(f"auto_off = {enabled}\nauto_off_minutes = {value}\n")


if __name__ == "__main__":
    unittest.main()
