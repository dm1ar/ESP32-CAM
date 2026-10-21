"""Validate camera settings and create a private NVS config.bin (Python 3.11+)."""
import argparse
import csv
import os
import subprocess
import tempfile
from pathlib import Path
import re
import sys
import tomllib


CONFIG_SIZE = 0x6000
CONFIG_OFFSET = 0x190000


def load(source: Path) -> dict:
    try:
        with source.open("rb") as stream:
            wifi = tomllib.load(stream)["wifi"]
        ssid, password = wifi["ssid"], wifi["password"]
        if not isinstance(ssid, str) or not isinstance(password, str):
            raise ValueError
        ssid_bytes, password_bytes = ssid.encode("utf-8"), password.encode("utf-8")
        if not 1 <= len(ssid_bytes) <= 32 or b"\0" in ssid_bytes:
            raise ValueError
        is_psk = len(password_bytes) == 64 and all(c in b"0123456789abcdefABCDEF" for c in password_bytes)
        if not (8 <= len(password_bytes) <= 63 or is_psk) or b"\0" in password_bytes:
            raise ValueError
    except (OSError, KeyError, TypeError, ValueError):
        # TOML parse exceptions can contain user input; never print their text.
        raise ValueError("Set [wifi] ssid and password in wifi.toml. SSID must be 1-32 UTF-8 bytes; password must be 8-63 bytes or 64 hex digits.") from None

    # DHCP hostname and JSON share this identifier. Restrict it to one ASCII
    # hostname label, bounded by ESP-NETIF's 32-character hostname capacity.
    # Do not interpolate invalid TOML values into diagnostics (secrets may have
    # been pasted into the wrong field). Older configurations get the default.
    node_name = wifi.get("node_name", "ESP32-CAM01")
    if not isinstance(node_name, str) or re.fullmatch(
        r"[A-Za-z0-9](?:[A-Za-z0-9-]{0,30}[A-Za-z0-9])?", node_name
    ) is None:
        raise ValueError("Set [wifi] node_name to 1-32 ASCII letters, digits or internal hyphens.")

    auto_off = wifi.get("auto_off", True)
    auto_off_minutes = wifi.get("auto_off_minutes", 20)
    if type(auto_off) is not bool:
        raise ValueError("Set [wifi] auto_off to a TOML boolean (true or false).")
    # Reject booleans, fractions and out-of-range policy values before
    # generating NVS entries, even when automatic stop is disabled.
    if type(auto_off_minutes) is not int or not 5 <= auto_off_minutes <= 120:
        raise ValueError("Set [wifi] auto_off_minutes to an integer from 5 to 120.")

    return {"ssid": ssid, "password": password, "node_name": node_name,
            "auto_off": auto_off, "auto_off_minutes": auto_off_minutes}


def generate(source: Path, destination: Path) -> None:
    settings = load(source)
    destination = destination.resolve()
    if destination == source.resolve():
        raise ValueError("Output must differ from wifi.toml.")
    destination.parent.mkdir(parents=True, exist_ok=True)
    # All intermediate credentials stay in a private directory. File entries
    # preserve spaces, commas, quotes and newlines exactly through the SDK tool.
    with tempfile.TemporaryDirectory(prefix="camera-config-", dir=destination.parent) as folder:
        private = Path(folder)
        rows = [["key", "type", "encoding", "value"], ["camera", "namespace", "", ""],
                ["schema", "data", "u32", "1"]]
        for key in ("ssid", "password", "node_name"):
            item = private / key
            item.write_bytes(settings[key].encode("utf-8"))
            item.chmod(0o600)
            rows.append([key, "file", "string", str(item)])
        rows += [["auto_off", "data", "u8", int(settings["auto_off"])],
                 ["minutes", "data", "u32", settings["auto_off_minutes"]]]
        csv_path = private / "settings.csv"
        with csv_path.open("w", newline="", encoding="utf-8") as stream:
            csv.writer(stream).writerows(rows)
        image = private / "config.bin"
        result = subprocess.run([sys.executable, "-m", "esp_idf_nvs_partition_gen",
                                 "generate", str(csv_path), str(image), hex(CONFIG_SIZE)],
                                capture_output=True, check=False)
        if result.returncode or not image.exists() or image.stat().st_size != CONFIG_SIZE:
            raise ValueError("NVS generation failed. Install esp-idf-nvs-partition-gen in this Python environment; check output access.")
        image.chmod(0o600)
        os.replace(image, destination)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, nargs="?", default=Path("wifi.toml"))
    parser.add_argument("destination", type=Path, nargs="?", default=Path("build/private_config/config.bin"))
    args = parser.parse_args()
    try:
        generate(args.source, args.destination)
    except (ValueError, OSError) as exc:
        # OSError filenames can contain user input; keep diagnostics generic.
        print(str(exc) if isinstance(exc, ValueError) else "Cannot read/write configuration files.", file=sys.stderr)
        sys.exit(1)
    print("Private config.bin generated; flash at 0x190000. Do not publish this file.")
