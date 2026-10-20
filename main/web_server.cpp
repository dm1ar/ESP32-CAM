#include "web_server.hpp"
#include <array>
#include <cinttypes>
#include <cstdio>
#include <limits>
#include <string_view>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/task.h"
#include "network.hpp"

// ESP-IDF's binary embedding generates these GNU linker symbols, not C++
// objects. Keep the extension confined to this translation unit (deviation D03).
extern const char indexHtmlStart[] asm("_binary_index_html_start");
extern const char indexHtmlEnd[] asm("_binary_index_html_end");

namespace cam {
namespace {
constexpr char tag[]{"web"};

[[nodiscard]] esp_err_t responseHeaders(httpd_req_t* request, const char* type) noexcept
{
    const auto result = httpd_resp_set_type(request, type);
    return (result == ESP_OK) ? httpd_resp_set_hdr(request, "Cache-Control", "no-store") : result;
}

[[nodiscard]] esp_err_t json(httpd_req_t* request, const char* body) noexcept
{
    const auto result = responseHeaders(request, "application/json");
    return (result == ESP_OK) ? httpd_resp_sendstr(request, body) : result;
}

// Created before switching power modes. Even an early header or Wi-Fi failure
// returns the camera to PWDN, resets rates, and attempts minimum modem sleep.
// The worker returns every driver FrameLease before replying to a request.
class StreamSession final {
public:
    StreamSession(FrameService& frames, SharedState& state) noexcept : frames_{frames}, state_{state}
    {
        result_ = frames_.begin();
    }
    ~StreamSession() noexcept
    {
        const auto result = frames_.finish(failed_);
        if (result != ESP_OK) {
            ESP_LOGE(tag, "Camera standby failed: %s", esp_err_to_name(result));
        }
        ESP_LOGI(tag, "Stream ended; standby requested");
    }
    [[nodiscard]] esp_err_t result() const noexcept { return result_; }
    StreamSession(const StreamSession&) = delete;
    StreamSession& operator=(const StreamSession&) = delete;
    void fail(esp_err_t error) noexcept
    {
        failed_ = true;
        state_.cameraFailed(error);
    }
private:
    FrameService& frames_;
    SharedState& state_;
    bool failed_{false};
    esp_err_t result_{ESP_OK};
};
} // namespace

esp_err_t WebServer::index(httpd_req_t* request) noexcept
{
    const auto result = responseHeaders(request, "text/html; charset=utf-8");
    // The embedding appends one NUL. Linker-symbol subtraction is reviewed D03.
    const auto length = indexHtmlEnd - indexHtmlStart - 1;
    return (result == ESP_OK) ? httpd_resp_send(request, indexHtmlStart, length) : result;
}

esp_err_t WebServer::capture(httpd_req_t* request) noexcept
{
    const auto result = frames_.capture(true);
    if (result != ESP_OK) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                  "Snapshot failed; see /status camera_error_code");
    }
    const auto frame = frames_.snapshotView();
    const auto headers = responseHeaders(request, "image/jpeg");
    return (headers == ESP_OK) ? httpd_resp_send(request,
        reinterpret_cast<const char*>(frame.data), static_cast<ssize_t>(frame.size)) : headers;
}

esp_err_t WebServer::health(httpd_req_t* request) noexcept
{
    return json(request, "{\"status\":\"ready\",\"stream_port\":81}");
}

esp_err_t WebServer::stop(httpd_req_t* request) noexcept
{
    state_.requestStop(); // Never manipulate hardware from the page task.
    return json(request, "{\"stopping\":true}");
}

esp_err_t WebServer::control(httpd_req_t* request) noexcept
{
    std::array<char, 128U> body{};
    if ((request->content_len == 0U) || (request->content_len >= body.size())) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid settings body");
    }
    std::size_t received{0U};
    while (received < request->content_len) {
        const auto remaining = request->content_len - received;
        const auto count = httpd_req_recv(request, body.data() + received, remaining);
        if ((count <= 0) || (static_cast<std::size_t>(count) > remaining)) {
            return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Incomplete settings");
        }
        received += static_cast<std::size_t>(count);
    }
    CameraSettings settings{};
    if (!parseControl(std::string_view{body.data(), received}, settings) || !state_.select(settings)) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                  "Use one listed resolution and JPEG quality 0-63");
    }
    return json(request, "{\"accepted\":true}");
}

esp_err_t WebServer::status(httpd_req_t* request) noexcept
{
    const auto now = esp_timer_get_time();
    // Status may request Stop through shared state, but cannot touch hardware.
    // The stream task independently enforces the same deadline without polls.
    static_cast<void>(state_.shouldStop(now));
    auto snapshot = state_.snapshot();
    expireRates(snapshot, now);
    wifi_ap_record_t ap{};
    wifi_ps_type_t powerSave{WIFI_PS_NONE};
    const auto powerResult = esp_wifi_get_ps(&powerSave);
    std::array<char, 16U> rssi{"null"};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        const auto length = std::snprintf(rssi.data(), rssi.size(), "%d", static_cast<int>(ap.rssi));
        if ((length < 0) || (static_cast<std::size_t>(length) >= rssi.size())) {
            return httpd_resp_send_500(request);
        }
    }
    const auto* const selected = findResolution(snapshot.selected.resolution);
    if (selected == nullptr) {
        return httpd_resp_send_500(request);
    }
    const char* const sensor = (snapshot.sensorPid == OV2640_PID) ? "OV2640"
        : ((snapshot.sensorPid == 0U) ? "OV2640 (not yet detected)" : "Unknown sensor");
    const bool pending = snapshot.streaming
        && (!snapshot.hasApplied || (snapshot.selected != snapshot.applied));
    std::array<char, 1024U> body{};
    const auto length = std::snprintf(body.data(), body.size(),
        "{\"sensor\":\"%s\",\"sensor_pid\":\"0x%04x\",\"width\":%" PRIu32 ",\"height\":%" PRIu32 ","
        "\"format\":\"JPEG\",\"jpeg_quality\":%" PRIu32 ",\"fps\":%.2f,\"kib_per_second\":%.2f,"
        "\"frame_bytes\":%zu,\"frames_sent\":%" PRIu64 ",\"capture_errors\":%" PRIu32 ",\"streaming\":%s,"
        "\"wifi_rssi_dbm\":%s,\"psram_bytes\":%zu,\"psram_free_bytes\":%zu,"
        "\"free_heap_bytes\":%" PRIu32 ",\"uptime_seconds\":%" PRIi64 ",\"idf_version\":\"%s\","
        "\"resolution\":\"%s\",\"camera_state\":\"%s\",\"wifi_modem_sleep\":%s,\"settings_pending\":%s,"
        "\"node_name\":\"%s\",\"auto_off\":%s,\"auto_off_minutes\":%" PRIu32 ","
        "\"stream_limit_seconds\":%" PRIu32 ","
        "\"stream_remaining_seconds\":%" PRIu32 ",\"stream_timed_out\":%s,"
        "\"camera_error_code\":%" PRIi32 "}",
        sensor, static_cast<unsigned>(snapshot.sensorPid), snapshot.width, snapshot.height,
        snapshot.selected.quality, static_cast<double>(snapshot.rates.fps),
        static_cast<double>(snapshot.rates.kibPerSecond), snapshot.frameBytes,
        snapshot.framesSent, snapshot.captureErrors, snapshot.streaming ? "true" : "false",
        rssi.data(), esp_psram_get_size(), heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        esp_get_free_heap_size(), esp_timer_get_time() / 1'000'000,
        esp_get_idf_version(), selected->name.data(), stateName(snapshot.cameraState),
        ((powerResult == ESP_OK) && (powerSave != WIFI_PS_NONE)) ? "true" : "false",
        pending ? "true" : "false", Network::nodeName(), snapshot.policy.autoOff ? "true" : "false",
        snapshot.policy.limitSeconds / 60U, snapshot.policy.limitSeconds,
        streamRemainingSeconds(snapshot, now), snapshot.streamTimedOut ? "true" : "false",
        snapshot.cameraError);
    if ((length < 0) || (static_cast<std::size_t>(length) >= body.size())) {
        return httpd_resp_send_500(request);
    }
    const auto result = responseHeaders(request, "application/json");
    return (result == ESP_OK) ? httpd_resp_send(request, body.data(), length) : result;
}

esp_err_t WebServer::sendFrame(httpd_req_t* request, RateMeter& meter) noexcept
{
    const auto captured = frames_.capture(false);
    if (captured != ESP_OK) { return captured; }
    const auto frame = frames_.streamView();
    // Capture can block. If the deadline or Stop arrived during capture,
    // return this lease without starting another network transmission.
    if (state_.shouldStop(esp_timer_get_time())) {
        return ESP_OK;
    }
    // HTTPD takes a signed length; reject unrepresentable driver input before
    // converting. The actual ESP32 PSRAM frame is far below this limit.
    constexpr auto maxLength = static_cast<std::size_t>(std::numeric_limits<ssize_t>::max());
    if (frame.size > maxLength) {
        state_.captureFailed();
        return ESP_ERR_INVALID_SIZE;
    }
    std::array<char, 128U> header{};
    const auto length = std::snprintf(header.data(), header.size(),
        "--esp32camframe\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n", frame.size);
    if ((length <= 0) || (static_cast<std::size_t>(length) >= header.size())) {
        return ESP_ERR_INVALID_SIZE;
    }
    auto result = httpd_resp_send_chunk(request, header.data(), length);
    if (result == ESP_OK) {
        // D02: char access to the JPEG object representation for the SDK socket
        // API. No ownership transfer and no buffer modification take place.
        result = httpd_resp_send_chunk(request, reinterpret_cast<const char*>(frame.data),
                                       static_cast<ssize_t>(frame.size));
    }
    if (result == ESP_OK) {
        result = httpd_resp_send_chunk(request, "\r\n", 2);
    }
    if (result == ESP_OK) {
        const auto now = esp_timer_get_time();
        const auto rates = meter.sent(now, frame.size);
        state_.sent(static_cast<std::uint32_t>(frame.width),
                    static_cast<std::uint32_t>(frame.height), frame.size, now, rates);
    }
    return result; // HTTP reads an independent copy; no driver lease is held.
}

esp_err_t WebServer::stream(httpd_req_t* request) noexcept
{
    StreamSession session{frames_, state_};
    auto result = session.result();
    if (result != ESP_OK) {
        return result;
    }
    result = responseHeaders(request, "multipart/x-mixed-replace;boundary=esp32camframe");
    RateMeter meter{};
    meter.reset(esp_timer_get_time());
    while (result == ESP_OK) {
        if (state_.shouldStop(esp_timer_get_time())) {
            break;
        }
        const auto desired = state_.snapshot();
        if (!desired.hasApplied || (desired.applied != desired.selected)) {
            meter.reset(esp_timer_get_time());
        }
        // Honor Stop received during a potentially slow initialization retry.
        if (state_.shouldStop(esp_timer_get_time())) {
            break;
        }
        result = sendFrame(request, meter);
        const auto cameraError = state_.snapshot().cameraError;
        if (cameraError != 0) {
            result = cameraError;
            session.fail(result);
            if (!desired.hasApplied) {
                static_cast<void>(httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                    "Camera capture failed; see /status camera_error_code"));
            }
        }
        // Capture worker has already returned the driver frame.
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
    if (result == ESP_OK) {
        // Close the multipart response and then the HTTP chunked transfer.
        result = httpd_resp_send_chunk(request, "--esp32camframe--\r\n", HTTPD_RESP_USE_STRLEN);
        if (result == ESP_OK) {
            result = httpd_resp_send_chunk(request, nullptr, 0);
        }
    }
    return result;
}

esp_err_t WebServer::start() noexcept
{
    if ((pageServer_ != nullptr) || (streamServer_ != nullptr)) {
        return ESP_ERR_INVALID_STATE;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144U;
    config.lru_purge_enable = true;
    config.max_open_sockets = 3U;
    config.send_wait_timeout = 5U;
    config.recv_wait_timeout = 5U;
    auto result = httpd_start(&pageServer_, &config);
    if (result != ESP_OK) { return result; }

    // Value-initialize every SDK structure so future optional fields stay zero.
    std::array<httpd_uri_t, 7U> routes{};
    routes[0U].uri = "/";
    routes[0U].handler = &bridge<&WebServer::index>;
    routes[1U].uri = "/health";
    routes[1U].handler = &bridge<&WebServer::health>;
    routes[2U].uri = "/status";
    routes[2U].handler = &bridge<&WebServer::status>;
    routes[3U].uri = "/control";
    routes[3U].handler = &bridge<&WebServer::control>;
    routes[4U].uri = "/stop";
    routes[4U].handler = &bridge<&WebServer::stop>;
    routes[6U].uri = "/capture";
    routes[6U].handler = &bridge<&WebServer::capture>;
    routes[5U].uri = "/stream";
    routes[5U].handler = &bridge<&WebServer::stream>;
    for (std::size_t i{0U}; i < routes.size(); ++i) {
        routes[i].method = ((i == 3U) || (i == 4U)) ? HTTP_POST : HTTP_GET;
        routes[i].user_ctx = this;
        if (i == 5U) {
            // A blocking MJPEG handler must never run on the page/status task.
            config.server_port = 81U;
            ++config.ctrl_port;
            result = httpd_start(&streamServer_, &config);
            if (result != ESP_OK) { break; }
        }
        result = httpd_register_uri_handler((i == 5U) ? streamServer_ : pageServer_, &routes[i]);
        if (result != ESP_OK) { break; }
    }
    if (result != ESP_OK) {
        // Registration copies route descriptors; there are no dangling stack
        // descriptors. Roll back either server if startup is incomplete.
        if (streamServer_ != nullptr) {
            ESP_ERROR_CHECK(httpd_stop(streamServer_));
            streamServer_ = nullptr;
        }
        ESP_ERROR_CHECK(httpd_stop(pageServer_));
        pageServer_ = nullptr;
        return result;
    }
    ESP_LOGI(tag, "Browser ready on port 80; MJPEG on port 81 (one viewer)");
    return ESP_OK;
}
} // namespace cam
