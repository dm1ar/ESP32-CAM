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
These `[wifi]` values are read when CMake generates the private header and are
embedded into the firmware; changes require rebuilding and flashing. Older
files without these keys get the same defaults. `auto_off` must be a TOML
boolean and the duration a whole number of minutes from 1 to 71,582,788
(the upper bound keeps seconds representable as a 32-bit unsigned value).
The duration is validated even when automatic stop is disabled.

With `auto_off = true`, every stream stops after the configured interval,
including the initial wake period. A fresh Start gets the full interval;
Apply settings never extends the deadline. The status bar shows remaining
`MM:SS` from device time and a message containing the configured duration on
expiry. With `auto_off = false`, it shows **Auto-off disabled**, with no timed
stop; manual Stop, disconnect and error cleanup still work. Device enforcement
does not depend on browser polling. Cleanup waits for any in-flight capture
or send to return, then restores standby and Wi-Fi modem sleep.
`/status` exposes `auto_off`, `auto_off_minutes`, `stream_limit_seconds`,
`stream_remaining_seconds` (zero idle/disabled) and `stream_timed_out`
(retained until the next stream starts). The configured seconds remain visible
when disabled; use `auto_off` to distinguish unlimited streaming from expiry.

## Browser stream

Verified on this network: **http://192.168.1.123/**. This is the current DHCP
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
