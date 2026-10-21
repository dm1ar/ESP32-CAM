# ESP32-CAM: MJPEG, RTSP and WebRTC

ESP-IDF firmware for the AI-Thinker ESP32-CAM with an OV2640 sensor. View live
JPEG video in a browser or VLC, take snapshots, change resolution and JPEG
quality, and set an automatic stream timeout. All three streaming modes run
on the camera; WebRTC needs no gateway.

The firmware uses native ESP-IDF 6.1, GNU++26 and FreeRTOS. The sensor stays
in standby until a viewer or snapshot request needs it. The flash lamp stays
off, and images are not saved to an SD card.

[Architecture and diagrams](docs/ARCHITECTURE.md) ·
[Coding guidelines](docs/CODING_GUIDELINES.md) ·
[Validation record](docs/VALIDATION.md) ·
[GitHub profile](https://github.com/dm1ar)

## Features and limits

- MJPEG in the supplied browser page or VLC, with HTTP/1.0 and HTTP/1.1 support.
- JPEG over RTSP/RTP, using UDP unicast or interleaved TCP; up to 10 captures/s.
- Direct JPEG over an encrypted WebRTC data channel; up to 5 captures/s.
- JPEG snapshots while idle or during any streaming mode.
- QVGA, VGA, SVGA, XGA and UXGA; JPEG quality 0–63, default VGA / 12.
- Browser controls for automatic stop: enabled by default, 20 minutes,
  adjustable from 5 to 120 minutes.
- Live telemetry, including measured sent FPS, throughput, Wi-Fi signal,
  memory use and remaining stream time.

**One active video viewer is supported across MJPEG, RTSP and WebRTC.** Stop
one viewer before starting another. Snapshots and the settings page remain
available. For multiple viewers, use a host such as openHAB to relay a single
upstream camera stream.

WebRTC uses the supplied JPEG canvas viewer rather than a standard H.264/VP8
video track. VLC and arbitrary WebRTC players cannot play this custom data
channel. Audio, H.264 encoding, OTA updates and SD-card recording are not
implemented.

## Hardware

The target is an AI-Thinker ESP32-CAM with an OV2640, 4 MB flash and working
PSRAM. The tested ESP32-D0WDQ6-V3 board has two Xtensa cores and an 8 MB PSRAM
chip, of which 4 MB is mapped. Other board revisions can have different memory
or sensor hardware; check the boot log and [pin assignment](main/board.hpp).

For UART programming, use 3.3 V logic and supply the board through its 5V pin
with adequate power. A DSD TECH SH-U06B bridge was used for validation.

| USB-to-UART adapter | ESP32-CAM |
| --- | --- |
| TX, 3.3 V logic | U0R / GPIO3 |
| RX, 3.3 V logic | U0T / GPIO1 |
| GND | GND |
| Appropriate 5 V supply | 5V input |

Connect GPIO0 to GND at reset for download mode. Remove that jumper and reset
for normal operation: GPIO0 supplies the camera clock at runtime. GPIO4 is the
flash lamp pin and also an SD-card data pin. The board's VCC pin is a 3.3 V
output.

## Configure and build

Install ESP-IDF **6.1** and its matching ESP32 toolchain and Python environment.
The project requires GNU++26 support and disables exceptions and RTTI in its
own firmware sources. See the [ESP-IDF documentation](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/)
for SDK installation.

Create a local `wifi.toml`, using [wifi.example.toml](wifi.example.toml) as a
starting point. For example:

```toml
[wifi]
node_name = "ESP32-CAM01"
auto_off = true
auto_off_minutes = 20
ssid = "your-wifi-ssid"
password = "your-wifi-password"
```

Use a 2.4 GHz network. Give each camera a unique `node_name`: 1–32 ASCII
letters/digits with optional internal hyphens. It identifies the DHCP host,
main browser page and status JSON. Missing identity/timeout settings default
to `ESP32-CAM01`, `true` and `20`; configured durations must be whole minutes
from 5 to 120.

The build validates the TOML and generates `build/private_config/wifi_config.h`.
`wifi.toml`, generated configuration, build products and managed components
are ignored by Git. Credentials are embedded in firmware binaries; keep both
the local configuration and those binaries private.

With the SDK environment activated, build using:

```bash
idf.py build
```

Alternatively, the project's wrapper activates the SDK environment. Override
its local installation defaults to match your machine:

```bash
export IDF_PATH="$HOME/esp/v6.1/esp-idf"
export IDF_TOOLS_PATH="$HOME/.espressif"
export IDF_PYTHON_ENV_PATH="$IDF_TOOLS_PATH/python_env/idf6.1_py3.12_env"
bash tools/idf.sh build
```

Keep [dependencies.lock](dependencies.lock) in version control. The build pins
`esp32-camera` 2.1.7, `esp_peer` 1.5.7 and `mdns` 1.14.0; the lock also records
`esp_jpeg` 1.3.1 and `esp_libsrtp` 1.0.0. Use `sdkconfig.defaults` for clean-build
defaults. Editing defaults does not override an existing generated `sdkconfig`;
use Menuconfig to change that configuration.

The included VS Code tasks provide Build, Flash, Monitor, Menuconfig and host
tests. Adjust the Espressif extension's SDK, Python and serial-port settings
for your workstation. `Ctrl+Shift+B` invokes the default Build task.

## Flash and start

The examples use `/dev/ttyUSB0` at 115200 baud; select your adapter's actual
port. Close other serial monitors before opening it.

1. Connect GPIO0 to GND and reset or power-cycle the ESP32-CAM.
2. Flash the complete build, including the bootloader and partition table:

   ```bash
   bash tools/idf.sh -p /dev/ttyUSB0 -b 115200 flash
   ```

3. Remove GPIO0 from GND.
4. Start the monitor, then press RESET:

   ```bash
   bash tools/idf.sh -p /dev/ttyUSB0 monitor
   ```

5. Read the assigned browser URL from UART. DHCP addresses can change.
   Exit the monitor with `Ctrl+]`.

The project deliberately disables automatic resets during flashing. USB
adapter enumeration alone does not establish a bootloader connection. If the
uploader receives no serial data while the camera is still reachable over
Wi-Fi, check the GPIO0 jumper and reset sequence. Brownout messages during
startup warrant checking the supply and wiring.

## Connect a viewer

Replace `CAMERA_IP` below with the address printed by the camera. Use a
client on the same LAN.

| Mode | URL | Client |
| --- | --- | --- |
| Camera controls and MJPEG viewer | `http://CAMERA_IP/` | Browser |
| Direct MJPEG stream | `http://CAMERA_IP:81/stream` | Browser or VLC |
| RTSP JPEG stream | `rtsp://CAMERA_IP:554/stream` | VLC with Live555, FFmpeg/FFplay |
| WebRTC viewer | `http://CAMERA_IP/webrtc` | Browser; tested in Chrome |
| Single JPEG snapshot | `http://CAMERA_IP/capture` | Browser or HTTP client |

For VLC, use **Media → Open Network Stream**, or run:

```bash
vlc http://CAMERA_IP:81/stream
vlc --rtsp-tcp rtsp://CAMERA_IP:554/stream
```

For Snap VLC, replace `vlc` with `snap run vlc`. Some distribution packages
omit Live555 and cannot play the RTSP endpoint. Snap VLC 3.0.24 was verified
with both RTSP and MJPEG. FFplay can also use RTSP over TCP:

```bash
ffplay -rtsp_transport tcp rtsp://CAMERA_IP:554/stream
```

For WebRTC, stop VLC and other camera viewers, open `/webrtc`, then click
**Start WebRTC**. Use **Stop** to end that session. Resolution, JPEG quality
and auto-off settings are on the main camera page. The camera resolves
Chrome's `.local` ICE candidate names through mDNS; no browser privacy flag
changes are needed. LAN multicast DNS must reach the camera. After a firmware
update, hard-refresh the page if the browser still has an older viewer loaded.

The interface is intended for a trusted LAN: HTTP signaling, snapshots and
RTSP have no authentication or TLS. WebRTC media uses DTLS/SCTP, but its
signaling is still HTTP. There is no STUN/TURN service or Internet traversal
configuration.

## Settings, snapshots and standby

The main page's **Apply settings** button changes resolution and JPEG quality.
Lower quality numbers produce better images and larger frames. Higher
resolution and larger JPEGs can reduce throughput. Settings are RAM-only and
return to VGA / quality 12 after reboot.

The **Auto-off** checkbox and **5–120 minute** slider save immediately. They
apply to the active video session and remain in RAM until reboot, which
restores the compiled TOML defaults. The countdown stays anchored to the
original session start: camera settings and snapshots do not reset it.
Disabling auto-off removes the deadline; enabling or shortening it past the
elapsed duration requests a stop. Policy edits cannot revive an already
stopped session. A new Start, or RTSP PLAY after PAUSE, starts a new interval.
A player that reconnects automatically can start another interval.

Snapshots return `image/jpeg` with `Cache-Control: no-store`. During a stream,
they reuse the latest frame when its settings match; a slow viewer can make
that cached image older than the request. Idle snapshots wake the sensor,
capture once, and return it to standby before sending the response. Snapshots
do not increment video FPS or frame counters. Each JPEG is limited to 512 KiB.

Stop, timeout, disconnect or capture failure releases the camera and restores
Wi-Fi minimum modem sleep after any outstanding capture/send completes. HTTP
stays reachable. This is camera standby, not ESP32 deep sleep. Sensor startup
allows three attempts with a 100 ms power-down interval before each attempt.
The firmware requires working PSRAM to start the camera services.

## HTTP interface and telemetry

| Method | Port | Path | Purpose |
| --- | --- | --- | --- |
| GET | 80 | `/` | Controls and MJPEG viewer |
| GET | 80 | `/webrtc` | JPEG WebRTC viewer |
| GET | 80 | `/health` | Service readiness JSON |
| GET | 80 | `/status` | Identity, settings, timing and measured telemetry |
| GET | 80 | `/capture` | One JPEG |
| POST | 80 | `/control` | Form: `resolution=VGA&quality=12` |
| POST | 80 | `/auto-off` | Form: `auto_off=true&auto_off_minutes=20` |
| POST | 80 | `/stop` | Request stop of the active video session |
| POST | 80 | `/webrtc/offer` | Complete data-channel SDP offer; returns SDP answer |
| POST | 80 | `/webrtc/close` | Close the WebRTC session |
| GET | 81 | `/stream` | Multipart MJPEG response |

A competing MJPEG viewer receives HTTP 503; WebRTC offers receive HTTP 409;
RTSP PLAY receives 453. In this firmware, RTSP 453 means that the video slot
is busy, not that Wi-Fi bandwidth was measured as insufficient.

The main page polls status every second while active and every five seconds
while idle. FPS and throughput measure complete successful sends over roughly
one-second windows; WebRTC counts acknowledged frames. These are not browser
rendering rates, and UDP send success does not guarantee delivery. Rates reset
after stop and expire after three seconds without a completed frame.

Status includes `node_name`, `camera_state`, `camera_error_code`, `webrtc_state`,
Wi-Fi RSSI, heap/PSRAM use, `internal_heap_free_bytes`,
`internal_heap_largest_bytes`, and the effective auto-off duration/countdown.
The timeout reason persists until the next video session. An initial capture
failure returns HTTP 500; inspect `camera_error_code` and UART for details.
`/health` reports service readiness, not a successful camera capture.

## openHAB

For a generic IP Camera Thing, configure `/capture` as `snapshotUrl` and the
full `http://CAMERA_IP:81/stream` URL as `mjpegUrl`. RTSP can be used as an
FFmpeg input when appropriate. Choose one upstream video connection and let
the host relay it to additional viewers; direct RTSP, MJPEG and WebRTC sessions
cannot run together. Snapshots can coexist with that upstream connection.
Account for auto-off when configuring continuous monitoring. Actual openHAB
integration remains untested.

## Validation and resource use

The validated build uses a 1500 KiB factory app partition at `0x10000` on 4 MB
flash, with no OTA slot. The latest measured application size is **1,383,392
bytes**, leaving **152,608 bytes (about 10%)** in that partition. Sizes can
change with toolchain and configuration. Three fixed JPEG buffers reserve
1.5 MiB of mapped PSRAM at startup, in addition to driver frame buffers.

On the tested board, WebRTC with mDNS candidates left approximately 48 KiB of
internal heap free while streaming, with a largest block around 46 KiB.
Internal free heap recovered to approximately 135 KiB after cleanup. Inspect
`/status` on your board; PSRAM capacity alone does not describe internal RAM
headroom for Wi-Fi and cryptography.

Validation completed on the OV2640 board:

| Check | Result |
| --- | --- |
| Host model, lifecycle, parser, concurrency and browser tests | Passed, including sanitizers and mocked peer lifecycle |
| Native ESP-IDF build and independent flash digest verification | Passed |
| MJPEG and snapshots, QVGA through UXGA | Original JPEGs decoded; concurrent snapshots and cleanup passed |
| HTTP/1.0 and HTTP/1.1 stream framing | JPEGs, Stop boundary and connection EOF passed |
| RTSP TCP and UDP, all five resolutions | FFmpeg decoding, snapshots, busy rejection and Stop passed |
| VLC RTSP and MJPEG | Decoder checks passed; playback confirmed by the user |
| Direct WebRTC | Real ICE/DTLS/SCTP, mDNS candidates, JPEG decoding and reconnect passed; Chrome live video confirmed |
| Auto-off | Host deadline tests and hardware live-policy changes passed; full-duration hardware expiry remains untested |
| openHAB | Integration untested |

To repeat the checks, run the following separately, with other viewers closed:

```bash
bash tools/test.sh
python3 tools/check_camera.py http://CAMERA_IP/
python3 tools/check_http_stream.py http://CAMERA_IP/
python3 tools/check_rtsp.py http://CAMERA_IP/
```

Host tests require GCC with GNU++26 support (tested with GCC 15), Node.js,
Python with Pillow, and FFmpeg. They use RTSP port 8554 and UDP ports 5004/5005
on the host. Firmware builds use the ESP-IDF cross-compiler; host fakes never
enter firmware include paths.

For the independent WebRTC client, create a host virtual environment:

```bash
python3 -m venv build/webrtc-test-env
build/webrtc-test-env/bin/python -m pip install aiortc Pillow
build/webrtc-test-env/bin/python tools/check_webrtc.py --mdns http://CAMERA_IP/
```

This virtual environment stays inside the ignored build directory. The WebRTC check
opens two connections, decodes original JPEGs, takes concurrent snapshots,
checks ownership rejection and reconnects. These automated checks do not
replace viewing the page in a real browser. Further implementation details
and historical evidence are in the [architecture](docs/ARCHITECTURE.md) and
[validation record](docs/VALIDATION.md).
