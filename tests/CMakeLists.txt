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

add_test(NAME test_mtc_tick_source COMMAND test_mtc_tick_source)
set_tests_properties(test_mtc_tick_source PROPERTIES LABELS "unit")
