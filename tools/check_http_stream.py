#!/usr/bin/env python3
"""Check raw HTTP/1.0 and HTTP/1.1 MJPEG framing and Stop/EOF on the board."""
import argparse
import http.client
import io
import json
import socket
import time
import urllib.parse
import urllib.request
from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url")
    args = parser.parse_args()
    base = args.url.rstrip("/")
    host = urllib.parse.urlsplit(base).hostname
    if not host:
        parser.error("Provide the DHCP browser URL")

    def stop():
        with urllib.request.urlopen(urllib.request.Request(base + "/stop", data=b""), timeout=5):
            pass

    def idle():
        for _ in range(50):
            with urllib.request.urlopen(base + "/status", timeout=5) as response:
                status = json.load(response)
            if not status["streaming"]:
                assert status["camera_error_code"] == 0
                return
            time.sleep(.1)
        raise AssertionError("Camera did not become idle")

    stop()
    idle()
    for version in ("1.0", "1.1"):
        try:
            with socket.create_connection((host, 81), timeout=12) as connection:
                connection.sendall(f"GET /stream HTTP/{version}\r\nHost: {host}:81\r\n\r\n".encode())
                response = http.client.HTTPResponse(connection)
                response.begin()
                assert response.status == 200, response.status
                assert response.getheader("Transfer-Encoding") is None
                assert response.getheader("Connection").lower() == "close"
                assert response.getheader("Content-Type").startswith("multipart/x-mixed-replace;")
                for index in range(3):
                    boundary = response.readline(128)
                    if boundary == b"\r\n":
                        boundary = response.readline(128)
                    assert boundary == b"--esp32camframe\r\n", boundary
                    headers = {}
                    for _ in range(8):
                        line = response.readline(256)
                        if line == b"\r\n":
                            break
                        key, value = line.decode().split(":", 1)
                        headers[key.lower()] = value.strip()
                    length = int(headers["content-length"])
                    assert 0 < length <= 524288
                    assert headers["content-type"] == "image/jpeg"
                    image = response.read(length)
                    assert len(image) == length
                    with Image.open(io.BytesIO(image)) as decoded:
                        decoded.load()
                        print(f"HTTP/{version} JPEG {index + 1}: {decoded.size}", flush=True)
                stop()
                tail = response.read(2 * 1024 * 1024 + 1)
                assert len(tail) <= 2 * 1024 * 1024
                assert tail.endswith(b"--esp32camframe--\r\n"), tail[-40:]
            idle()
        finally:
            stop()
            idle()
    print("PASS: HTTP/1.0 and HTTP/1.1 original JPEGs, no chunk coding, Stop boundary and EOF")


if __name__ == "__main__":
    main()
