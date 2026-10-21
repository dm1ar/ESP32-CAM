#!/usr/bin/env python3
"""Board acceptance: original JPEG decoding, snapshots and stream sharing.

Usage: python3 tools/check_camera.py http://<DHCP-address-from-UART>/
Requires Pillow on the host. Stops streaming and restores settings on exit.
"""
import argparse
import io
import json
import threading
import time
import urllib.parse
import urllib.request
from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url")
    args = parser.parse_args()
    base = args.url.rstrip("/")
    parsed = urllib.parse.urlsplit(base)
    if parsed.scheme != "http" or not parsed.hostname or parsed.username or parsed.password:
        parser.error("Use the plain HTTP camera URL reported on UART")
    stream_url = f"http://{parsed.hostname}:81/stream"

    def get_status():
        with urllib.request.urlopen(base + "/status", timeout=10) as response:
            return json.load(response)

    def post(path, body=b""):
        with urllib.request.urlopen(urllib.request.Request(
                base + path, data=body, method="POST"), timeout=15) as response:
            return json.load(response)

    def standby():
        deadline = time.monotonic() + 12
        while time.monotonic() < deadline:
            status = get_status()
            if not status["streaming"] and status["camera_state"] == "standby":
                assert status["wifi_modem_sleep"] and status["fps"] == 0
                assert status["camera_error_code"] == 0
                return status
            time.sleep(.2)
        raise AssertionError("Camera did not return to healthy standby")

    def decode(data, size):
        with Image.open(io.BytesIO(data)) as image:
            image.load()
            assert image.size == size, (image.size, size)

    def snapshot(size):
        with urllib.request.urlopen(base + "/capture", timeout=15) as response:
            assert response.headers.get_content_type() == "image/jpeg"
            assert response.headers.get("Cache-Control") == "no-store"
            data = response.read(524289)
            assert 0 < len(data) <= 524288
            decode(data, size)

    initial = get_status()
    print("Testing", initial["node_name"], base, flush=True)
    results = []
    try:
        post("/stop")
        standby()
        for resolution, size in [("QVGA", (320, 240)), ("VGA", (640, 480)),
                                 ("SVGA", (800, 600)), ("XGA", (1024, 768)),
                                 ("UXGA", (1600, 1200))]:
            post("/control", f"resolution={resolution}&quality=12".encode())
            before = get_status()["frames_sent"]
            for _ in range(2):
                snapshot(size)
                assert standby()["frames_sent"] == before
            done, ready = threading.Event(), threading.Event()
            failures, count = [], [0]

            def stream():
                try:
                    with urllib.request.urlopen(stream_url, timeout=15) as response:
                        while not done.is_set():
                            line = response.readline()
                            if not line:
                                raise AssertionError("Stream ended unexpectedly")
                            if line.strip() != b"--esp32camframe":
                                continue
                            headers = {}
                            while True:
                                line = response.readline()
                                if line == b"\r\n":
                                    break
                                if not line:
                                    raise AssertionError("Incomplete frame headers")
                                key, value = line.decode("ascii").split(":", 1)
                                headers[key.lower()] = value.strip()
                            length = int(headers["content-length"])
                            assert 0 < length <= 524288
                            data = response.read(length)
                            assert len(data) == length
                            decode(data, size)
                            count[0] += 1
                            ready.set()
                except Exception as error:
                    if not done.is_set():
                        failures.append(str(error))
                    ready.set()

            reader = threading.Thread(target=stream)
            reader.start()
            try:
                assert ready.wait(20), "No initial stream frame"
                assert not failures, failures
                active = get_status()
                assert active["streaming"] and not active["stream_timed_out"]
                remaining = active["stream_remaining_seconds"]
                for _ in range(4):
                    snapshot(size)
                    status = get_status()
                    assert status["streaming"] and status["camera_error_code"] == 0
                    assert status["stream_remaining_seconds"] <= remaining
                assert not failures, failures
            finally:
                done.set()
                post("/stop")
                reader.join(20)
                assert not reader.is_alive(), "Stream reader did not exit"
            standby()
            result = {"resolution": resolution, "snapshots": 6,
                      "decoded_stream_frames": count[0], "result": "pass"}
            results.append(result)
            print(json.dumps(result), flush=True)
    finally:
        post("/stop")
        standby()
        post("/control", (f'resolution={initial["resolution"]}'
                          f'&quality={initial["jpeg_quality"]}').encode())
    print("PASS: idle snapshots, simultaneous JPEG stream/snapshots, cleanup and countdown", flush=True)


if __name__ == "__main__":
    main()
