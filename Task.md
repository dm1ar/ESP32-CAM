# # ESP32-CAM

We create Firmware for the esp32-cam module connected through an SH-U06B serial bridge from DSD TECH.

## Init

read the assets/ reference documents
the esp32-cam is jumpered into BOOT-mode
it is coonected to: Bus 011 Device 005: ID 067b:23a3 Prolific Technology, Inc. ATEN Serial Bridge

enable USB device connection for codex

check, if the module is accessible.

## FirstApp

initialize wifi connection to SSID: W5QD34A
let me set the WIFI PW in a wifi.toml file before building

make FirstApp to stream a live stream to a webbrowser on my network
and give the link address.

### Live camera status

Add a live status line to the browser page, refreshed once per second:

- Camera model, resolution, image format, and measured frames sent per second.
- Last JPEG size, stream throughput, and streaming/idle state.
- Sensor PID, JPEG quality, Wi-Fi signal strength, physical PSRAM and free
  mapped PSRAM, free heap, uptime, total frames sent, capture errors, and ESP-IDF version.

Expose these readings as JSON at `/status`. Measure frame rate from successfully
sent stream frames, not a configured target; report zero after stopping or
when frames stop arriving. Keep status polling responsive during streaming.

Build with ESP-IDF, flash via `/dev/ttyUSB0` while the board is in boot mode,
then remove the BOOT jumper and reset for browser verification.

Implementation status (2026-10-10): live status UI and `/status` JSON are
implemented; ESP-IDF build and flash verification passed. The board now boots
and connects successfully. Live browser checks confirmed measured FPS,
resolution changes through UXGA, responsive status, and idle zero FPS.

### Browser controls and standby

- Add resolution selection: QVGA (320x240), VGA (640x480), SVGA (800x600),
  XGA (1024x768), and UXGA (1600x1200).
- Add JPEG quality selection from 0 to 63, with lower numbers meaning higher
  image quality. Apply changes safely between frames, or retain them while idle.
  Settings are held in RAM and reset to VGA / quality 12 on reboot.
- Start with the camera in standby. Start stream must wake it from the browser.
- After Stop, capture failure, or stream disconnect, deinitialize camera capture,
  release buffers, disable its clock, and assert the camera power-down pin.
- Enable Wi-Fi minimum modem sleep while idle; disable it during streaming.
  Keep Wi-Fi associated and HTTP reachable so browser wake works.
- Show camera state and Wi-Fi power-saving state in live status. Poll status
  every second during streaming and every five seconds while idle.
- Verify setting changes, standby memory release, repeated browser wake, and
  returning to standby when the browser closes.
- Allow up to three camera initialization attempts, with a 100 ms power-down
  interval before each attempt, to recover transient sensor I²C errors observed
  during repeated wake testing. Keep the camera in standby if all attempts fail.

Implementation status (2026-10-10): controls and standby are implemented.
Live Chromium checks verified streaming at every listed resolution, quality
changes, mobile layout, Stop, buffer release, and Wi-Fi modem sleep. Repeated
wake testing found intermittent sensor I²C failures. Recovery firmware built
without compiler warnings and was flashed with hash verification. Its final
repeated-wake and browser-close checks remain pending: after upload, no serial
boot output was received and the previous browser address was unreachable.
Check power, remove GPIO0-to-GND, and reset before continuing verification.

## Refactoring to current stable ESP-IDF

refactor the complete code using esp-idf v6.1 on vscode in gnu++26
use a coding guideline according to current MISRA C/C++

include ample comments in english in the code base

generate a class diagram of the finishe code in Mermaid

generate further uml diagrams in mermaid if applicable


Implementation status (2026-10-15): complete first-party firmware refactor
into Application, Network, Camera, WebServer and SharedState classes using
ESP-IDF v6.1 / GNU++26. Scoped frame/session/lock guards enforce resource
ownership. The browser controller is readable and commented. The local
MISRA C++:2023 (TC1:2026)-oriented guideline and GNU++26/SDK deviation record
are in docs/CODING_GUIDELINES.md; this is not a formal compliance claim.
Mermaid class, sequence and state diagrams are in docs/ARCHITECTURE.md.
Host tests and ESP-IDF compilation passed. The standard 1500 KiB single-app
partition provides headroom. On 2026-10-15, full flashing succeeded at
115200 baud with bootloader, partition table and application hashes verified.
The user subsequently confirmed that the flashed code is accepted and running.
Detailed agent-collected runtime acceptance measurements remain unrecorded.

## Node identity and automatic stream stop

Add `[wifi].node_name` to `wifi.toml`, defaulting to `ESP32-CAM01`, so each
camera can be built/flashed with its own identity. Use it for the DHCP
hostname, UART URL log and browser/status identification. Preserve credentials.

Automatically stop each stream after 60 minutes and show its remaining time
in the browser status bar. Use device-monotonic time and retain the deadline
across settings changes. Enter normal standby on expiry; a new explicit Start
gets a fresh hour. Expose remaining time and timeout reason in `/status`.

Implementation status: implemented; sanitized firmware/browser/config host
tests and ESP-IDF v6.1 compilation pass. This feature build was flashed
at 115200 baud with application hash verification; the board was left in
bootloader. Remove GPIO0-to-GND and reset to run. A real full-hour hardware
run remains pending.


## Configurable automatic stream stop

Add `[wifi].auto_off = true` and `auto_off_minutes = 20` to `wifi.toml` and
its safe template. Generate compile-time firmware settings from these values.
Disabled auto-off permits unlimited streaming with ordinary Stop/disconnect
cleanup; enabled auto-off uses the configured duration and countdown.

Implementation status (2026-10-20): implemented; sanitized firmware, browser
and generator host tests pass. ESP-IDF v6.1 build passed. Flashing succeeded
at 115200 baud after entering manual boot mode. The fast-reflash verification
failed once and esptool retried the full application successfully. A separate
verify-flash confirmed all 1,051,040 application bytes at 0x10000 match the
build. The board was left in bootloader; remove GPIO0-to-GND and reset to run.
Runtime validation of the configurable policy remains pending.

## Stream-start failure investigation

2026-10-20: direct HTTP requests plus UART reproduced repeated OV2640
initialization failures on ESP32-CAM02. SCCB register writes fail with
0x108 and the camera driver cannot set frame size. Auto-off did not expire.
A compatibility candidate selects the pinned camera component's legacy SCCB
backend under ESP-IDF 6.1 (SDK EOL note retained; see D08). Error reporting
now retains camera_error_code, returns HTTP 500 for initial wake failure,
and displays the error in the browser. Host tests and compilation passed;
the compatibility build was then flashed with write hash verification and
independent verify-flash at 0x10000. Runtime repeated-start validation is
pending removal of GPIO0-to-GND and reset into the application.


2026-10-20 hardware result: the legacy SCCB comparison failed on the first
start and across the resolution sweep. It is not a verified fix. Source
selection is restored to the maintained backend, with an unflashed 10 MHz
input-clock timing candidate built and host-tested. Before another upload,
a full power cycle with GPIO0 disconnected and browser tabs closed has been
requested. The board currently still runs the failed legacy comparison image.

Cold-power-cycle result: initialization can succeed, but returned JPEGs have
FF D2 instead of the FF DA scan marker at offset 609. A diagnostic host-only
marker correction allowed a complete 640x480 decode; no repair is applied in
firmware. The cause remains unproven. The 10 MHz/new-backend candidate passes
host tests and build and logs configured XCLK; manual boot entry has been
requested to flash it and test original JPEG output.


The 10 MHz/new-backend timing candidate was flashed successfully at 115200
baud. Write hash verification and a separate verify-flash of the complete
1,051,696-byte application at 0x10000 passed. The board was left in
bootloader; removal of GPIO0-to-GND and reset have been requested before
unmodified JPEG decoding and repeated-start validation. Do not claim fixed
until the runtime checks pass.


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
