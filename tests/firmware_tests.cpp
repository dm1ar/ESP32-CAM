#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <thread>
#include <type_traits>
#include "camera.hpp"
#include "frame_store.hpp"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "model.hpp"
#include "shared_state.hpp"

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

// Driver fault injection checks the production Camera and FrameLease classes.
struct FakeDriver {
    unsigned initCalls{}, deinitCalls{}, returned{}, delays{}, failures{}, borrowed{};
    std::uint32_t power{1U};
    bool initialized{}, missingFrame{};
    camera_config_t lastConfig{};
    camera_fb_t frame{};
    sensor_t sensor{{0x2642U}};
} driver;

void testControl()
{
    for (const auto& resolution : cam::resolutions) {
        for (unsigned quality{0U}; quality <= 63U; ++quality) {
            std::array<char, 96U> body{};
            static_cast<void>(std::snprintf(body.data(), body.size(),
                "resolution=%s&quality=%u", resolution.name.data(), quality));
            cam::CameraSettings parsed{};
            require(cam::parseControl(body.data(), parsed), "accept supported settings");
            require(parsed.resolution == resolution.value && parsed.quality == quality,
                    "parse exact supported settings");
        }
    }
    constexpr std::array invalid{
        "", "resolution=VGA", "quality=12", "resolution=bad&quality=12",
        "resolution=VGA&quality=-1", "resolution=VGA&quality=+1",
        "resolution=VGA&quality=64", "resolution=VGA&quality=9999999999999999999999",
        "resolution=VGA&quality=", "resolution=VGA&quality=1x",
        "resolution=VGA&quality=12&quality=13", "resolution=VGA&resolution=VGA&quality=12",
        "resolution=VGA&quality=12&x=y", "resolution=VGA&quality=12&",
        "resolution=VGA&quality=%31", "resolution=VGA&quality=1=2",
        "resolution=VGA&quality= 1", "&resolution=VGA&quality=12"
    };
    for (const auto* body : invalid) {
        cam::CameraSettings parsed{cam::Resolution::uxga, 63U};
        const auto original = parsed;
        require(!cam::parseControl(body, parsed), "reject malformed form");
        require(parsed == original, "invalid input must not partially apply");
    }
    cam::CameraSettings parsed{};
    require(cam::parseControl("quality=0&resolution=QVGA", parsed), "allow reordered fields");
    constexpr char embeddedNul[]{"resolution=VGA&quality=1\0garbage"};
    require(!cam::parseControl({embeddedNul, sizeof(embeddedNul) - 1U}, parsed), "reject embedded NUL");
    require(!cam::CameraSettings{static_cast<cam::Resolution>(255U), 12U}.valid(), "invalid enum rejected");
}

void testRates()
{
    cam::RateMeter meter{};
    meter.reset(0);
    auto rates = meter.sent(500'000, 1024U);
    require(rates.fps == 0.0F, "no configured FPS before first window");
    rates = meter.sent(1'000'000, 2048U);
    require(std::fabs(rates.fps - 2.0F) < 0.001F, "two successfully sent frames per second");
    require(std::fabs(rates.kibPerSecond - 3.0F) < 0.001F, "actual JPEG bytes per second");
    rates = meter.sent(3'000'000, 4096U);
    require(std::fabs(rates.fps - 0.5F) < 0.001F, "slow transmissions include elapsed time");
    require(std::fabs(rates.kibPerSecond - 2.0F) < 0.001F, "slow throughput is measured");
    meter.reset(4'000'000);
    require(meter.sent(4'500'000, 1024U).fps == 0.0F, "settings reset old window");
    cam::StreamSnapshot snapshot{};
    snapshot.streaming = true;
    snapshot.lastFrameUs = 1'000'000;
    snapshot.rates = {7.0F, 9.0F};
    cam::expireRates(snapshot, 3'999'999);
    require(snapshot.rates.fps == 7.0F, "active rates before expiry");
    cam::expireRates(snapshot, 4'000'000);
    require(snapshot.rates.fps == 0.0F && snapshot.rates.kibPerSecond == 0.0F, "expiry at three seconds");
    snapshot.streaming = false;
    snapshot.rates = {7.0F, 9.0F};
    cam::expireRates(snapshot, 1'000'000);
    require(snapshot.rates.fps == 0.0F, "idle has zero FPS immediately");
}

void testState()
{
    cam::SharedState state{};
    require(state.snapshot().selected == cam::CameraSettings{}, "reboot defaults VGA/12");
    require(state.select({cam::Resolution::uxga, 63U}), "idle selection");
    require(state.snapshot().width == 1600U, "idle dimensions follow selected settings");
    state.begin(100);
    state.ready({cam::Resolution::uxga, 63U}, 0x2642U);
    require(state.select({cam::Resolution::qvga, 0U}), "active selection");
    auto snapshot = state.snapshot();
    require(snapshot.width == 1600U && snapshot.selected != snapshot.applied,
            "active settings remain pending until stream applies them");
    state.sent(1600U, 1200U, 1024U, 200, {2.0F, 2.0F});
    state.requestStop();
    require(state.snapshot().stopRequested, "stop crosses tasks");
    state.finish(false);
    snapshot = state.snapshot();
    require(!snapshot.streaming && snapshot.rates.fps == 0.0F && !snapshot.hasApplied,
            "disconnect clears active status and rates");
    require(snapshot.width == 320U, "pending selection visible after disconnect");
    state.begin(300);
    require(!state.snapshot().stopRequested, "next stream clears previous stop");
    state.captureFailed();
    state.cameraFailed(0x20004);
    state.finish(true);
    require(state.snapshot().cameraState == cam::CameraState::error
            && state.snapshot().captureErrors == 1U, "capture failure is recoverable error state");
    require(state.snapshot().cameraError == 0x20004, "driver error retained after cleanup");
    state.begin(400);
    require(state.snapshot().cameraError == 0, "retry clears previous camera error");

    // A snapshot must not mix dimensions from two frames during concurrent
    // writes. This tests lock coverage with an actual host mutex, not FreeRTOS.
    cam::SharedState concurrent{};
    std::atomic<bool> done{false};
    std::thread writer{[&] {
        for (unsigned i{0U}; i < 20'000U; ++i) {
            concurrent.sent(320U, 240U, 1U, 0, {});
            concurrent.sent(1600U, 1200U, 1U, 0, {});
        }
        done.store(true);
    }};
    while (!done.load()) {
        snapshot = concurrent.snapshot();
        require(snapshot.width * 3U == snapshot.height * 4U, "snapshot dimensions are atomic");
    }
    writer.join();
    require(concurrent.snapshot().framesSent == 40'000U, "no lost frame updates");
}

void testStreamDeadline()
{
    cam::SharedState state{};
    require(cam::streamRemainingSeconds(state.snapshot(), 0) == 0U, "idle has no running timer");
    constexpr std::int64_t start{5'000'000};
    constexpr auto deadline = start + cam::StreamPolicy{}.limitUs();
    require(state.snapshot().policy.autoOff && state.snapshot().policy.limitSeconds == 1200U,
            "default auto-off policy is enabled for twenty minutes");
    state.begin(start);
    require(cam::streamRemainingSeconds(state.snapshot(), start) == 1200U, "start with twenty minutes");
    require(cam::streamRemainingSeconds(state.snapshot(), start + 1) == 1200U, "round partial seconds up");
    require(cam::streamRemainingSeconds(state.snapshot(), start + 1'000'000) == 1199U,
            "countdown uses device time");
    require(state.select({cam::Resolution::uxga, 63U}), "settings change during timer");
    state.waking();
    state.ready({cam::Resolution::uxga, 63U}, 0x2642U);
    state.sent(1600U, 1200U, 100U, start + 10'000'000, {});
    require(state.snapshot().streamDeadlineUs == deadline, "settings and frames cannot extend session");
    require(!state.shouldStop(deadline - 1), "stream permitted until deadline");
    require(cam::streamRemainingSeconds(state.snapshot(), deadline - 1) == 1U,
            "last partial second stays visible");
    require(state.shouldStop(deadline), "automatic stop at configured deadline without status polling");
    require(state.snapshot().streamTimedOut && state.snapshot().stopRequested, "timeout reason retained");
    require(cam::streamRemainingSeconds(state.snapshot(), deadline) == 0U, "zero at deadline");
    state.finish(false);
    require(state.snapshot().streamTimedOut && !state.snapshot().streaming,
            "standby retains timeout reason for browser");
    require(cam::streamRemainingSeconds(state.snapshot(), deadline + 100) == 0U, "idle countdown stays zero");
    state.begin(deadline + 1'000'000);
    require(!state.snapshot().streamTimedOut && !state.snapshot().stopRequested, "new session resets timeout");
    require(cam::streamRemainingSeconds(state.snapshot(), deadline + 1'000'000) == 1200U,
            "restart receives a fresh full session");
    state.requestStop();
    require(state.shouldStop(deadline + 2'000'000) && !state.snapshot().streamTimedOut,
            "manual Stop is not mislabeled as timeout");

    cam::SharedState custom{{true, 7U * 60U}};
    custom.begin(start);
    require(cam::streamRemainingSeconds(custom.snapshot(), start) == 420U, "custom duration injected");
    require(!custom.shouldStop(start + 420'000'000 - 1), "custom deadline not early");
    require(custom.shouldStop(start + 420'000'000), "custom seven-minute deadline enforced");

    cam::SharedState unlimited{{false, 20U * 60U}};
    unlimited.begin(start);
    require(unlimited.snapshot().streamDeadlineUs == 0, "disabled policy has no deadline");
    require(cam::streamRemainingSeconds(unlimited.snapshot(), start) == 0U,
            "disabled timer has no countdown");
    constexpr auto muchLater = start + 7LL * 24LL * 60LL * 60LL * 1'000'000LL;
    require(!unlimited.shouldStop(muchLater) && !unlimited.snapshot().streamTimedOut,
            "disabled timer cannot expire even after seven days");
    unlimited.requestStop();
    require(unlimited.shouldStop(muchLater), "manual Stop still works with auto-off disabled");
    unlimited.finish(false);
    require(!unlimited.snapshot().streaming && !unlimited.snapshot().streamTimedOut,
            "disabled policy retains ordinary standby cleanup");
}

void testCamera()
{
    static_assert(!std::is_copy_constructible_v<cam::FrameLease>);
    static_assert(!std::is_move_constructible_v<cam::FrameLease>);
    driver = {};
    cam::Camera camera{};
    require(camera.sleep() == ESP_OK && driver.power == 1U, "boot starts powered down");
    driver.failures = 2U;
    require(camera.prepare({}) == ESP_OK, "third initialization attempt succeeds");
    require(driver.initCalls == 3U && driver.delays == 3U, "bounded retry with PWDN interval");
    require(driver.lastConfig.frame_size == FRAMESIZE_VGA && driver.lastConfig.jpeg_quality == 12,
            "driver receives default settings");
    require(driver.lastConfig.xclk_freq_hz == 10'000'000, "conservative sensor clock reaches driver");
    require(camera.prepare({}) == ESP_OK && driver.initCalls == 3U, "unchanged settings avoid reinit");
    {
        const cam::FrameLease lease{};
        require(lease.get() != nullptr && driver.borrowed == 1U, "frame borrowed");
    }
    require(driver.borrowed == 0U && driver.returned == 1U, "frame returned exactly once");
    require(camera.prepare({cam::Resolution::uxga, 0U}) == ESP_OK, "reconfigure after return");
    require(driver.deinitCalls == 1U && driver.lastConfig.frame_size == FRAMESIZE_UXGA,
            "old driver is deinitialized before reinit");
    driver.missingFrame = true;
    { const cam::FrameLease lease{}; require(lease.get() == nullptr, "missing capture"); }
    require(driver.returned == 1U, "null frames never returned");
    require(camera.sleep() == ESP_OK && !driver.initialized && driver.power == 1U,
            "stop releases driver and asserts PWDN");
    driver.failures = 3U;
    const auto calls = driver.initCalls;
    require(camera.prepare({}) == ESP_FAIL && driver.initCalls == calls + 3U,
            "initialization stops after three failures");
    require(driver.power == 1U && !driver.initialized, "failed wake is powered down");
    require(camera.prepare({}) == ESP_OK, "next browser wake can recover");
    require(camera.sleep() == ESP_OK, "final camera cleanup");
}
void testLivePolicy()
{
    cam::StreamPolicy policy{};
    for (const auto* body : {"auto_off=true&auto_off_minutes=5", "auto_off_minutes=120&auto_off=false"}) {
        require(cam::parseAutoOff(body, policy), "valid browser policy form");
    }
    constexpr std::array invalid{
        "", "auto_off=true", "auto_off=1&auto_off_minutes=20", "auto_off=false&auto_off_minutes=4",
        "auto_off=true&auto_off_minutes=121", "auto_off=true&auto_off_minutes=-1",
        "auto_off=true&auto_off_minutes=20.0", "auto_off=true&auto_off_minutes=999999999",
        "auto_off=true&auto_off_minutes=20&x=y", "auto_off=true&auto_off_minutes=20&",
        "auto_off=true&auto_off=true&auto_off_minutes=20"};
    for (const auto* body : invalid) {
        const auto before = policy;
        require(!cam::parseAutoOff(body, policy), "reject malformed browser policy");
        require(policy.autoOff == before.autoOff && policy.limitSeconds == before.limitSeconds,
                "invalid policy does not partially apply");
    }
    cam::SharedState state{};
    state.begin(100);
    require(state.setPolicy({false, 300U}, 100'000'100), "disable during stream");
    require(!state.shouldStop(400'000'100) && state.snapshot().streamDeadlineUs == 0,
            "disabled policy has no deadline");
    require(state.setPolicy({true, 1200U}, 400'000'100), "enable during stream");
    require(state.snapshot().streamDeadlineUs == 1'200'000'100, "enabling anchors original stream start");
    require(state.setPolicy({true, 300U}, 400'000'100), "shorter policy accepted");
    require(state.shouldStop(400'000'100) && state.snapshot().streamTimedOut,
            "already elapsed new duration stops immediately");
    require(state.setPolicy({false, 7200U}, 400'000'101) && state.shouldStop(400'000'101),
            "policy edit cannot revive stopped stream");
    require(!state.setPolicy({true, 299U}, 0) && !state.setPolicy({true, 7201U}, 0),
            "policy API enforces slider bounds");
    state.finish(false);
    state.begin(500'000'100);
    require(!state.shouldStop(900'000'100), "new stream honors disabled policy");
    require(state.setPolicy({true, 300U}, 900'000'100) && state.shouldStop(900'000'100),
            "enabling after selected interval stops old stream");
}
void testFrameStore()
{
    driver = {};
    cam::Camera camera{};
    cam::SharedState state{};
    cam::FrameStore frames{camera, state};
    std::array<std::uint8_t, 8U> jpeg{0xFFU, 0xD8U, 1U, 2U, 3U, 4U, 0xFFU, 0xD9U};
    driver.frame = {jpeg.data(), jpeg.size(), 640U, 480U, PIXFORMAT_JPEG};
    require(frames.start() == ESP_OK, "allocate bounded PSRAM copies");
    require(frames.capture(true) == ESP_OK, "idle snapshot wakes and captures");
    require(driver.borrowed == 0U && !driver.initialized && driver.power == 1U,
            "idle snapshot returns frame and powers down before send");
    require(frames.snapshotView().size == jpeg.size() && frames.snapshotView().data[2U] == 1U,
            "snapshot owns driver-independent bytes");
    jpeg[2U] = 42U;
    require(frames.snapshotView().data[2U] == 1U, "returned driver frame cannot mutate snapshot");
    require(!state.snapshot().streaming && state.snapshot().framesSent == 0U
            && state.snapshot().cameraState == cam::CameraState::standby,
            "snapshot does not start a stream or count as transmitted stream FPS");
    state.begin(100U);
    require(frames.capture(false) == ESP_OK, "capture stream copy");
    const auto returned = driver.returned;
    require(frames.capture(true) == ESP_OK && driver.returned == returned,
            "active snapshot reuses published frame without extra capture or reinit");
    require(frames.streamView().data != frames.snapshotView().data,
            "slow HTTP sends use separate buffers");
    jpeg[2U] = 99U;
    require(frames.capture(false) == ESP_OK && frames.streamView().data[2U] == 99U,
            "next stream frame updates its copy");
    require(frames.snapshotView().data[2U] == 42U,
            "new stream capture cannot overwrite snapshot being sent");
    require(state.snapshot().streamDeadlineUs == 1'200'000'100,
            "snapshot sharing does not extend stream deadline");
    require(state.select({cam::Resolution::qvga, 12U}), "select new settings during stream");
    const auto initCalls = driver.initCalls;
    require(frames.capture(true) == ESP_OK && driver.initCalls == initCalls + 1U,
            "snapshot never reuses old settings after selection changes");
    require(frames.sleep() == ESP_OK, "worker standby returns all leases");
    state.finish(false);
    driver.missingFrame = true;
    require(frames.capture(true) == ESP_ERR_INVALID_STATE, "snapshot capture failure reported");
    require(!driver.initialized && driver.power == 1U && driver.borrowed == 0U,
            "failed snapshot cleans up camera");
    require(frames.snapshotView().size == 0U, "failure cannot expose stale snapshot");
    driver.missingFrame = false;
    driver.frame.len = cam::JpegBuffer::capacity + 1U;
    require(frames.capture(true) == ESP_ERR_INVALID_SIZE, "oversize JPEG rejected before copy");
    require(!driver.initialized && driver.borrowed == 0U, "oversize JPEG returns lease and sleeps");
    driver.frame.len = jpeg.size();
    require(frames.capture(true) == ESP_OK && state.snapshot().cameraError == 0,
            "snapshot recovers after previous failure");
    driver.failures = 3U;
    require(frames.capture(true) == ESP_FAIL && !driver.initialized && driver.power == 1U,
            "initialization failure leaves snapshot camera asleep");
    cam::JpegBuffer buffer{};
    require(!buffer.assign({jpeg.data(), jpeg.size(), 640U, 480U}),
            "unallocated output rejects copy");
    require(buffer.allocate() && !buffer.assign({nullptr, 1U, 640U, 480U}),
            "null source rejected");
}
} // namespace

void* heap_caps_malloc(std::size_t size, std::uint32_t caps)
{
    require(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT), "JPEG copies use PSRAM");
    return std::malloc(size);
}
void heap_caps_free(void* pointer) { std::free(pointer); }
const char* esp_err_to_name(esp_err_t) { return "injected failure"; }
esp_err_t gpio_set_direction(gpio_num_t, gpio_mode_t) { return ESP_OK; }
esp_err_t gpio_set_level(gpio_num_t, std::uint32_t level) { driver.power = level; return ESP_OK; }
void vTaskDelay(std::uint32_t ticks)
{
    require(ticks == 100U && driver.power == 1U, "100 ms PWDN delay precedes each probe");
    ++driver.delays;
}
esp_err_t esp_camera_init(const camera_config_t* config)
{
    require(!driver.initialized && driver.borrowed == 0U, "no reinit with outstanding frames");
    driver.lastConfig = *config;
    ++driver.initCalls;
    driver.power = 0U;
    if (driver.failures > 0U) { --driver.failures; return ESP_FAIL; }
    driver.initialized = true;
    return ESP_OK;
}
esp_err_t esp_camera_deinit()
{
    require(driver.borrowed == 0U && driver.initialized, "deinit only after frames returned");
    ++driver.deinitCalls;
    driver.initialized = false;
    return ESP_OK;
}
camera_fb_t* esp_camera_fb_get()
{
    require(driver.initialized, "capture requires initialized driver");
    if (driver.missingFrame) { return nullptr; }
    ++driver.borrowed;
    return &driver.frame;
}
void esp_camera_fb_return(camera_fb_t* frame)
{
    require(frame == &driver.frame && driver.borrowed == 1U, "return matching frame once");
    --driver.borrowed;
    ++driver.returned;
}
sensor_t* esp_camera_sensor_get() { return driver.initialized ? &driver.sensor : nullptr; }

int main()
{
    testControl();
    testRates();
    testState();
    testStreamDeadline();
    testCamera();
    testFrameStore();
    testLivePolicy();
    std::puts("PASS: settings, measured rates, shared state, concurrency, camera lifecycle, shared JPEG ownership and snapshots");
}
