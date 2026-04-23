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

# ── NNG parse tests (no sockets) ─────────────────────────────────────────────
add_executable(test_nng_parse test_nng_parse.cpp)

target_include_directories(test_nng_parse PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(test_nng_parse PRIVATE
    gradient_motion
)

add_test(NAME test_nng_parse COMMAND test_nng_parse)
set_tests_properties(test_nng_parse PROPERTIES LABELS "unit")

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

# ── NNG integration test (real loopback, opt-in) ──────────────────────────────
if(BUILD_DAEMON)
    add_executable(test_nng_integration
        test_nng_integration.cpp
        ${CMAKE_SOURCE_DIR}/daemon/comms/NngBusClient.cpp
    )

    target_include_directories(test_nng_integration PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/daemon
    )

    target_link_libraries(test_nng_integration PRIVATE
        gradient_motion
        nng::nng
        pthread
    )

    target_compile_definitions(test_nng_integration PRIVATE GME_ENABLE_INTEGRATION_TESTS)

    add_test(NAME test_nng_integration COMMAND test_nng_integration)
    set_tests_properties(test_nng_integration PROPERTIES LABELS "integration")
endif()
