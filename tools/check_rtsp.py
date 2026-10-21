#!/usr/bin/env python3
"""Hardware acceptance for RTSP JPEG over TCP/UDP plus concurrent snapshots.

Usage: python3 tools/check_rtsp.py http://<DHCP-address-from-UART>/
Requires FFmpeg and Pillow. Stops video and restores camera settings on exit.
"""
import argparse
import io
import json
import subprocess
import time
import urllib.error
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
    rtsp = f"rtsp://{parsed.hostname}:554/stream"

    def status():
        with urllib.request.urlopen(base + "/status", timeout=8) as response:
            return json.load(response)

    def post(path, body=b""):
        with urllib.request.urlopen(urllib.request.Request(
                base + path, data=body, method="POST"), timeout=10) as response:
            return json.load(response)

    def idle():
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            value = status()
            if not value["streaming"] and value["camera_state"] == "standby":
                assert value["camera_error_code"] == 0 and value["wifi_modem_sleep"]
                assert value["fps"] == 0
                return
            time.sleep(.2)
        raise AssertionError("No healthy standby after RTSP disconnect/Stop")

    def command(transport, frames):
        return ["ffmpeg", "-hide_banner", "-loglevel", "error", "-rtsp_transport", transport,
                "-analyzeduration", "1000000", "-probesize", "32768",
                "-i", rtsp, "-frames:v", str(frames), "-f", "framemd5", "-"]

    def count_frames(output, width, height):
        assert f"{width}x{height}" in output, "Wrong decoded resolution"
        return len([line for line in output.splitlines() if line and not line.startswith("#")])

    initial = status()
    print("Testing", initial["node_name"], rtsp, flush=True)
    try:
        post("/stop")
        idle()
        for resolution, width, height in [("QVGA", 320, 240), ("VGA", 640, 480),
                                          ("SVGA", 800, 600), ("XGA", 1024, 768),
                                          ("UXGA", 1600, 1200)]:
            post("/control", f"resolution={resolution}&quality=12".encode())
            for transport in ("tcp", "udp"):
                result = subprocess.run(command(transport, 3), capture_output=True, text=True, timeout=35)
                assert result.returncode == 0, result.stderr
                assert count_frames(result.stdout, width, height) == 3, (result.stdout, result.stderr)
                idle()
                print(json.dumps({"resolution": resolution, "transport": transport,
                                  "decoded_frames": 3, "result": "pass"}), flush=True)
        post("/control", b"resolution=VGA&quality=12")
        reader = subprocess.Popen(command("tcp", 100), stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, text=True)
        try:
            deadline = time.monotonic() + 15
            while not status()["streaming"]:
                assert reader.poll() is None, "RTSP client ended before streaming"
                assert time.monotonic() < deadline, "RTSP did not start"
                time.sleep(.1)
            before = status()
            for _ in range(4):
                with urllib.request.urlopen(base + "/capture", timeout=12) as response:
                    assert response.headers.get_content_type() == "image/jpeg"
                    with Image.open(io.BytesIO(response.read(524289))) as image:
                        image.load()
                        assert image.size == (640, 480)
                after = status()
                assert after["streaming"] and after["camera_error_code"] == 0
                assert after["stream_remaining_seconds"] <= before["stream_remaining_seconds"]
            # Live policy updates must not restart the video or its clock.
            post("/auto-off", b"auto_off=false&auto_off_minutes=120")
            disabled = status()
            assert disabled["streaming"] and not disabled["auto_off"]
            assert disabled["stream_remaining_seconds"] == 0
            post("/auto-off", b"auto_off=true&auto_off_minutes=5")
            enabled = status()
            assert enabled["streaming"] and enabled["auto_off"]
            assert 0 < enabled["stream_remaining_seconds"] <= 300
            for invalid in (b"auto_off=true&auto_off_minutes=4",
                            b"auto_off=true&auto_off_minutes=121",
                            b"auto_off=1&auto_off_minutes=20"):
                try:
                    post("/auto-off", invalid)
                    raise AssertionError("Invalid policy accepted")
                except urllib.error.HTTPError as error:
                    assert error.code == 400
            assert status()["auto_off_minutes"] == 5
            post("/auto-off", (f'auto_off={str(initial["auto_off"]).lower()}'
                               f'&auto_off_minutes={initial["auto_off_minutes"]}').encode())
            try:
                with urllib.request.urlopen(f"http://{parsed.hostname}:81/stream", timeout=8):
                    raise AssertionError("Competing MJPEG session was allowed")
            except urllib.error.HTTPError as error:
                assert error.code == 503, error.code
            assert status()["streaming"], "Rejected client stopped the RTSP owner"
            time.sleep(1.2)
            measured = status()
            assert measured["fps"] > 0 and measured["frames_sent"] > before["frames_sent"]
            post("/stop")
            output, errors = reader.communicate(timeout=15)
            assert reader.returncode == 0, errors
            assert count_frames(output, 640, 480) > 0
            idle()
            print("PASS: concurrent snapshots, busy MJPEG rejection, measured FPS and HTTP Stop", flush=True)
        finally:
            if reader.poll() is None:
                reader.terminate()
                reader.communicate(timeout=10)
    finally:
        post("/stop")
        idle()
        post("/control", (f'resolution={initial["resolution"]}'
                          f'&quality={initial["jpeg_quality"]}').encode())
        post("/auto-off", (f'auto_off={str(initial["auto_off"]).lower()}'
                           f'&auto_off_minutes={initial["auto_off_minutes"]}').encode())
    print("PASS: RTSP hardware acceptance including live auto-off controls", flush=True)


if __name__ == "__main__":
    main()
