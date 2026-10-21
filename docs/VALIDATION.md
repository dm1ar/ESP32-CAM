# Refactor validation

## Immediate stream failure investigation (2026-10-20)

Reproduced on running ESP32-CAM02 at 192.168.1.127 with UART capture and
direct HTTP requests, independently of the browser. Of three consecutive
starts, the first returned five JPEG frame endings; starts two and three
closed before HTTP response headers. Status reported camera `error`, zero
capture errors, and `stream_timed_out=false`.

UART showed the sensor being detected, followed by SCCB writes failing at
sensor address 0x30, register 0x35, value 0xda, return 264 (0x108,
`ESP_ERR_INVALID_RESPONSE`). One attempt additionally logged a software
I2C bus timeout. The camera driver then reported failure to set frame size
and exhausted all three initialization attempts. The evidence locates the
failure in camera wake/configuration, not the compiled auto-off interval.
It does not by itself distinguish driver compatibility from electrical
or sensor timing issues.

The legacy-backend comparison was flashed at 115200 baud and independently
verified at 0x10000. It failed the first cold MCU-start VGA request with
HTTP 500 and `camera_error_code=-1`; all three initialization attempts failed
at register 0x35/value 0xda. Follow-up requests at all supported resolutions
also failed. UXGA additionally showed failure at register 0x23/value 0x00.
This does **not** validate the legacy-backend workaround, and the source has
been reverted to the maintained backend. The legacy comparison was subsequently replaced by the 10 MHz maintained-
backend timing candidate described below.

The next timing candidate changes only the sensor input clock from 20 MHz
to 10 MHz relative to the maintained-backend build. Host tests confirm the
clock configuration reaches the driver, and firmware compilation passed;
this candidate is not yet uploaded or hardware-proven. A full power cycle
with GPIO0 ungrounded and other viewer tabs closed has been requested first,
to distinguish a persistent powered-sensor state from an MCU reset and avoid
multiple-client interference. Do not attribute the fault conclusively to
hardware, firmware or supply until these checks are complete.

The available diagnostic script decodes actual JPEGs with Pillow and checks
standby state, rates and memory after Stop. Its planned acceptance run is
five repeated VGA starts followed by QVGA/VGA/SVGA/XGA/UXGA, three decoded
frames each. No successful ten-cycle result has been obtained.

Independently, error reporting is corrected: an initial camera wake failure
returns HTTP 500 instead of a silent connection close, status retains
`camera_error_code`, and the browser shows that code after image failure.
The next session clears it. Host tests cover error retention, retry reset,
and browser error presentation. Host tests and the ESP-IDF build pass.

## Recorded results (2026-10-15)

The user confirmed that the flashed C++ refactor is accepted and running.
The later node-name/countdown changes have passed host tests and ESP-IDF
compilation and were uploaded at 115200 baud with application hash
verification. The board was left in bootloader; remove GPIO0-to-GND and reset
to run. A real full-hour timeout check remains pending. Their current app size is
1,050,352 bytes (approximately 32% partition headroom). Historical refactor
measurements and flash verification below describe the preceding build.

| Check | Result |
| --- | --- |
| `bash tools/idf.sh fullclean build` | Passed with ESP-IDF v6.1 and GCC 15.2.0; no compiler warnings/errors. SDK component-validation CMake warnings remain upstream. |
| Actual compiler commands | All seven first-party `.cpp` sources use `-std=gnu++26`, strict warnings, no exceptions and no RTTI. |
| Sanitized firmware host tests | Passed parser, rate, state/concurrency and injected camera lifecycle checks. |
| Browser controller host tests | Passed commands, rendering, polling, stale response and failure checks. |
| ESP clangd 21.1.3 | All seven first-party translation units parsed with zero errors, using GCC builtin headers and disabled development tweak tests. |
| Mermaid parser | Class, sequence and state diagrams all parsed successfully. |
| Firmware/partition | 1,048,528-byte app in 1,536,000-byte factory partition; 487,472 bytes (32%) free. |
| Flash | Full flash at 115200 baud passed: bootloader at 0x1000, partition table at 0x8000 and application at 0x10000; all three write hashes verified. ESP32-D0WDQ6-V3 revision v3.1 confirmed. |
| Refactor runtime | User confirmed the preceding flashed C++ firmware is accepted and running; detailed acceptance measurements were not collected by the agent. |

## Reproducible host checks

Run from this workspace:

```sh
bash tools/test.sh
bash tools/idf.sh build
```

Host requirements are GCC with GNU++26 support, the address/undefined-behavior
sanitizer runtimes, pthreads, and Node.js. VS Code provides **ESP32-CAM: Host
tests** under Run Task. Executables are written to ignored `build/host-tests/`.
No host tests read `wifi.toml`, communicate with the board or use real camera
frames. The ESP-IDF build validates the private Wi-Fi configuration locally.

The host suite runs the actual production model, shared-state and camera
modules. Host SDK fakes are isolated under `tests/fakes/` and are never on
the firmware's include path. Coverage includes:

- All 320 supported resolution/quality combinations; reordered fields,
  quality boundaries, duplicate/unknown fields, incomplete/malformed input,
  huge numeric strings, embedded NULs, and unchanged output on rejection.
- One-second FPS/byte measurement, slow frames, rate reset, idle zeroes and
  exact three-second stale expiry.
- Idle selection, pending settings while streaming, applied settings, Stop,
  capture error state, restart and 40,000 concurrently updated frame records
  with coherent snapshots using a host mutex.
- Three bounded camera attempts, 100 ms PWDN intervals, unchanged-setting
  optimization, frame return, reinitialization only after return, missing
  frame handling, final PWDN after retry exhaustion and later wake recovery.
- Actual embedded browser controller with a fake DOM, HTTP responses and
  timers: commands, rendering measured FPS, unsaved edits, active/idle polling,
  out-of-order responses, HTTP failures and restored command buttons.
- Node-name generation defaults/custom values and hostname validation, using
  dummy credentials only; invalid values are rejected without exposing input.
- The default 1200-second and custom 420-second stream deadlines using injected monotonic timestamps,
  exact boundary, partial seconds, settings changes, manual Stop, idle zero
  and a fresh configured interval on restart; disabled auto-off remains active
  after seven simulated days and still accepts manual Stop. Browser tests cover node identification,
  `20:00`/`07:00`/`01:01`/`00:01` display, configured timeout messages, disabled
  display, automatic stop and stale timeout status. Generator tests reject
  strings/numbers for the boolean and invalid/fractional/overflow durations.

Compiler warnings are not a substitute for a MISRA assessment. Host mutexes
and driver fakes do not reproduce ESP32 scheduling, electrical behavior,
real Wi-Fi or actual MJPEG browser decoding.

## Board acceptance procedure (pending)

Use a stable 5 V supply, common ground, and 3.3 V UART logic. Only flash when
the user has requested it and the board is in manual boot mode. Use the full
Flash task to include the new partition table. Remove GPIO0-to-GND and reset
after upload; GPIO0 becomes the camera clock.

1. Read UART at 115200 baud. Confirm IDF v6.1, PSRAM availability, sensor PID
   on first stream, and the actual DHCP URL. Do not assume the historical IP.
2. Load `/`, `/health`, and `/status` on port 80. Expect HTTP 200, camera
   standby, VGA/12, zero rates and minimum Wi-Fi modem sleep before Start.
3. Start the port-81 stream. Decode at least three JPEG frames at each QVGA,
   VGA, SVGA, XGA and UXGA setting. Apply quality 0, 12 and 63. Confirm dimensions,
   selected/applied settings and status responsiveness while streaming.
4. Confirm frames/throughput reflect actual transmissions over one-second
   windows. Stop: rates must become zero, mapped PSRAM/heap must recover,
   PWDN must be high, XCLK inactive and minimum modem sleep enabled.
5. Repeat Start/Stop at least ten times; change settings during wake and
   streaming. Confirm no heap/PSRAM decline across idle checkpoints and no
   reinitialization with outstanding buffers. Check failures recover within
   three tries and leave the camera asleep if exhausted.
6. Close the browser midstream and interrupt Wi-Fi. Confirm capture resources
   are released after socket/capture timeout; reconnect and start again.
   Check `/status` expires stale rates after three seconds without a frame.
7. POST malformed, missing, duplicate and out-of-range control fields.
   Expect HTTP 400 and unchanged settings. Try an incomplete request body;
   it must time out without blocking camera ownership or corrupting state.
8. Reboot. Confirm RAM settings reset to VGA/12, camera starts in standby,
   the lamp stays off, and the browser remains usable on a narrow screen.
9. Build/flash distinct `node_name` values for separate cameras. Confirm each
   DHCP hostname, UART identity, browser title and `/status` name match its
   configuration. Changing the name must preserve Wi-Fi credentials.
10. Run a stream for the compiled interval (20 minutes by default): remaining time must count down without resetting
    when changing resolution/quality. Confirm automatic standby, zero rates,
    released buffers, modem sleep and the timeout message after that interval
    (allow any in-flight capture/send to return). Restart and confirm a fresh
    `20:00` with default settings. Also verify device auto-stop with status
    polling disabled. Build/flash `auto_off = false`; verify no countdown or
    automatic stop, with normal manual Stop and disconnect cleanup preserved.

Previous pre-refactor firmware was hardware-tested as described in README.
Those results do not validate this C++ refactor. Record new UART logs, HTTP
results, decoded image dimensions and memory observations after flashing.

## Configurable auto-off (2026-10-20)

The new build reads `[wifi].auto_off` (default true) and `auto_off_minutes`
(default 20) at compile time. Sanitized firmware, browser and configuration
host tests pass; ESP-IDF v6.1 builds without compiler warnings/errors. The
firmware is 1,051,040 bytes, with about 32% application partition headroom.
Hardware checks for this configurable policy remain pending.

After manual boot mode was entered, flashing succeeded at 115200 baud.
Fast-reflash verification initially failed, and esptool automatically rewrote
the complete application with successful hash verification. A separate
`verify-flash 0x10000 build/esp32_cam.bin` confirmed the digest of all
1,051,040 application bytes at the factory boot address. The bootloader and
partition table were verified unchanged. The board remains in bootloader;
remove GPIO0-to-GND and reset to run. Runtime checks are pending.

### Cold-start and JPEG-header result

After the user power-cycled the device with other viewers closed, the camera
reported uptime 38 seconds, zero frames and standby. A subsequent start
initialized and returned frames, but Pillow rejected the JPEG. Pillow's own
JPEG encode/decode self-test passed. Three separately inspected frames had
valid SOI/JFIF/DQT/DHT/SOF0 segments, but at offset 609 contained `FF D2`
instead of the expected JPEG start-of-scan marker `FF DA`. A diagnostic copy
with only byte 610 changed from D2 to DA decoded fully as 640x480. No JPEG
repair has been added to firmware. This establishes malformed camera output
as one immediate browser failure, in addition to the observed initialization
write failures. It does not prove whether sensor timing, configuration or
hardware causes the wrong marker.

The maintained-backend 10 MHz candidate passes host tests and compilation.
Successful initialization now logs configured XCLK for identification. The candidate has now been flashed with write hash verification and a
separate verify-flash of all 1,051,696 application bytes at 0x10000. The
board was left in bootloader; GPIO0 removal and reset are required for the
planned original-JPEG decoding and repeated-start checks. The firmware
change is not yet a hardware-proven fix.


### 10 MHz runtime result (2026-10-20)

After the user reset into application mode, UART reported ESP32-CAM02 at
http://192.168.1.127/ and successful initialization with configured XCLK
10000000 Hz. The acceptance script stopped on cycle 1 because Pillow could
not decode the original VGA JPEG. Status showed streaming=true,
camera_error_code=0, capture_errors=0 and stream_timed_out=false, with
1198 seconds remaining. The configured 20-minute timeout was not responsible.
Three further original frames (13975, 13975 and 13170 bytes) all contained
FF D2 at offset 609 instead of the FF DA scan marker. Changing only byte 610
in a diagnostic host copy allowed each frame to decode fully at 640x480.
Firmware sends the original frame buffer without such a repair.
Thus lowering XCLK did not resolve malformed output. Repeated-start and
resolution acceptance are not passed. The test stopped the stream afterward.
Next physical checks are an unpowered camera ribbon reseat and confirmation
of stable board power; these are diagnostic steps, not a proven hardware
fault. Evidence is in /tmp/esp32-cam-clock-trial-jpeg-check.json and
/tmp/esp32-cam-stream-recovery-test-uart.log (session-local files).


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
