add_executable(test_curves test_curves.cpp)

target_include_directories(test_curves PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_curves PRIVATE
    gradient_motion
)

add_test(NAME test_curves COMMAND test_curves)
set_tests_properties(test_curves PROPERTIES LABELS "unit")
