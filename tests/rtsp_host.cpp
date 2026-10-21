/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "rtsp_server.hpp"
#include "rtsp_protocol.hpp"
#include "host_sdk.hpp"
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
int main(int argc, char** argv)
{
    std::signal(SIGPIPE, SIG_IGN); // lwIP returns an error; POSIX otherwise raises SIGPIPE.
    if (argc < 2) { return 1; }
    if (!loadCameraFixture(argv[1])) { return 1; }
    static cam::Camera camera{};
    static cam::SharedState state{cam::StreamPolicy{true, (argc > 2) ? 2U : 1200U}};
    static cam::FrameService frames{camera, state};
    static cam::RtspServer server{frames, state, 8554U};
    if ((frames.start() != ESP_OK) || (server.start() != ESP_OK)) { return 2; }
    std::puts("READY"); std::fflush(stdout);
    std::string command;
    while (std::getline(std::cin, command)) {
        if (command == "begin") {
            std::printf("{\"result\":%d}\n", frames.begin());
        } else if (command == "finish") {
            std::printf("{\"result\":%d}\n", frames.finish(false));
        } else if (command == "snapshot") {
            const auto result = frames.capture(true);
            cam::rtsp::Jpeg jpeg{};
            const auto valid = (result == ESP_OK) && cam::rtsp::parseJpeg(frames.snapshotView(), jpeg);
            std::printf("{\"snapshot\":%s}\n", valid ? "true" : "false");
        } else if (command == "stop") {
            state.requestStop();
            std::puts("{\"stopping\":true}");
        } else if (command == "status") {
            const auto status = state.snapshot();
            std::printf("{\"streaming\":%s,\"timed_out\":%s,\"error\":%d}\n",
                status.streaming ? "true" : "false", status.streamTimedOut ? "true" : "false", status.cameraError);
        }
        std::fflush(stdout);
    }
    std::_Exit(0); // Test process owns detached process-lifetime SDK task fakes.
}
