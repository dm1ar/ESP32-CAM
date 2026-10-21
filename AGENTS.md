# ESP32-CAM workspace

## Project

Develop firmware for an ESP32-CAM connected through a DSD TECH SH-U06B
USB-to-UART bridge. Read `Task.md` for the current task.

## Reference material

Reference PDFs are in `assets/`, not `../assets`:

- `assets/ESP32-Cam_datasheet.pdf`
- `assets/AZ273_A_19-4_DE_B089MDKBWG_266cf55e-51d0-4347-b6ab-66388a87589a.pdf`

The supplied documents describe the AI-Thinker ESP32-CAM: 4 MB flash,
4 MB PSRAM, OV2640 camera, and UART at 115200 baud. Confirm actual hardware
through the bootloader before relying on these specifications.

## Hardware conventions

- Use 3.3 V UART logic; the guide recommends supplying the board at its 5V pin.
- Adapter TX connects to U0R (GPIO3); adapter RX connects to U0T (GPIO1).
- Connect grounds. VCC on the ESP32-CAM is a 3.3 V output.
- GPIO0 connected to GND at reset selects download mode. The user reports
  that the board is currently jumpered for this mode.
- GPIO4 drives the flash lamp and is also an SD-card data pin.
- Remove the GPIO0-to-GND jumper and reset to run uploaded firmware.

## Tools and validation

No firmware, build configuration, or test suite exists at initialization.
PlatformIO (`pio`) and Python `serial` are installed; `esptool` is not available
in the default Python environment. Select and document a build workflow when
firmware work begins. Validate compilation before upload and distinguish host
USB enumeration from a successful ESP32 bootloader connection.

### FirstApp workflow

The user selected native ESP-IDF in VS Code. Use `bash tools/idf.sh build`,
with stable ESP-IDF 6.1 at `/home/rax/esp/v6.1/esp-idf` and its Python 3.12
environment at `/home/rax/.espressif/python_env/idf6.1_py3.12_env`. VS Code tasks
provide Build, Flash, Monitor, and Menuconfig. Use `/dev/ttyUSB0` at 115200 baud.
The 2026-10-15 GNU++26 refactor passed an ESP-IDF v6.1 build and sanitized
host tests. On 2026-10-15, the bootloader, new partition table and application
were flashed at 115200 baud with all three write hashes verified. The board
was subsequently started; the user confirmed the refactor is accepted and
running. Detailed agent-collected runtime acceptance measurements remain
unrecorded. First-party sources
are `.cpp`/`.hpp`; use `main/board.hpp` for board pins. Follow the local
MISRA-oriented guideline in `docs/CODING_GUIDELINES.md` (including documented
GNU++26/SDK deviations; no formal compliance claim). Mermaid diagrams are in
`docs/ARCHITECTURE.md`; board acceptance checks are in `docs/VALIDATION.md`.
Run `bash tools/test.sh` for firmware/model/browser host tests. Host fakes
must never enter firmware include paths. The app uses the standard 1500 KiB
single-app partition and has approximately 32% free space. Flash the complete
build, including the partition table. The source backup is at
`/home/rax/esp/esp32-cam-before-cpp26`; the previous SDK/configuration backup
is at `/home/rax/esp/esp32-cam-before-idf61`.

FirstApp serves a VGA MJPEG stream: the browser page is on port 80, `/health`
is on port 80, and `/stream` is on port 81. It joins the Wi-Fi network configured
in the local ignored `wifi.toml` file. Never print or commit that file's password.
`tools/wifi_config.py` validates it and generates a private header in the ignored
build directory. Credentials are embedded in firmware; do not share binaries.
Use `wifi.example.toml` as the safe template. Preserve `dependencies.lock`.

Flashing uses manual boot mode with no automatic reset. After upload, the user
must remove the GPIO0-to-GND jumper and reset; GPIO0 is also the camera clock.
Read the DHCP browser URL from UART rather than guessing the board's address.
See `README.md` for programming and operating instructions.

FirstApp was built, flashed, and run successfully on 2026-10-10. Current DHCP
URL: `http://192.168.1.123/` (can change). Hardware reports OV2640 and 8 MB physical
PSRAM, of which 4 MB is mapped; the PSRAM memory test passed. HTTP page and
health returned 200, and three VGA JPEG stream frames decoded successfully.

The browser also has a one-second live status display backed by `/status` JSON
on port 80. FPS and throughput measure successfully sent stream frames over
one-second windows; they reset on disconnect and expire after three seconds
without a frame. Shared stream statistics use a FreeRTOS critical section.
Do not substitute a configured or browser rendering frame rate for this metric.

Browser controls select QVGA/VGA/SVGA/XGA/UXGA and JPEG quality 0–63.
The embedded page lives in `main/index.html`. Settings are RAM-only, defaulting
to VGA / 12 after reboot. `POST /control` accepts resolution and quality;
`POST /stop` explicitly ends streaming. Camera hardware belongs exclusively
to the stream server task: reinitialize only between returned frame buffers.
Initialize on demand, retry up to three times with a 100 ms PWDN interval,
and deinitialize/assert PWDN after stop, disconnect, or failure. Idle Wi-Fi
uses `WIFI_PS_MIN_MODEM`; streaming uses `WIFI_PS_NONE`. HTTP remains reachable
in standby. Status polling is one second active and five seconds idle.

## Initial device-access findings (2026-10-10)

Host sysfs exposes USB VID:PID `067b:23a3` at `11-6.1`, bus 011 device 005,
with driver `pl2303` and serial device `ttyUSB0` (major/minor `188:0`).
The current execution sandbox exposes neither `/dev/ttyUSB0` nor `/dev/bus/usb`;
opening `/dev/ttyUSB0` fails with ENOENT and `lsusb` cannot initialize libusb.
USB/serial access must be provided by the session host before bootloader
communication can be verified. Recheck these findings each session; USB paths
and device numbers can change. Do not treat these findings as a hardware fault.

## Device access verified after permission change (2026-10-10)

With sandboxing disabled, `/dev/ttyUSB0` is accessible. ROM bootloader queries
at 115200 baud succeeded using the installed PlatformIO esptool v4.5.1:

```sh
python3 /home/rax/.platformio/packages/tool-esptoolpy@1.40501.0/esptool.py --port /dev/ttyUSB0 --baud 115200 --before no_reset --after no_reset --no-stub chip_id
python3 /home/rax/.platformio/packages/tool-esptoolpy@1.40501.0/esptool.py --port /dev/ttyUSB0 --baud 115200 --before no_reset --after no_reset --no-stub flash_id
```

Confirmed ESP32-D0WDQ6-V3 revision v3.1, 40 MHz crystal, MAC
`a0:dd:6c:79:ae:18`, and 4 MB SPI flash (manufacturer `c4`, device `6016`).
PSRAM and camera remain unverified. No firmware was written; the chip was left
in bootloader mode. The bridge now reports USB location `11-4`; rediscover it
instead of assuming the initial USB path. The unversioned `tool-esptoolpy`
installation fails to import `intelhex`; use the working version above.

## Node identity and stream limit

`[wifi].node_name` in local ignored `wifi.toml` defaults to `ESP32-CAM01`.
It is a 1–32 character ASCII hostname label (letters/digits, internal hyphens)
and identifies the DHCP hostname, UART URL log, browser and `/status`. Use a
unique name and rebuild for each camera. Preserve the local password.

`[wifi].auto_off` defaults to true; `auto_off_minutes` defaults to 20.
The generator validates these and embeds constants when compiling.
`Network::streamPolicy()` supplies `StreamPolicy` to `SharedState` at startup.
Enabled streams have a device-monotonic deadline set by `SharedState::begin()`;
disabled auto-off has no deadline, and manual Stop/disconnect cleanup remains. Applying settings never extends it. `shouldStop(now)`
requests Stop under the spinlock; only the stream task performs camera cleanup
after an outstanding frame/send returns. Status includes `node_name`,
`auto_off`, `auto_off_minutes`, `stream_limit_seconds`,
`stream_remaining_seconds` (zero idle/disabled) and `stream_timed_out`.
The browser shows an MM:SS countdown and explicit auto-stop message. Timeout
reason persists until the next stream. This feature build passed tests and
compilation, and was flashed at 115200 baud with application hash verification.
It was left in bootloader; remove GPIO0-to-GND and reset to run. A full-hour
hardware check of the timeout remains pending.

The configurable auto-off build (2026-10-20) passed host tests and compilation;
flashing then succeeded at 115200 baud after manual boot entry. Fast-reflash
verification failed once and the full-image fallback succeeded. A separate
verify-flash confirmed the application digest at 0x10000. The board was left
in bootloader; remove GPIO0-to-GND and reset to run. Runtime checks remain
pending. The browser derives its duration/disabled text
from status JSON; do not hard-code a 60-minute timeout. Duration must be a
whole number of minutes from 1 to 71,582,788; `auto_off` must be a real TOML
boolean. Preserve local credentials when editing the configuration.

## Stream-start investigation (2026-10-20)

Direct HTTP/UART reproduction found OV2640 initialization failing at SCCB
register 0x35/value 0xda, return 0x108, after sensor detection. The stream
timeout was not reached. A candidate build selects
`CONFIG_SCCB_HARDWARE_I2C_DRIVER_LEGACY=y` in defaults and sdkconfig; ESP-IDF
6.1 still provides it but marks it EOL (coding deviation D08). No managed
dependency files or lock were changed. This candidate was flashed with
hash verification and independently verified at 0x10000. Hardware repeated
wake validation remains pending after removing GPIO0-to-GND and resetting;
do not claim the compatibility fix proven yet.
`camera_error_code` in status persists after failure and clears on begin;
initial wake failure now sends HTTP 500 and the browser shows its error code.

The legacy comparison was subsequently tested and failed on the first VGA
start and in a resolution sweep; do not present it as a working fix. Source
has been restored to the maintained SCCB backend and `board.hpp` now selects
a 10 MHz XCLK timing candidate (build/tests pass, not uploaded). The running
board still has the failed legacy comparison image. A cold power cycle with
GPIO0 ungrounded and all browser camera tabs closed has been requested before
further upload. Sensor write failures remain unresolved.

A subsequent full power cycle allowed initialization, but captured JPEGs
contained FF D2 instead of the FF DA SOS marker at offset 609. A host-only
diagnostic copy with byte 610 corrected decoded as 640x480; production code
does not repair JPEG bytes. Hardware/timing/configuration cause is unproven.
The 10 MHz maintained-backend candidate now logs configured XCLK on wake;
manual boot entry has been requested for upload and unmodified-frame tests.

Latest state: the 10 MHz maintained-backend candidate has now been flashed
and independently verified at 0x10000 (1,051,696 bytes). The board was left
in bootloader and GPIO0 removal/reset were requested. Unmodified-frame
decoding and repeated-start validation are pending. This supersedes earlier
notes saying the 10 MHz candidate is unflashed or the legacy image is current.


Latest runtime result (2026-10-20): the 10 MHz maintained-backend firmware
boots at http://192.168.1.127/ and initializes the OV2640, but original JPEG
decoding still fails. Three frames retain FF D2 instead of FF DA at offset
609; host-only marker correction decodes 640x480. No byte repair is in
firmware. Status confirms auto-off has not expired (1198 seconds remaining).
Repeated-start acceptance stopped on the first failure. Stream was stopped
after testing. The timing trial is not a fix; physical ribbon/power checks
are the next diagnostic step. This supersedes pending-runtime notes above.


Replacement-board upload (2026-10-20): bootloader confirmed ESP32-D0WDQ6-V3
revision 3.1, MAC a0:dd:6c:79:56:74, 40 MHz crystal and 4 MB flash
(manufacturer c4/device 6016). This differs from the previous board's MAC.
The same maintained-backend 10 MHz candidate was built and flashed at
115200 baud with no automatic reset. Bootloader, partition table and
application write hashes passed; independent verify-flash at 0x10000 matched
all 1,051,696 application bytes. Board left in bootloader awaiting manual
GPIO0 removal/reset. Camera, PSRAM, DHCP address and JPEG decoding on this
replacement remain unverified. Do not reuse the previous DHCP address as
proof of this replacement's address.


Replacement-board runtime acceptance (2026-10-20): UART DHCP reports
http://192.168.1.128/ for ESP32-CAM02 (MAC a0:dd:6c:79:56:74). The same
10 MHz maintained-backend firmware passed all ten sequential start/stop
cycles: five VGA, then QVGA, VGA, SVGA, XGA and UXGA. Three original
JPEGs per cycle decoded completely with Pillow at the selected dimensions
(30 frames total, no marker repair). Every cycle returned camera_state to
standby, camera_error_code to 0, FPS to 0 and Wi-Fi modem sleep to true.
Free mapped PSRAM stabilized at 4,191,356 bytes; post-cycle free heap
stabilized around 4,384,539 bytes. UART confirms configured XCLK 10000000 Hz.
Socket reset warnings coincide with deliberate test client disconnects;
no initialization failure was observed. Final settings restored to VGA/12,
stream stopped. Auto-off is enabled at 20 minutes; full-duration hardware
expiry is not validated by these short tests. Boot reported 8 MB physical
PSRAM (4 MB mapped). Passing this replacement with the same image points
toward the previous hardware setup, but does not isolate board, sensor,
ribbon, connection or power as the cause. Session-local evidence:
/tmp/esp32-cam-stream-recovery-test.json and its -uart.log companion.
This supersedes the replacement's pending-runtime notes above.


Snapshot/shared-frame implementation (2026-10-20, branch rtsp): adds
GET /capture on port 80, image/jpeg with no-store. Camera ownership after
boot moves from port-81 HTTP to a dedicated camera_owner FreeRTOS worker.
Session begin/finish and capture are serialized through a static request queue;
three bounded 512 KiB startup PSRAM allocations hold latest, stream and
snapshot copies. Driver frames return before HTTP sends. Active snapshots
reuse the latest image at matching settings; idle snapshots wake, capture and
return to standby before sending. Snapshots do not extend auto-off or enter
stream FPS/counters. HTTP 500 on invalid/oversize/capture failure. D09 records
the bounded allocation exception. One direct MJPEG viewer remains supported;
RTSP is not implemented. Sanitized production FrameStore lifecycle/copy tests,
browser/config tests and the ESP-IDF build pass. tools/check_camera.py provides
original-JPEG tests across all resolutions, idle snapshots and concurrent
stream/snapshots. Upload and hardware validation of this change are pending.
This supersedes earlier instructions assigning camera ownership to port 81.


Snapshot firmware upload: bootloader confirmed replacement MAC
a0:dd:6c:79:56:74. Complete IDF flash at 115200 baud succeeded; unchanged
bootloader/partition content was verified and application write hash passed.
Separate verify-flash matched all 1,053,520 application bytes at 0x10000.
Board left in bootloader; GPIO0 removal and reset requested before the
new tools/check_camera.py simultaneous snapshot/stream hardware test.


Snapshot hardware acceptance (2026-10-20): after manual reset, UART DHCP
confirmed ESP32-CAM02 at http://192.168.1.128/. tools/check_camera.py passed
all five resolutions (QVGA/VGA/SVGA/XGA/UXGA): 30 original JPEG snapshots
(10 idle, 20 during streaming) and 47 original MJPEG frames decoded completely
with correct dimensions. Snapshot responses returned image/jpeg and no-store.
Idle snapshots did not increment stream counters; simultaneous snapshots kept
streaming active without extending the countdown. Each cycle restored standby,
zero FPS, modem sleep and camera_error_code=0. Final HTTP status confirms
VGA/12, standby and no camera error. Runtime log:
/tmp/esp32-cam-snapshot-runtime.log; boot log:
/tmp/esp32-cam-snapshot-boot.log (session-local evidence). Firmware remains
running with auto-off enabled at 20 minutes. Full-duration expiry and actual
openHAB integration remain untested. Snapshot hardware acceptance is now
passed, superseding the pending-upload/runtime notes above.


RTSP implementation (2026-10-20, branch rtsp): adds
rtsp://<DHCP-address>:554/stream using baseline JPEG over RFC 2435 RTP, UDP
unicast (camera ports 5004/5005) or interleaved TCP. OPTIONS/DESCRIBE/SETUP/
PLAY/PAUSE/TEARDOWN/GET_PARAMETER and RTCP SR/SDES/BYE are implemented.
One RTSP control client at a time; one active video owner across MJPEG/RTSP.
Worker begin atomically rejects competing video claims, and rejected HTTP
clients do not clean up the existing owner. Snapshots remain available.
Existing auto-off and HTTP Stop apply; PAUSE/resumed PLAY starts a new interval.
Fixed request/packet buffers, strict range checks, standard JPEG Huffman
table fingerprints, explicit quantizers, at most 10 capture requests/s.
Status counts actual complete frame sends (UDP delivery is not guaranteed).
Host sanitizers, parser/packetizer tests, and production server/worker POSIX
socket tests with independent FFmpeg TCP/UDP decoding pass, including
pause/resume, reconnect, busy ownership, snapshots, Stop and shortened
auto-off. ESP-IDF build passes (31% app partition free). No dependencies or
credentials changed. Hardware RTSP acceptance/upload remain pending;
tools/check_rtsp.py tests all resolutions and concurrency with actual FFmpeg,
then tools/check_camera.py repeats HTTP regression. This supersedes earlier
notes saying RTSP is not implemented. No authentication or multicast is added.


RTSP upload: replacement MAC a0:dd:6c:79:56:74 confirmed through ROM.
Complete IDF flash at 115200 baud passed write hash verification; separate
verify-flash matched all 1,067,456 application bytes at 0x10000. Board left
in bootloader, manual GPIO0 removal/reset required. RTSP hardware acceptance
and HTTP snapshot/MJPEG regression remain pending.


First RTSP hardware check: DHCP 192.168.1.128 and QVGA TCP passed independent
FFmpeg decoding (3 frames). The following UDP SETUP returned HTTP-style
RTSP 500 before capture; acceptance stopped and cleanup restored standby.
SDK inspection shows each HTTP server consumes three infrastructure sockets
(listener/control/message), so default CONFIG_LWIP_MAX_SOCKETS=10 cannot
cover both servers, HTTP clients, RTSP control and UDP RTP/RTCP. The candidate
raises the budget to 20 (up to 16 configured sockets plus headroom) and adds
UDP allocation/configuration errno logging. This candidate requires another
flash; UDP and full hardware acceptance are not yet passed.


Socket-budget candidate upload: CONFIG_LWIP_MAX_SOCKETS=20 is compiled.
Manual boot connection to replacement MAC a0:dd:6c:79:56:74 succeeded after
CuteCom released the port and the user reset the board. Full IDF flash at
115200 baud passed write hashes; independent verify-flash matched 1,067,792
application bytes at 0x10000. Board remains in download mode awaiting
GPIO0 removal/reset for repeated RTSP UDP/TCP and HTTP acceptance.


Socket-budget hardware result: QVGA TCP and UDP each decoded three frames,
confirming UDP SETUP now succeeds. A later VGA/default-FFmpeg run timed out;
a bounded-probe TCP run decoded three original VGA frames with no decode
errors. A direct UDP receiver saw three complete frames/28 packets with no
sequence gaps before the server closed (camera error 0, RSSI -84 dBm). Full
acceptance therefore remains failed/incomplete. A new candidate adds bounded
UDP transient-send retries and fatal errno/poll logging, TCP_NODELAY and SDP
framerate ceiling. Espressif documents delayed retries for UDP ENOMEM:
https://docs.espressif.com/projects/esp-idf/en/release-v5.4/esp32/api-guides/lwip.html
These are candidate remedies, not yet hardware-proven. SDK build passes;
host socket tests now inject ENOMEM. Another upload/runtime check is needed.


2026-10-21 live policy / RTSP correction: browser Auto-off checkbox and
5–120 minute slider save immediately to POST /auto-off. Runtime policy is
RAM-only, starts at compiled TOML defaults (true/20 by default), and remains
anchored to original stream start. Disable removes deadline; enabling or
shortening past elapsed time stops; policy edits cannot resurrect Stop.
TOML range is now also 5–120 minutes; credentials are preserved.
Read /home/rax/Dokumente/cam.log: normal MJPEG socket-close warning 104,
followed by repeated RTSP connects/disconnects before camera wake. Local
VLC 3.0.23 configuration explicitly disables Live555 and falls back to
SAT>IP/RealRTSP, reproducing failure against our host server; --rtsp-tcp is
not available in this build. Use FFplay or RTSP-enabled VLC to validate.
The candidate also includes bounded UDP ENOMEM/etc retries, TCP_NODELAY,
20 sockets and SDP framerate ceiling from the prior investigation. Sanitized
model/browser/config/RTSP tests including injected ENOMEM pass; SDK build
passes. New hardware acceptance remains pending. Console cam.log is ignored.


Live-policy/RTSP candidate flashed to replacement MAC a0:dd:6c:79:56:74
at 115200 baud. Complete IDF flash passed write hashes; independent verify
matched all 1,071,712 application bytes at 0x10000. Board left in bootloader.
GPIO0 removal/reset and FFmpeg plus HTTP regression checks are pending.
