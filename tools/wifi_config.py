"""Validate local Wi-Fi TOML and generate a private build header, without logging secrets."""
import argparse
from pathlib import Path
import re
import sys
import tomllib


def generate(source: Path, destination: Path) -> None:
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
    # Seconds are exposed as uint32_t in JSON. Reject booleans, fractions,
    # zero and overflow before producing C++ constants, even when disabled.
    if type(auto_off_minutes) is not int or not 5 <= auto_off_minutes <= 120:
        raise ValueError("Set [wifi] auto_off_minutes to an integer from 5 to 120.")

    def array(name: str, value: bytes) -> str:
        return f"static const unsigned char {name}[] = {{{','.join(map(str, value))},0}};\n"

    header = "// Generated from wifi.toml. Contains credentials; do not commit.\n#pragma once\n"
    header += array("APP_WIFI_SSID", ssid_bytes)
    header += array("APP_WIFI_PASSWORD", password_bytes)
    header += f'inline constexpr char APP_NODE_NAME[] = "{node_name}";\n'
    header += f'inline constexpr bool APP_AUTO_OFF = {str(auto_off).lower()};\n'
    header += f'inline constexpr unsigned APP_AUTO_OFF_MINUTES = {auto_off_minutes}U;\n'
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not destination.exists() or destination.read_text() != header:
        destination.write_text(header)
        destination.chmod(0o600)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    try:
        generate(args.source, args.destination)
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        sys.exit(1)
