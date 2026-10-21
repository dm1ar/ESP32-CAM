# ESP32-CAM with ESP-IDF

FirstApp is a native ESP-IDF 6.1 / GNU++26 live camera application for the AI-Thinker
ESP32-CAM. It connects to Wi-Fi, reports its browser URL over UART, and serves
a configurable MJPEG live stream (VGA / quality 12 by default). The flash lamp stays off. Frames are
streamed without saving them to an SD card.

## Wi-Fi configuration

Before building, edit the local `wifi.toml` file:

```toml
[wifi]
node_name = "ESP32-CAM01"
auto_off = true
auto_off_minutes = 20
ssid = "W5QD34A"
password = "your-wifi-password"
```

Use a 2.4 GHz WPA2/WPA3-compatible network. The build validates the TOML file
and generates a header inside `build/private_config/`. Missing or invalid
credentials stop the build. Editing the TOML file triggers regeneration on
the next build, including builds from the Espressif extension.
`wifi.toml` and generated build files are excluded from Git. Credentials are
embedded in the firmware, so do not share firmware binaries or the generated
header. Passwords are not printed by the generator or application.

Give each camera its own `node_name` (for example `ESP32-CAM01`,
`ESP32-CAM02`) before building/flashing it. The name defaults to `ESP32-CAM01`
if omitted. Use 1–32 ASCII letters/digits with optional internal hyphens;
spaces, underscores, leading/trailing hyphens and dots are rejected. The name
is embedded in that firmware and used for the DHCP hostname, UART URL log,
browser heading/title and `/status` `node_name`. Router DNS behavior varies;
continue to read the actual DHCP URL from UART. A new build is required for
each camera's identity; credentials are preserved when changing only the name.

`auto_off` defaults to **true** and `auto_off_minutes` defaults to **20**.
The TOML duration must be a whole number from **5 to 120 minutes**, even when
automatic stop is disabled. These values supply the boot defaults.

The browser has an **Auto-off** checkbox and a **5–120 minute** slider. Toggling
the checkbox or releasing the slider saves immediately through `POST /auto-off`.
Changes are RAM-only and apply to the current MJPEG or RTSP session; reboot
restores the compiled TOML defaults. They do not require reflashing.

Timing stays anchored to the original stream start. Disabling removes the
active deadline; enabling or shortening to an already elapsed interval stops
the current stream. Changing camera resolution/quality does not reset it.
Once Stop/timeout has been requested, policy edits cannot revive that session;
start a new stream instead. Snapshot requests do not extend a deadline.
The browser displays remaining `MM:SS`, or **Auto-off disabled**. Cleanup waits
for an in-flight capture/send, then restores standby and Wi-Fi modem sleep.
Device enforcement does not depend on status polling.

`POST /auto-off` accepts exactly `auto_off=true&auto_off_minutes=20` (or false,
5–120 minutes); malformed/out-of-range requests return 400 without applying.
`/status` reports the effective `auto_off`, `auto_off_minutes`,
`stream_limit_seconds`, `stream_remaining_seconds` (zero idle/disabled), and
`stream_timed_out`. The timeout reason persists until a new stream starts.

## Browser stream

Verified on this network: **http://192.168.1.128/**. This is the current DHCP
address and can change after reboot or router changes.

Once the board boots and connects, the serial log prints
`Browser URL: http://<assigned-ip>/`. Open that address from a device on the
same network. Press **Start stream** to wake the camera and begin streaming;
the page initially leaves it in standby. The direct MJPEG endpoint is `http://<assigned-ip>:81/stream`;
`http://<assigned-ip>/health` reports server readiness. One stream viewer at
a time is supported; the page and health endpoint use a separate server.

Wi-Fi reconnects automatically, and a new DHCP address is printed if it
changes. Press Start stream after a connection loss.
The camera service has no authentication and is intended for your local
network. Actual frame rate depends on Wi-Fi and camera conditions.

The live status line refreshes once per second during streaming and every five
seconds while idle, and shows the sensor model,
resolution, JPEG format, measured sent frames per second, last JPEG size,
throughput, and streaming/idle state. Additional details show sensor PID,
JPEG quality, Wi-Fi RSSI, physical PSRAM and free mapped PSRAM, free heap,
uptime, total sent frames, capture errors, and ESP-IDF version.

`http://<assigned-ip>/status` exposes these readings as JSON. FPS and throughput
measure frames successfully written to the stream socket over roughly
one-second windows; they do not measure browser rendering speed. Both reset
to zero when the stream stops or no frames have been sent for three seconds.
The last frame's size and total sent frame count remain visible while idle.

## JPEG snapshots and shared frames

`GET http://<assigned-ip>/capture` returns one JPEG with `Content-Type: image/jpeg`
and `Cache-Control: no-store`, using the selected resolution and quality.
During streaming it copies the latest captured frame, without opening another
stream or changing the stream deadline. When idle it wakes the sensor, captures
one frame and powers down before sending the response. A failed capture returns
HTTP 500 and retains its error code in `/status`. Snapshot requests do not
increment stream FPS, throughput or frame counters.

A dedicated FreeRTOS capture worker owns the camera. It returns every driver
frame before network transmission. Three startup-allocated PSRAM buffers
(cache, stream response, snapshot response) isolate slow HTTP consumers.
Each JPEG is bounded to 512 KiB; larger frames return an error instead of
allocating more memory. One MJPEG viewer remains supported. Port 80 processes
one snapshot at a time; an idle snapshot may briefly delay other page requests
while the camera wakes. RTSP uses the same stream buffer and exclusive video session (see below).

For openHAB's generic IP Camera Thing, use `/capture` as `snapshotUrl` and
`:81/stream` as `mjpegUrl`. Let openHAB relay the stream to multiple viewers.
Its additional FFmpeg consumers should use that relay rather than opening a
second camera stream. These endpoints have not yet been tested in openHAB.
Consider the configured auto-off duration when choosing continuous monitoring.

To validate the board after flashing and reading its DHCP URL from UART:

```bash
python3 tools/check_camera.py http://<assigned-ip>/
```

This requires Pillow on the host, decodes original JPEGs at all five resolutions,
checks idle and simultaneous streaming snapshots, then restores the original
settings and stops the stream. On the replacement board, all five resolutions passed: 30 original snapshots
and 47 stream frames decoded, including simultaneous snapshot/stream access.
Standby cleanup and countdown preservation passed; actual openHAB integration
and full-duration auto-off expiry remain untested.

## RTSP for VLC and FFmpeg

The firmware provides a JPEG RTSP/RTP endpoint on TCP port 554:

```text
rtsp://<assigned-ip>:554/stream
```

For the replacement board's last observed DHCP address, this is
`rtsp://192.168.1.128:554/stream`. Confirm its current address on UART.
In a VLC build with Live555 RTSP support, use **Media > Open Network Stream**.
UDP unicast and interleaved TCP are supported. To force TCP in such a VLC build
or test with FFplay, use:

```bash
vlc --rtsp-tcp rtsp://192.168.1.128:554/stream
ffplay -rtsp_transport tcp rtsp://192.168.1.128:554/stream
```

JPEG is transported according to RFC 2435 (static RTP payload type 26,
90 kHz timestamps, explicit quantizers and fragments up to 1400 bytes).
The OV2640's baseline 4:2:2/4:2:0 images with standard Huffman tables are
supported; unsupported/malformed images cause a reported camera error.
No H.264 encoding or audio is provided. RTSP captures are paced at at most
10 frames/second; `/status` FPS and JPEG throughput measure actual complete
sends, excluding RTP/RTCP overhead. Temporary UDP buffer-pressure errors are retried with a bounded delay,
and TCP disables Nagle buffering for interleaved media. UDP counters measure successful sends,
not receipt at the viewer; UDP loss can spoil individual frames.

One active video session is allowed across RTSP and HTTP MJPEG. A competing
HTTP stream receives 503; RTSP PLAY receives 453 while HTTP owns the camera.
Only one RTSP control client is serviced at a time. Snapshots work during
either stream. For multiple viewers, let openHAB or another host relay one
camera connection. The web page remains available for settings, telemetry
and Stop. RTSP follows the existing selected resolution/quality and compiled
auto-off policy; snapshots and settings never extend the deadline.

OPTIONS, DESCRIBE, SETUP, PLAY, PAUSE, TEARDOWN and GET_PARAMETER are
implemented. PAUSE releases the camera; a later PLAY starts a new timed
session. Disconnect, HTTP Stop or auto-off releases the camera and returns
to standby. An idle control connection expires after 60 seconds without
control/RTCP activity. TCP uses negotiated interleaved channels; UDP uses
camera ports 5004/5005 and the client's negotiated RTP/RTCP ports. The SDK socket budget is 20 to cover both HTTP servers and RTSP/UDP. The UDP
destination is always the control connection's peer. Multicast and
authentication are not implemented. A client that automatically reconnects
can start another timed session after auto-off.

The installed Ubuntu VLC 3.0.23 was inspected and built with
`--disable-live555`. Its SAT>IP/RealRTSP fallbacks cannot play this generic
JPEG endpoint, and it does not recognize `--rtsp-tcp`. This was reproduced
against the production server on host sockets. Use the installed FFplay for
RTSP testing, or a VLC distribution with Live555 support. Firmware changes
cannot supply a missing client decoder/access module.

The existing `/capture` and HTTP MJPEG endpoints remain the simplest openHAB
integration. RTSP can be supplied as `ffmpegInput` when needed, but openHAB must not
hold direct RTSP and direct MJPEG connections simultaneously. Choose one
upstream video connection and relay it on the host; `/capture` can coexist
with either. Compatibility with an actual openHAB installation is untested.

Host socket tests run the production server and capture worker with synthetic
JPEGs: FFmpeg decodes TCP/UDP, and tests cover control framing, pause/resume,
reconnect, concurrent snapshots, busy ownership, Stop and a shortened deadline.
Hardware RTSP acceptance is pending. After flashing and reading DHCP from UART:

```bash
python3 tools/check_rtsp.py http://<assigned-ip>/
python3 tools/check_camera.py http://<assigned-ip>/
```

The first check requires FFmpeg and Pillow and tests all five resolutions over
TCP and UDP, simultaneous snapshots, busy HTTP rejection, actual FPS and Stop.
The second repeats HTTP MJPEG/snapshot acceptance. Both restore the original
settings and leave the camera in standby.

## Resolution, quality, and standby

Select QVGA (320x240), VGA (640x480), SVGA (800x600), XGA (1024x768), or
UXGA (1600x1200), and JPEG quality from 0 to 63, then press **Apply settings**.
Lower quality numbers produce better images and larger frames. Higher
resolution and image quality can reduce frame rate. Settings changes briefly
reinitialize the camera between frames; while idle, they are saved without
waking it. These settings reset to VGA / quality 12 after a reboot.

At boot, the camera remains powered down while Wi-Fi connects. Start stream
initializes it and switches Wi-Fi to full performance. Stop, closing the
viewer, or a stream error stops capture, frees camera buffers, disables its
clock, and asserts its power-down pin. Stream disconnect detection can take
several seconds because socket/capture operations have bounded timeouts.
Wi-Fi minimum modem sleep is then enabled while the HTTP server stays
reachable. This is camera standby with Wi-Fi power saving, not ESP32 deep sleep.
The browser can wake the camera again through the existing network connection.
Camera initialization allows three attempts with a 100 ms power-down interval
before each attempt to recover transient sensor communication failures.
If all attempts fail, the camera returns to standby; press Start stream to retry.

Status JSON includes `camera_state`, `wifi_modem_sleep`, `resolution`, and
`settings_pending`. Before the first stream, the camera model is identified
as expected rather than detected. `POST /control` takes form fields
`resolution=VGA&quality=12`; unsupported resolutions and qualities return 400.
`POST /stop` requests the active stream to stop.

## VS Code

The Espressif ESP-IDF extension is installed. Workspace settings select
`/home/rax/esp/v6.1/esp-idf`, target `esp32`, UART `/dev/ttyUSB0`, and
115200 baud. The IDE installation record and build wrapper use the dedicated
Python 3.12 environment at
`/home/rax/.espressif/python_env/idf6.1_py3.12_env`.
ESP-IDF 6.1 is installed from Espressif's stable `v6.1` tag; the previous
5.5.1 installation remains available for rollback.

Use **Terminal > Run Build Task** (`Ctrl+Shift+B`) to compile. Under
**Terminal > Run Task**, the project provides Flash, Monitor, and Menuconfig
tasks. The Espressif extension's Build, Flash, and Monitor commands are also
available once its setup is loaded. After changing SDK versions, run
**Developer: Reload Window**. If its status bar still shows an old
setup error, run **ESP-IDF: Select Current ESP-IDF Version**, select the
6.1 installation, and run **ESP-IDF: Doctor Command**.

The workspace uses ESP clangd 21 with the GCC driver include paths from the
compilation database. Reload the VS Code window after switching SDK/indexer
versions; `main/CMakeLists.txt` sets the actual GNU++26 compiler mode.

Debugging over JTAG requires a separate JTAG adapter; the SH-U06B is used
for UART flashing and monitoring.

## Command line

The wrapper activates the installed ESP-IDF and its Python 3.12 environment:

```bash
bash tools/idf.sh build
bash tools/idf.sh menuconfig
bash tools/idf.sh -p /dev/ttyUSB0 -b 115200 flash
bash tools/idf.sh -p /dev/ttyUSB0 monitor
```

Close CuteCom or other serial monitors before flashing or monitoring here.
Exit the ESP-IDF monitor with `Ctrl+]`.

## Flash and run

1. Use 3.3 V UART logic, common GND, adapter TX to U0R and RX to U0T.
   Supply the board through its 5V input with adequate power.
2. Connect GPIO0 to GND and press the board's reset button to enter download mode.
3. Run the full Flash task or command, including the partition table. Upload is configured to avoid automatic
   resets because reset/boot control lines have not been confirmed wired.
4. Remove the GPIO0-to-GND jumper after flashing. GPIO0 is also the camera clock.
5. Start the Monitor task, then press reset to run the firmware and see its logs.

Defaults enable 4 MB flash, DIO at 40 MHz, PSRAM at 40 MHz, and a PSRAM
memory test. If PSRAM is missing, startup continues and the app logs the
problem without attempting camera capture. Camera pins are in
`main/board.hpp` and match the supplied board datasheet.

## Dependencies and verification

The 2026-10-20 stream-start investigation found sensor configuration failures
and malformed JPEG scan markers on the original hardware. The replacement
board (MAC ending `56:74`) runs the same maintained I2C backend and 10 MHz
sensor clock successfully at `http://192.168.1.128/` (DHCP can change).
It passed ten start/stop cycles across QVGA through UXGA, with all 30 original
JPEG frames decoded and standby cleanup checked. This points toward the
previous hardware setup without isolating the failed component. See
`docs/VALIDATION.md` for evidence and the remaining full-duration auto-off check.
Camera errors are retained as `/status` `camera_error_code`, shown in the
browser, and cleared on the next start. An initial wake failure returns HTTP
500 rather than silently closing the stream connection.

`main/idf_component.yml` pins Espressif's camera driver to 2.1.7.
Keep `dependencies.lock` in version control. `sdkconfig.defaults` contains
the board defaults; `sdkconfig`, `build/`, and `managed_components/` are generated.
Changes to defaults do not override an already generated `sdkconfig`; use
Menuconfig to change an existing configuration.

On 2026-10-15, the firmware was refactored into C++ classes using GNU++26.
All seven first-party translation units compile with strict warnings treated
as errors, with exceptions and RTTI disabled. The camera driver and dependency
versions are unchanged. The standard 1500 KiB single-app partition replaces
the nearly full 1 MiB partition; use a full flash to update its partition table.
The factory app still starts at `0x10000`; no OTA partition is configured.

- [Architecture and Mermaid class, sequence and state diagrams](docs/ARCHITECTURE.md)
- [MISRA-oriented coding guidelines and documented deviations](docs/CODING_GUIDELINES.md)
- [Host coverage and pending board acceptance procedure](docs/VALIDATION.md)

Run `bash tools/test.sh` (GCC 15 with sanitizers and Node.js) or the VS Code
**ESP32-CAM: Host tests** task for host checks. `bash tools/idf.sh build`
builds the real firmware. Host tests and the ESP-IDF build passed. The app is
1,048,528 bytes with approximately 32% of its partition free. This refactor
was flashed successfully on 2026-10-15 at 115200 baud: bootloader, partition
table and application all passed write hash verification. The board was left
in bootloader mode; remove GPIO0-to-GND and reset to run. Runtime acceptance
for this refactor is still pending. Earlier hardware results
below describe the previous firmware. The source before the C++ conversion
is backed up in `/home/rax/esp/esp32-cam-before-cpp26`; the earlier SDK/config
backup remains at `/home/rax/esp/esp32-cam-before-idf61`.

The ESP32 ROM bootloader and 4 MB flash have been verified over UART.
FirstApp was built and flashed successfully on 2026-10-10. Runtime checks
confirmed the OV2640 camera, an 8 MB PSRAM chip (4 MB mapped, memory test
passed), Wi-Fi connection, HTTP 200 for the page and health endpoint, and
three successfully decoded 640 x 480 JPEG frames. The health endpoint also
remained responsive while streaming.
Playwright Chromium also verified that the page displays a 640 x 480 live
image, Stop halts it, Start reconnects it, and no page JavaScript errors occur.

References: [Espressif VS Code settings](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/settings.html)
and [Espressif camera driver](https://components.espressif.com/components/espressif/esp32-camera/versions/2.1.7/readme).

---



codex resume 01a13e7a-640b-7533-b683-67ec231917a2
