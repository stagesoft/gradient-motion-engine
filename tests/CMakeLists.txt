# ***
# SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
# SPDX-License-Identifier: GPL-3.0-or-later
# ***

add_executable(test_curves test_curves.cpp)

target_include_directories(test_curves PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_curves PRIVATE
    gradient_motion
)

add_test(NAME test_curves COMMAND test_curves)
set_tests_properties(test_curves PROPERTIES LABELS "unit")

# ── MtcTickSource tests (no MIDI hardware required) ──────────────────────────
add_executable(test_mtc_tick_source test_mtc_tick_source.cpp)

target_include_directories(test_mtc_tick_source PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(test_mtc_tick_source PRIVATE
    gradient_motion
    mtcreceiver
    rtmidi
)

target_compile_definitions(test_mtc_tick_source PRIVATE MTCRECV_TESTING)

add_test(NAME test_mtc_tick_source COMMAND test_mtc_tick_source)
set_tests_properties(test_mtc_tick_source PROPERTIES LABELS "unit")

# ── LockFreeQueue tests ───────────────────────────────────────────────────────
add_executable(test_lockfree_queue test_lockfree_queue.cpp)

target_include_directories(test_lockfree_queue PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(test_lockfree_queue PRIVATE
    gradient_motion
)

add_test(NAME test_lockfree_queue COMMAND test_lockfree_queue)
set_tests_properties(test_lockfree_queue PROPERTIES LABELS "unit")

# ── OSC parser unit tests (no network, no MIDI) ──────────────────────────────
if(LIBLO_FOUND)
    add_executable(test_osc_parse test_osc_parse.cpp)

    target_include_directories(test_osc_parse PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}
    )

    target_link_libraries(test_osc_parse PRIVATE
        gradient_motion
        ${LIBLO_LIBRARIES}
    )

    add_test(NAME test_osc_parse COMMAND test_osc_parse)
    set_tests_properties(test_osc_parse PROPERTIES LABELS "unit")
endif()

# ── FadeMotion behavioural regression tests (ported from test_fade_registry) ──
if(BUILD_DAEMON AND LIBLO_FOUND)
    add_executable(test_fade_motion test_fade_motion.cpp)

    target_include_directories(test_fade_motion PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}
    )

    target_link_libraries(test_fade_motion PRIVATE
        gradient_motion
        mtcreceiver
        ${RTMIDI_LIBRARIES}
        ${LIBLO_LIBRARIES}
        pthread
    )

    target_compile_definitions(test_fade_motion PRIVATE MTCRECV_TESTING)

    add_test(NAME test_fade_motion COMMAND test_fade_motion)
    set_tests_properties(test_fade_motion PROPERTIES LABELS "unit")

    # ── MotionRegistry lifecycle tests (TestMotion double, no OSC) ───────────
    add_executable(test_motion_registry test_motion_registry.cpp)

    target_include_directories(test_motion_registry PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}
    )

    target_link_libraries(test_motion_registry PRIVATE
        gradient_motion
        mtcreceiver
        ${RTMIDI_LIBRARIES}
        ${LIBLO_LIBRARIES}
        pthread
    )

    target_compile_definitions(test_motion_registry PRIVATE MTCRECV_TESTING)

    add_test(NAME test_motion_registry COMMAND test_motion_registry)
    set_tests_properties(test_motion_registry PROPERTIES LABELS "unit")

    # ── OscServer integration test (real loopback, opt-in) ──────────────────
    add_executable(test_osc_server_integration
        test_osc_server_integration.cpp
        ${CMAKE_SOURCE_DIR}/daemon/comms/OscServer.cpp
    )

    target_include_directories(test_osc_server_integration PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/daemon
    )

    target_link_libraries(test_osc_server_integration PRIVATE
        gradient_motion
        ${LIBLO_LIBRARIES}
        pthread
    )

    add_test(NAME test_osc_server_integration COMMAND test_osc_server_integration)
    set_tests_properties(test_osc_server_integration PROPERTIES LABELS "integration")

    # ── OSC latency benchmark (spec SC-002) ─────────────────────────────────
    add_executable(bench_osc_latency
        bench_osc_latency.cpp
        ${CMAKE_SOURCE_DIR}/daemon/comms/OscServer.cpp
    )

    target_include_directories(bench_osc_latency PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/daemon
    )

    target_link_libraries(bench_osc_latency PRIVATE
        gradient_motion
        ${LIBLO_LIBRARIES}
        pthread
    )

    target_compile_definitions(bench_osc_latency PRIVATE
        BENCH_RESULTS_FILE="${CMAKE_SOURCE_DIR}/tests/bench_results/osc_latency.txt"
    )

    add_test(NAME bench_osc_latency COMMAND bench_osc_latency)
    set_tests_properties(bench_osc_latency PROPERTIES LABELS "bench")

    # ── OSC loopback benchmark (SC-007) ──────────────────────────────────────
    add_executable(test_motion_registry_bench test_motion_registry_bench.cpp)

    target_include_directories(test_motion_registry_bench PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}
    )

    target_link_libraries(test_motion_registry_bench PRIVATE
        gradient_motion
        mtcreceiver
        ${RTMIDI_LIBRARIES}
        ${LIBLO_LIBRARIES}
        pthread
    )

    target_compile_definitions(test_motion_registry_bench PRIVATE MTCRECV_TESTING)

    add_test(NAME test_motion_registry_bench COMMAND test_motion_registry_bench)
    set_tests_properties(test_motion_registry_bench PROPERTIES LABELS "integration")
endif()
