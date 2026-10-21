#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_dir="$project_dir/build/host-tests"
mkdir -p "$test_dir"
# Host stubs are isolated from the real IDF build. GNU++26 matches firmware;
# sanitizers exercise bounds/lifetime errors in the tested production modules.
"${CXX:-g++}" -std=gnu++26 -Wall -Wextra -Wpedantic -Wconversion \
    -Wsign-conversion -Wshadow -Werror -pthread -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$project_dir/tests/fakes" -I "$project_dir/main" \
    "$project_dir/tests/firmware_tests.cpp" "$project_dir/main/model.cpp" \
    "$project_dir/main/shared_state.cpp" "$project_dir/main/camera.cpp" \
    "$project_dir/main/frame_store.cpp" \
    -o "$test_dir/firmware_tests"
"$test_dir/firmware_tests"
"${CXX:-g++}" -std=gnu++26 -Wall -Wextra -Wpedantic -Wconversion \
    -Wsign-conversion -Wshadow -Werror -g -fsanitize=address,undefined \
    -I "$project_dir/main" "$project_dir/tests/http_stream_tests.cpp" \
    -o "$test_dir/http_stream_tests"
"$test_dir/http_stream_tests"
"${CXX:-g++}" -std=gnu++26 -Wall -Wextra -Wpedantic -Wconversion \
    -Wsign-conversion -Wshadow -Werror -g -fsanitize=address,undefined \
    -I "$project_dir/tests/fakes" -I "$project_dir/main" \
    "$project_dir/tests/configuration_tests.cpp" "$project_dir/main/configuration.cpp" \
    -o "$test_dir/configuration_tests"
"$test_dir/configuration_tests"
node "$project_dir/tests/web_flash_tests.mjs"
node "$project_dir/tests/browser_tests.mjs"
"${CONFIG_TEST_PYTHON:-python3}" "$project_dir/tests/wifi_config_tests.py"
python3 - "$test_dir/rtsp-fixture.jpg" <<'PY'
import sys
from PIL import Image
image = Image.new('RGB', (640, 480))
pixels = image.load()
for y in range(480):
    for x in range(640):
        pixels[x, y] = ((x * 7) % 256, (y * 3) % 256, (x + y) % 256)
image.save(sys.argv[1], 'JPEG', quality=85, subsampling=1)
PY
"${CXX:-g++}" -std=gnu++26 -Wall -Wextra -Wpedantic -Wconversion \
    -Wsign-conversion -Wshadow -Werror -pthread -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$project_dir/tests/fakes" -I "$project_dir/main" \
    "$project_dir/tests/rtsp_tests.cpp" "$project_dir/main/rtsp_protocol.cpp" \
    -o "$test_dir/rtsp_tests"
"$test_dir/rtsp_tests" "$test_dir/rtsp-fixture.jpg"
"${CXX:-g++}" -std=gnu++26 -Wall -Wextra -Wpedantic -Wconversion \
    -Wsign-conversion -Wshadow -Werror -pthread -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$project_dir/tests/fakes" -I "$project_dir/main" \
    "$project_dir/tests/rtsp_host.cpp" "$project_dir/tests/host_sdk.cpp" "$project_dir/main/rtsp_server.cpp" \
    "$project_dir/main/rtsp_protocol.cpp" "$project_dir/main/frame_service.cpp" \
    "$project_dir/main/frame_store.cpp" "$project_dir/main/camera.cpp" \
    "$project_dir/main/shared_state.cpp" "$project_dir/main/model.cpp" \
    -o "$test_dir/rtsp_host"
python3 "$project_dir/tests/rtsp_integration.py" "$test_dir/rtsp_host" "$test_dir/rtsp-fixture.jpg"
node "$project_dir/tests/webrtc_browser_tests.mjs"
"${CXX:-g++}" -std=gnu++26 -Wall -Wextra -Wpedantic -Wconversion \
    -Wsign-conversion -Wshadow -Werror -g -fsanitize=address,undefined \
    -I "$project_dir/main" "$project_dir/tests/webrtc_protocol_tests.cpp" \
    -o "$test_dir/webrtc_protocol_tests"
"$test_dir/webrtc_protocol_tests"
"${CXX:-g++}" -std=gnu++26 -Wall -Wextra -Wpedantic -Wconversion \
    -Wsign-conversion -Wshadow -Werror -g -fsanitize=address,undefined \
    -I "$project_dir/main" "$project_dir/tests/webrtc_sdp_tests.cpp" \
    -o "$test_dir/webrtc_sdp_tests"
"$test_dir/webrtc_sdp_tests"
"${CXX:-g++}" -std=gnu++26 -Wall -Wextra -Wpedantic -Wconversion \
    -Wsign-conversion -Wshadow -Werror -pthread -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$project_dir/tests/fakes" -I "$project_dir/main" \
    "$project_dir/tests/webrtc_lifecycle_tests.cpp" "$project_dir/tests/host_sdk.cpp" \
    "$project_dir/main/webrtc_server.cpp" "$project_dir/main/frame_service.cpp" \
    "$project_dir/main/frame_store.cpp" "$project_dir/main/camera.cpp" \
    "$project_dir/main/shared_state.cpp" "$project_dir/main/model.cpp" \
    -o "$test_dir/webrtc_lifecycle_tests"
"$test_dir/webrtc_lifecycle_tests" "$test_dir/rtsp-fixture.jpg"
