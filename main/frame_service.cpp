/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "frame_service.hpp"
#include "freertos/task.h"
#include "network.hpp"
#include "esp_log.h"
#include "esp_timer.h"

namespace cam {
esp_err_t FrameService::start() noexcept
{
    if (queue_ != nullptr) { return ESP_ERR_INVALID_STATE; }
    const auto result = store_.start();
    if (result != ESP_OK) { return result; }
    queue_ = xQueueCreateStatic(2U, sizeof(Request*), queueBytes_.data(), &queueStorage_);
    if (queue_ == nullptr) { return ESP_ERR_NO_MEM; }
    if (xTaskCreate(&entry, "camera_owner", 4096U, this, 5U, nullptr) != pdPASS) {
        vQueueDelete(queue_);
        queue_ = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t FrameService::submit(Operation operation) noexcept
{
    if (queue_ == nullptr) { return ESP_ERR_INVALID_STATE; }
    StaticSemaphore_t storage{};
    const auto completion = xSemaphoreCreateBinaryStatic(&storage);
    if (completion == nullptr) { return ESP_ERR_NO_MEM; }
    Request request{operation, completion};
    auto* pointer = &request;
    // No timeout after enqueue: the worker borrows this request and semaphore
    // until completion. Camera driver capture has its own bounded wait.
    const auto queued = xQueueSend(queue_, &pointer, portMAX_DELAY);
    const auto completed = (queued == pdTRUE)
        ? xSemaphoreTake(completion, portMAX_DELAY) : pdFALSE;
    vSemaphoreDelete(completion);
    return (completed == pdTRUE) ? request.result : ESP_FAIL;
}

esp_err_t FrameService::capture(bool snapshot) noexcept
{
    return submit(snapshot ? Operation::snapshot : Operation::stream);
}

esp_err_t FrameService::begin() noexcept { return submit(Operation::begin); }

esp_err_t FrameService::finish(bool failed) noexcept
{
    return submit(failed ? Operation::finishFailed : Operation::finish);
}

void FrameService::entry(void* context) noexcept
{
    if (context != nullptr) { static_cast<FrameService*>(context)->run(); }
    vTaskDelete(nullptr);
}

void FrameService::run() noexcept
{
    for (;;) {
        Request* request{nullptr};
        if ((xQueueReceive(queue_, &request, portMAX_DELAY) != pdTRUE) || (request == nullptr)) {
            continue;
        }
        if (request->operation == Operation::begin) {
            if (state_.snapshot().streaming) {
                request->result = ESP_ERR_INVALID_STATE;
            } else {
                state_.begin(esp_timer_get_time());
                request->result = Network::setStreamingPower(true);
                if (request->result != ESP_OK) {
                    state_.cameraFailed(request->result);
                    state_.finish(true);
                }
            }
        } else if ((request->operation == Operation::finish)
                   || (request->operation == Operation::finishFailed)) {
            static_cast<void>(state_.shouldStop(esp_timer_get_time()));
            request->result = store_.sleep();
            const auto power = Network::setStreamingPower(false);
            if (power != ESP_OK) { ESP_LOGW("capture", "Modem sleep restore failed"); }
            if (request->result != ESP_OK) { state_.cameraFailed(request->result); }
            state_.finish((request->operation == Operation::finishFailed)
                          || (request->result != ESP_OK));
        } else {
            const bool standalone = (request->operation == Operation::snapshot)
                && !state_.snapshot().streaming;
            auto result = standalone ? Network::setStreamingPower(true) : ESP_OK;
            if (result == ESP_OK) {
                result = store_.capture(request->operation == Operation::snapshot);
            }
            if (standalone) {
                const auto power = Network::setStreamingPower(false);
                if (power != ESP_OK) { ESP_LOGW("capture", "Modem sleep restore failed"); }
            }
            request->result = result;
        }
        // Copy before signaling: caller may immediately destroy its request.
        const auto completion = request->done;
        static_cast<void>(xSemaphoreGive(completion));
    }
}
} // namespace cam
