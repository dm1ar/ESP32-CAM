# Firmware coding guidelines

## Baseline and scope

First-party firmware is C++ built with stable ESP-IDF v6.1 and the ESP32
GCC 15.2.0 toolchain, explicitly using `-std=gnu++26`. The language mode means
the compiler's implemented C++26 features plus GNU extensions; it is not a
promise that the compiler implements every future language feature.

The safety baseline is **MISRA C++:2023 (TC1:2026)**, whose published
scope remains C++17. MISRA's current publication incorporates technical
corrigenda without adding new guidelines.
The corresponding current C baseline is **MISRA C:2025** for reviews of C
interfaces. There are no remaining first-party firmware `.c` files. ESP-IDF
and managed camera/JPEG C sources retain their upstream languages and rules.
Python build helpers and browser JavaScript follow local defensive coding
conventions; MISRA C/C++ does not apply to those languages.

This is a MISRA-oriented project coding guideline, **not a MISRA compliance
claim**. GNU++26 differs from the MISRA C++ language baseline. No licensed
MISRA rule checker, complete rule-by-rule assessment, or compliance process
has been run. The local decisions below must not be mistaken for official
MISRA rule text or rule identifiers.

## Required conventions

1. Give each module one responsibility. Use private class state, scoped enums,
   named namespaces and `constexpr` constants. No application-wide mutable
   global state; the sole application object has static storage in `app_main`.
2. Value-initialize all SDK structures. Avoid partially initialized objects,
   uninitialized scalars and relying on SDK enum values for application indexing.
3. Use fixed-width integers for counters and protocol values, `size_t` for
   buffer sizes, signed microseconds for ESP timers, and explicit checked
   conversions at interfaces. Use the matching `PRI*`/`%zu` format specifiers.
4. Validate an entire request before changing settings. Accept only the five
   known resolutions and quality 0–63. Bound request size and receive counts;
   reject unknown fields, duplicates, embedded NULs, overflow and trailing junk.
5. Use `std::array` or fixed SDK arrays. Do not allocate first-party firmware
   objects with `new`, `delete`, `malloc` or `free`; the bounded startup-only
   PSRAM byte buffers have the explicit D09 exception below. Avoid allocating STL
   containers and strings in request/capture paths. Use bounded `string_view`
   only while its backing storage is alive.
6. Express ownership through object lifetime. Delete copying for hardware,
   synchronization and server owners. `FrameLease` returns exactly one borrowed
   buffer. `StreamSession` requests worker cleanup on all returns.
7. Keep camera initialization, capture and deinitialization in the capture
   worker after boot. Return every frame before reconfiguration. Never manipulate
   the camera or its GPIO from page, control, stop or status handlers.
8. Read/write telemetry only through `SharedState`. FrameStore data belongs
   to the worker; HTTP handlers read only their own completed output buffers.
   The SharedState RAII spinlock guard encloses only bounded memory operations. No I/O, allocation, logging,
   delay, HTTP transmission or driver call may occur under that lock.
9. Return and inspect `esp_err_t` for recoverable operations. Mark returned
   results `[[nodiscard]]`. Handle HTTP header/send failures and initialization
   failures. Log cleanup failures; attempt both camera and Wi-Fi cleanup even
   if one fails. Fatal checks are reserved for startup and startup rollback.
10. Use `noexcept` on firmware functions. Exceptions and RTTI are explicitly
    disabled. Do not use recursion, `goto`, C-style casts, unions for type
    punning or unbounded string functions. Owning raw pointers are confined
    to JpegBuffer's RAII PSRAM boundary (D09).
11. Keep callbacks small. Check opaque SDK pointers before casting, register
    only correctly typed callback contexts and keep their objects alive until
    reset. Any borrowed request/frame pointer stays inside its callback scope.
12. Document intent, ownership, units, bounded ranges and recovery constraints
    in English. Add braces to conditional/loop bodies. Prefer clear early
    error returns over deeply nested conditions. Keep secrets out of logs.
13. Compute FPS/throughput from complete sent frames only. Reset on session
    start, reconfiguration and finish; expire stale readings after three
    seconds. Browser polling and rendering rates are separate quantities.
14. Treat dependencies as separate review boundaries. Preserve the generated
    dependency lock and pinned camera version. Never patch managed SDK code
    merely to hide a first-party compiler diagnostic.

## Documented deviations and boundary decisions

These IDs are project records, not MISRA rule numbers. Review them whenever
the toolchain, SDK or relevant boundary code changes.

| ID | Decision and reason | Containment and evidence |
| --- | --- | --- |
| D01 | GNU++26 is required by the task although MISRA C++:2023 covers C++17. Defaulted equality, std::span and string_view::starts_with use C++20 features. | Exact compiler mode in `main/CMakeLists.txt`; build metadata verifies all first-party sources. Most code uses the C++17 subset. Formal conformance requires separate assessment. |
| D02 | ESP-IDF uses non-owning pointers, `void*` callback contexts, varargs logging/formatting and byte-to-`char` socket buffers. | Casts are confined to SDK adapters; callbacks check null; pointer lifetime is documented; bounded `snprintf` results and send lengths are checked. `reinterpret_cast` is confined to JPEG/protocol byte views and the POSIX/lwIP sockaddr adapter boundary. |
| D03 | Embedded HTML uses GNU assembler symbol names and linker-defined start/end addresses. Subtracting these symbols is an SDK linker convention outside portable C++ array provenance. | Only `web_server.cpp` uses the symbols; `EMBED_TXTFILES` supplies a contiguous NUL-terminated payload. No general inline assembly is used. |
| D04 | SDK macros and headers use C constructs/GNU extensions, including `#include_next` and flexible arrays. | Firmware enables strict conversion/sign/shadow/format warnings and `-Werror`, but not `-Wpedantic`, which rejects SDK headers. Host tests add `-Wpedantic`. SDK default warning exclusions remain inherited. This is not a MISRA checker. |
| D05 | Wi-Fi, HTTP, FreeRTOS and camera drivers allocate memory internally; an MJPEG server needs driver-managed frame buffers. | Except for the explicit D09 PSRAM buffers, no direct application heap allocation; static event group storage; two bounded camera buffers; failed startup is fatal; scoped stream/frame guards guarantee application cleanup attempts. Allocation inside upstream code is not claimed MISRA-compliant. |
| D06 | The SDK requires C-linkage `app_main`, global driver APIs and process-lifetime callbacks. Application members are not shut down on normal `app_main` return. | One static `Application` owns all callback state; reset ends its lifetime. Boot failures fail fast. Wi-Fi acquisition intentionally waits indefinitely in timed 30-second intervals and logs progress. |
| D07 | Small bounded `memcpy` operations transfer generated credentials into SDK byte arrays. A maximum-length SSID/PSK may occupy the full array without a trailing NUL. | TOML validation bounds input; compile-time assertions check destination capacity; credentials are never formatted as C strings or printed. |
| D09 | Three JPEG buffers allocate 512 KiB each from PSRAM at startup using `heap_caps_malloc`, with RAII destruction via `heap_caps_free`. | One cache and independent stream/snapshot copies; bounded copy with null/length checks, no per-frame allocation, no driver lease retained during send. Failure stops startup or returns HTTP 500. Host tests cover copy lifetime, reuse, failure and oversize rejection. |
| D08 (closed) | A temporary legacy SCCB compatibility trial did not resolve the observed OV2640 sensor-write failures. | The legacy backend selection has been reverted in source to the maintained I2C backend. The 10 MHz candidate passed repeated starts and original-JPEG decoding on the replacement board. No active legacy-driver deviation remains in source. |

## Verification and limits

Run `bash tools/test.sh` for production parser/rate/state/camera tests with
AddressSanitizer and UndefinedBehaviorSanitizer, a host mutex concurrency
exercise, browser controller tests, RTSP parser/packetizer corruption tests, and
production RTSP socket/worker tests decoded independently by FFmpeg. The
RTSP host tests require Pillow and FFmpeg and use TCP port 8554 plus UDP
5004/5005. Camera/GPIO and RTOS functions are
host fakes here; they test application contracts rather than electrical or
real-time behavior. Run `bash tools/idf.sh build` for the actual ESP32 ABI,
SDK headers, format checking, linker and partition-size checks.

Before claiming behavior on the board, flash the complete build (including
the updated partition table), remove GPIO0-to-GND, reset, read the DHCP URL
from UART, and run the procedure in `VALIDATION.md`. Compilation and mocked
tests cannot establish power stability, I2C recovery, real FreeRTOS timing,
Wi-Fi operation or camera image quality. A licensed standard, qualified
analysis tools and a full documented assessment would be needed for any
future MISRA compliance claim.

## References

- [MISRA C++ baseline and publications](https://misra.org.uk/misra-cpp/)
- [MISRA C publications](https://misra.org.uk/misra-c/)
- [ESP-IDF v6.1 C++ language, linkage and limitations](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-guides/cplusplus.html)
- [ESP-IDF v6.1 release](https://github.com/espressif/esp-idf/releases/tag/v6.1)

RTSP unsigned RTP sequence, timestamp and packet/octet counters intentionally
wrap according to their wire widths. Standard JPEG Huffman tables are checked
with structural bounds and FNV-1a fingerprints; this is a format compatibility
check for sensor output, not a cryptographic integrity guarantee. RTSP sockets
are scoped resources, while startup listeners/tasks have application lifetime.
No allocation occurs per RTSP packet/request. Host socket adapters alone
ignore POSIX SIGPIPE to match lwIP's send-error behavior.
