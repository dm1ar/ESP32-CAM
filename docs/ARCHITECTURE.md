# Firmware architecture

All first-party firmware is C++ in namespace `cam`. `app_main` has C linkage
for the ESP-IDF entry point and holds one static `Application`. Its members
outlive the event callbacks and HTTP tasks, even after `app_main` returns.
The embedded JavaScript `LiveCameraPage` class owns browser presentation.

## Class diagram

```mermaid
classDiagram
    class Application {
        -Network network_
        -Camera camera_
        -SharedState state_
        -WebServer web_
        +run() esp_err_t
    }
    class Network {
        -station_
        -connected_
        -eventStorage_
        +start() esp_err_t
        +logUrl() void
        +nodeName() const_char_ptr
        +setStreamingPower(bool) esp_err_t
        -onEvent(context, base, id, data) void
    }
    class Camera {
        -CameraSettings applied_
        -bool initialized_
        +prepare(CameraSettings) esp_err_t
        +sleep() esp_err_t
        +matches(CameraSettings) bool
        +sensorPid() uint32_t
    }
    class FrameLease {
        -camera_fb_t frame_
        +get() camera_fb_t
        +~FrameLease()
    }
    class SharedState {
        -StreamSnapshot data_
        -portMUX_TYPE lock_
        +snapshot() StreamSnapshot
        +select(CameraSettings) bool
        +requestStop() void
        +shouldStop(now) bool
        +begin(now) void
        +waking() void
        +ready(settings, pid) void
        +sent(width, height, bytes, now, rates) void
        +captureFailed() void
        +cameraFailed(error) void
        +finish(failed) void
    }
    class CriticalSection {
        -portMUX_TYPE lock_
        +~CriticalSection()
    }
    class WebServer {
        -Camera camera_
        -SharedState state_
        -pageServer_
        -streamServer_
        +start() esp_err_t
        -index(request) esp_err_t
        -health(request) esp_err_t
        -status(request) esp_err_t
        -control(request) esp_err_t
        -stop(request) esp_err_t
        -stream(request) esp_err_t
        -sendFrame(request, meter) esp_err_t
    }
    class StreamSession {
        -Camera camera_
        -SharedState state_
        -bool failed_
        +fail() void
        +~StreamSession()
    }
    class RateMeter {
        -startUs_
        -frames_
        -bytes_
        -Rates rates_
        +reset(now) void
        +sent(now, bytes) Rates
    }
    class CameraSettings {
        +Resolution resolution
        +uint32_t quality
        +valid() bool
    }
    class StreamSnapshot {
        +StreamPolicy policy
        +CameraSettings selected
        +CameraSettings applied
        +CameraState cameraState
        +Rates rates
        +bool streaming
        +bool stopRequested
        +int64_t streamDeadlineUs
        +bool streamTimedOut
        +int32_t cameraError
    }
    class StreamPolicy {
        +bool autoOff
        +uint32_t limitSeconds
        +limitUs() int64_t
    }
    class Rates {
        +float fps
        +float kibPerSecond
    }
    class LiveCameraPage {
        +start() void
        +stop() Promise
        +apply() Promise
        +refresh() Promise
        +render(sample) void
    }
    Application *-- Network
    Application *-- Camera
    Application *-- SharedState
    Application *-- WebServer
    WebServer --> Camera : borrows
    WebServer --> SharedState : borrows
    WebServer ..> FrameLease : scoped per frame
    WebServer ..> StreamSession : scoped per request
    WebServer ..> RateMeter : local per stream
    StreamSession --> Camera : cleanup
    StreamSession --> SharedState : finish
    StreamSession ..> Network : restore modem sleep
    SharedState *-- StreamSnapshot
    SharedState ..> CriticalSection : scoped locking
    StreamSnapshot *-- CameraSettings
    StreamSnapshot *-- StreamPolicy
    StreamSnapshot *-- Rates
    Camera *-- CameraSettings
    RateMeter *-- Rates
    LiveCameraPage ..> WebServer : HTTP ports 80 and 81
```

`Camera`, `Network`, `Application`, `WebServer`, `SharedState`, `FrameLease`,
`StreamSession` and `CriticalSection` cannot be copied. `FrameLease` also
cannot be moved: one driver buffer always has one scoped return. It borrows
the buffer, while the driver retains allocation ownership. `Camera` has no
destructor-driven hardware action at global shutdown; `StreamSession` is the
explicit runtime cleanup boundary.

## Task and ownership boundaries

| Execution context | Responsibility | Hardware access |
| --- | --- | --- |
| `app_main` at boot | Lamp off, initial PWDN, PSRAM check, Wi-Fi and HTTP startup | GPIO and initial camera sleep before HTTP starts |
| ESP-IDF event loop | Wi-Fi connect/reconnect, DHCP URL logging, connection event bit | Wi-Fi API only |
| HTTP server on port 80 | HTML, health, telemetry, settings validation, Stop request | Read-only system/Wi-Fi queries; no camera calls |
| HTTP server on port 81 | One blocking MJPEG session, settings application, retries, frames, cleanup | Sole camera owner after boot |
| Browser | Start/Stop/Apply controls and status presentation | HTTP only |

Every shared read/write goes through a bounded spinlock in `SharedState`.
Snapshot copies leave the lock before driver queries and JSON formatting.
No camera calls, blocking operations or network I/O occur under the lock.
Settings are compared by value rather than wrapping generation counters.
A setting accepted during initialization remains pending until the next
frame boundary; returning to the already applied setting avoids a needless
reinitialization. RAM-only VGA/12 defaults return on reset.

## Stream and setting sequence

```mermaid
sequenceDiagram
    actor User
    participant UI as LiveCameraPage
    participant Page as WebServer port 80
    participant State as SharedState
    participant Stream as WebServer port 81
    participant Camera
    participant Driver as ESP camera driver
    User->>UI: Start stream
    UI->>Stream: GET /stream
    Stream->>State: begin(now)
    Stream->>Stream: Create StreamSession and disable Wi-Fi sleep
    Stream->>State: Snapshot selected settings
    Stream->>State: waking()
    Stream->>Camera: prepare(settings)
    loop At most three initialization attempts
        Camera->>Driver: PWDN high, delay 100 ms, initialize
    end
    Camera-->>Stream: Result and sensor PID
    Stream->>State: ready(applied settings, PID)
    loop Until Stop, disconnect, or failure
        Stream->>Driver: FrameLease acquires JPEG
        Stream-->>UI: Send multipart header, JPEG, terminator
        Stream->>State: sent(actual bytes, time, measured rates)
        Stream->>Driver: FrameLease returns buffer
        opt Settings changed on page task
            User->>UI: Apply settings
            UI->>Page: POST /control
            Page->>State: select(validated settings)
            Stream->>State: Snapshot selected settings
            Stream->>Camera: prepare(new settings), after frame returned
            Stream->>State: ready(new settings, PID)
            Stream->>Stream: Reset rate window
        end
        UI->>Page: GET /status
        Page->>State: snapshot()
        Page-->>UI: JSON, expire rates if stale
    end
    User->>UI: Stop stream
    UI->>Page: POST /stop and close image connection
    Page->>State: requestStop()
    Stream->>Camera: StreamSession destructor calls sleep()
    Camera->>Driver: Deinitialize capture and assert PWDN
    Stream->>Stream: Restore Wi-Fi minimum modem sleep
    Stream->>State: finish(), zero rates
```

The sequence shows a successful session. A capture/initialization error
uses the same scoped cleanup. A socket/header failure is treated as a
disconnect and returns to standby; hardware/capture failures report `error`
with PWDN asserted. A later GET `/stream` can retry from either state.
Stop is asynchronous: an in-progress driver capture or socket send must
return before the stream task can clean up. Current send/receive timeouts
are five seconds; the pinned camera driver's frame wait can take four seconds.

## Camera state machine

```mermaid
stateDiagram-v2
    [*] --> Standby: Boot asserts PWDN
    Standby --> Waking: Browser GET /stream
    Error --> Waking: Browser retries GET /stream
    Waking --> Streaming: Initialization succeeds
    Waking --> Error: Three failures or GPIO error, cleanup
    Waking --> Standby: Stop observed after successful initialization
    Streaming --> Waking: Settings changed, previous buffer returned
    Streaming --> Standby: Stop, enabled deadline or socket failure, cleanup
    Streaming --> Error: Capture or cleanup failure
    Standby --> Standby: Select RAM settings or poll status
    Error --> Error: Poll status or select settings
```

Both standby and error mean the application has attempted to deinitialize
the driver and assert sensor PWDN; failures are logged. Wi-Fi stays associated
with minimum modem sleep requested. The page/health server stays available.
`streaming` in JSON denotes an active stream session, including its wake phase;
`camera_state` distinguishes waking from actually configured streaming.

## Node identity and session deadline

`wifi.toml` provides `[wifi].node_name`, defaulting to `ESP32-CAM01`.
The private configuration generator validates a single 1–32 character ASCII
hostname label (letters/digits, internal hyphens) before generating a constant.
`Network::nodeName()` exposes only that identifier; the credential header
stays private to the network translation unit. The name identifies the DHCP
host, UART URL message, browser and status JSON. No mDNS service is added.

`[wifi].auto_off` defaults to true and `auto_off_minutes` defaults to 20.
The configuration generator validates boolean/integer types and duration
bounds before generating C++ constants. `Network::streamPolicy()` supplies
those compiled values to `Application` and `SharedState` at startup; they
cannot be changed through browser controls. `StreamPolicy` contains the
enabled flag and limit in seconds.

`SharedState::begin()` stores a device-monotonic deadline of start + the
configured duration when enabled, or no deadline when disabled. It clears
the previous timeout reason. `shouldStop(now)` checks
that deadline under the existing spinlock and requests Stop without touching
hardware. The stream task checks before initialization, after initialization
and after acquiring a frame. Status polling can request the same Stop, but
the stream task does not depend on polls to enforce it. The existing session
guard performs power-down and Wi-Fi sleep cleanup at the next safe boundary.
Settings/rate-window changes never modify the session deadline.

Status includes `node_name`, `auto_off`, `auto_off_minutes`, `stream_limit_seconds`,
`stream_remaining_seconds` (rounded upwards while active, zero idle/disabled) and
`stream_timed_out` (retained in standby until a new stream). The browser
shows remaining time as `MM:SS` or Auto-off disabled, clears its stream intent on timeout and
allows an explicit restart. While waiting for a new stream to be acknowledged,
it ignores stale timeout status belonging to the previous session.

## Rates and browser polling

`RateMeter` accumulates only successfully sent complete frames and JPEG bytes
over at least one second. It publishes actual frames/elapsed seconds and
JPEG bytes/(1024 × elapsed seconds). Multipart overhead is excluded, matching
the original metric. Failed sends do not increment either metric or total
frames. Reset and expiry are covered by host tests.

The browser polls every second while a session is active or requested, and
every five seconds idle. Its generation counter prevents older fetch replies
from overwriting the display or creating extra polling timers. Unsaved edits
are preserved. The device's measured FPS is displayed without substituting
browser frame/render rates.

## Build and dependencies

All seven first-party translation units use GNU++26; managed dependencies
keep upstream settings. Exceptions and RTTI are disabled. The generated
private Wi-Fi header remains in ignored `build/private_config/`; binaries
contain credentials and must not be shared.

The standard `SINGLE_APP_LARGE` partition layout provides a 1500 KiB factory
application beginning at `0x10000`, within the verified 4 MB flash. It keeps
the existing NVS and PHY regions; it does not provide OTA. Full flashing
updates the partition table along with the app. `dependencies.lock` retains
camera 2.1.7, JPEG 1.3.1 and IDF 6.1.0.
