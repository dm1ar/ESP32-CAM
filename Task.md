# # ESP32-CAM

## ESP Web Tools and NVS settings (web-flash)

Implemented generic firmware with runtime settings from read-only NVS
partition config at 0x190000, size 0x6000, camera namespace/schema 1. No
compiled credential fallback or dependency on local TOML during compilation.
Generator tools/wifi_config.py now produces config.bin from wifi.toml using
esp-idf-nvs-partition-gen 0.1.9, private intermediates and atomic replacement.
Startup validates all keys and stays idle with provisioning instructions if
settings are unavailable/invalid. Node/policy/credentials are loaded before
Wi-Fi and shared with services via application-owned storage.

tools/prepare_web_flash.py creates a credential-free build/web-flash package
(firmware.bin, manifest.json, index.html, installer.mjs) for ESP Web Tools
10.4.0. Browser modes install+config, firmware-update/preserve, config-only;
private config uses local Blob URLs and is not uploaded. Erase prompt stays
enabled and must be left unchecked on updates. No Improv implementation.
Native ESP-IDF/VS Code workflows retain firmware flashing; config/flash-config/
web-flash wrapper commands and VS Code tasks added. Partition table preserves
existing NVS/PHY/app addresses and adds separate config storage.

Build and complete host suite pass; app 1,384,784 bytes / 151,216 free.
Official NVS generation/CRC/exact-string tests, private credential scans,
package boundary/sector-preservation tests and actual Chromium installer
custom-element/manifests/Blob tests pass. No hardware writes or publishing.
Real serial/browser installation, NVS boot and persistence tests remain
pending. Current running board still has the previously accepted firmware.

Hardware upload subsequently performed at user request: replacement MAC
a0:dd:6c:79:56:74 confirmed. Private config.bin written at 0x190000 and
independently verified. Full native IDF flash then verified app (1,384,784
bytes), partition table and unchanged settings. Public web-installer merged
image (1,450,320 bytes at 0x0) flashed as a firmware-only update and separately
verified along with all 24,576 unchanged NVS config bytes. Both update paths
preserve private settings on actual flash. Board remains in bootloader;
GPIO0 removal/reset requested before NVS boot, streaming and persistence
acceptance. Actual browser serial flashing is not established by these
esptool writes. Evidence: /tmp/esp32-cam-nvs-hardware-*.log.

Chrome WebRTC follow-up (2026-10-21): browser disconnects at ICE pairing.
UART candidates are `unknown`, followed immediately by peer state 8;
numeric aiortc ICE had passed. Candidate now resolves .local UDP candidates
using pinned Espressif mdns 1.14.0, with bounded SDP rewriting, four queries
at two seconds each and service teardown before DTLS. No browser privacy
flags changed. IDF build passes, application 1,383,392 bytes / 152,608 free.
SDP and lifecycle host tests pass, including lookup-failure cleanup; real
mDNS/Chrome checks pending. No correction firmware uploaded yet. New test option:
tools/check_webrtc.py --mdns advertises real browser-style multicast names.
First correction upload attempt after user confirmation received no serial
bootloader data and wrote nothing. /dev/ttyUSB0 is accessible with no other
holder afterward. Another reset with GPIO0 grounded requested; previous
accepted MJPEG/RTSP firmware remains installed. Log:
/tmp/esp32-cam-mdns-flash.log (session-local).
Second correction upload and separate known-working esptool 4.5.1 no-stub
ROM query also received no serial data; nothing written. The camera still
answers /health and /status over Wi-Fi with recent-reset uptime (~45 s),
showing normal application boot rather than download mode. Requested power
off, direct GPIO0-to-GND jumper check on ESP32-CAM, then power on while
keeping it connected. Logs: /tmp/esp32-cam-mdns-flash-retry.log and
/tmp/esp32-cam-mdns-rom-check.log. USB adapter remains ttyUSB0.
After the requested power-cycle/jumper check, ROM connection succeeded on
MAC a0:dd:6c:79:56:74. Complete IDF flash at 115200 baud passed write/content
verification; independent verify-flash matched all 1,383,392 application
bytes at 0x10000. Board left in bootloader; GPIO0 removal/reset requested
before real multicast mDNS, ICE/DTLS/SCTP and Chrome acceptance checks.
Runtime mDNS acceptance now passed: DHCP 192.168.1.128; two real peer
connections using three multicast .local names each, all names resolved in
UART. Ten VGA JPEGs/two snapshots decoded, busy rejection, reconnect and
cleanup passed. Streaming internal heap 49,503 bytes free, largest 47,104;
idle recovered 138,611. HTTP/1.0+1.1 JPEG/Stop/EOF regression also passed.
No camera error/panic/brownout during checks. Camera idle VGA/12, auto-off
true/20. User asked to hard-refresh /webrtc in Chrome and report playback;
actual Chrome confirmation still pending.
A subsequent Chrome browser attempt completed ICE/DTLS/SCTP, opened channel
ID 1 and acknowledged frames at ~4.55 FPS. frames_sent advanced 16→104 before
the remote closed DTLS; healthy standby/error=0 and heap recovery followed.
This verifies the viewer decode/canvas/ACK path; user visual confirmation
still pending. One unresolved secondary mDNS candidate did not prevent use
of the resolved LAN candidate. Evidence: same UART log at uptime 74–104 s.
User now confirms "Live video works" in Chrome. Browser WebRTC acceptance
passed, superseding pending Chrome/visual confirmation notes. No further
flash is needed for this correction. Full-duration auto-off remains untested.

MJPEG/VLC follow-up (2026-10-21): user confirms Snap VLC RTSP works but
MJPEG works only in browser. Direct reproduction found VLC falling back
to HTTP/1.0 while the device still sends HTTP chunk coding. Dechunking via
a temporary host relay lets the same VLC select MJPEG and decode pictures.
Candidate /stream source now sends close-delimited multipart with checked
partial writes through public httpd_send; initial camera HTTP 500, Stop,
boundaries and measured counters retained. Build and full host tests pass;
1,340,272 application bytes. No new firmware uploaded yet; manual boot entry
requested. Validate with tools/check_http_stream.py and direct Snap VLC
after upload, plus HTTP regression. See docs/VALIDATION.md for evidence.
Candidate upload now succeeded at 115200 baud on MAC a0:dd:6c:79:56:74.
Complete IDF flash passed verification; separate verify-flash matched all
1,340,272 application bytes at 0x10000. Board left in bootloader awaiting
GPIO0 removal/reset and direct Snap VLC plus raw HTTP/1.0/1.1 acceptance.
Runtime acceptance now passed: UART DHCP 192.168.1.128, raw HTTP/1.0 and
HTTP/1.1 each decode three VGA JPEGs with no chunk coding and correct
Stop/closing-boundary/EOF. Direct Snap VLC 3.0.24 now auto-selects MJPEG and
decodes pictures; bounded CLI dummy-output probe cleaned up to standby.
Full HTTP regression passes all five resolutions, 30 snapshots and 79
MJPEG frames, simultaneous snapshots/countdown/cleanup. Settings restored
VGA/12; no camera error or brownout observed during this boot/checks.

WebRTC continuation (2026-10-21, branch `webrtc`): direct JPEG data-channel
implementation with pinned Espressif esp_peer 1.5.7 and esp_libsrtp 1.0.0.
Actual ESP32 build and complete host suite pass; binary 1,340,544 bytes,
195,456 bytes free in existing 1500 KiB partition. Browser at /webrtc;
one video owner shared with RTSP/MJPEG, snapshots and auto-off retained.
Host lifecycle tests mock the peer API; real ICE/DTLS/SCTP and internal RAM
acceptance remain pending. Independent client: tools/check_webrtc.py with
aiortc/Pillow. Initial upload after user boot-mode confirmation received
no serial data and wrote nothing; port available, another manual reset
with GPIO0 grounded requested. Existing accepted firmware remains installed.
See README and docs/VALIDATION.md for protocol, limits and acceptance steps.
Upload retry after another manual reset succeeded on replacement MAC
a0:dd:6c:79:56:74 at 115200 baud. Complete flash verified bootloader and
partition content and the application write hash. Separate verify-flash
matched all 1,340,544 application bytes at 0x10000. Board left in bootloader;
manual GPIO0 removal/reset and real WebRTC acceptance remain pending.
First runtime attempt after reset reports repeated brownout resets during
Wi-Fi PHY initialization, before DHCP or WebRTC peer creation. Requested
power cycle with GPIO0 disconnected and stable 5V supply check. Hardware
WebRTC and RAM acceptance remain unverified; no code remedy claimed.
Subsequent runtime acceptance: DHCP 192.168.1.128; two real aiortc
ICE/DTLS/SCTP connections decode ten VGA JPEGs, snapshots/concurrent-owner
rejection/reconnect/idle cleanup pass. Streaming internal heap ~51,115 bytes
free, largest block 47,104; idle recovers ~139,599 bytes. HTTP regression
passes all five resolutions (30 snapshots, 41 MJPEG frames); RTSP passes
all five resolutions TCP+UDP (30 frames), snapshots, busy rejection, Stop
and live policy validation. No further brownout occurred during checks.
Actual browser playback and full-duration auto-off remain pending. User
asked to open /webrtc after automated tests released the camera.

We create Firmware for the esp32-cam module connected through an SH-U06B serial bridge from DSD TECH.

## Init

read the assets/ reference documents
the esp32-cam is jumpered into BOOT-mode
it is coonected to: Bus 011 Device 005: ID 067b:23a3 Prolific Technology, Inc. ATEN Serial Bridge

enable USB device connection for codex

check, if the module is accessible.

## FirstApp

initialize wifi connection to SSID: <private-wifi-ssid>
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


2026-10-21 web-flash/NVS hardware acceptance: private config at 0x190000,
native IDF firmware and merged public image at zero were flashed with write
hashes and independent verification. Config remained unchanged through both
firmware writes. Boot joined Wi-Fi as ESP32-CAM02 at 192.168.1.128 and
reported the expected NVS auto-off defaults true/20. HTTP/1.0+1.1, snapshots
and MJPEG at all five resolutions, RTSP TCP/UDP at all five resolutions and
two real WebRTC/mDNS connections passed. Reboot restoration after RAM policy
edits awaits manual reset. Actual browser USB flashing remains untested;
merged-image verification used esptool. No publication or push performed.


2026-10-21 user acceptance: the user confirmed the installation works and
requested no further tests. No additional hardware tests were performed.
Restoration of NVS defaults after the temporary RAM policy change was not
independently measured; actual browser USB flashing remains unverified.
README now leads with browser installation and separates VS Code/ESP-IDF use.
Project-owned source files carry the requested Axel Richter SPDX header;
third-party dependencies are excluded. Housing CAD data belongs in cad/housing/.
