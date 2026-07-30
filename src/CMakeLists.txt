# ***
# SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
# SPDX-License-Identifier: GPL-3.0-or-later
# ***

set(GME_SOURCES
    time/MtcTickSource.cpp
    gradient/SigmoidCurve.cpp
    gradient/BezierCurve.cpp
    gradient/ResampledCurve.cpp
    gradient/CurveFactory.cpp
    signal/FadeCommand.cpp
    signal/parseFadeOscCommand.cpp
    osc/OscSender.cpp
    motion/MotionRegistry.cpp
    motion/FadeMotion.cpp
    motion/MotionFactory.cpp
)

add_library(gradient_motion STATIC ${GME_SOURCES})

target_include_directories(gradient_motion PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)

# Arm the CuemsLogger backend on the LIBRARY, not just the daemon executable.
# Previously only mtcreceiver and gradient-motiond carried HAVE_CUEMS_LOGGER,
# so every GME_LOG_* compiled into this library fell back to the std::cerr
# emitter — under systemd those lines are stamped PRIORITY=6 and invisible to
# `cuems-logs -e` — and GradientEngine.cpp, listed in BOTH targets, was
# compiled twice with two different logging backends. PUBLIC on purpose: the
# daemon and the test binaries then share one backend (side effect: ctest
# output for the 5 test binaries goes to syslog when cuemslogger is enabled).
# The TARGET guard covers BUILD_DAEMON=OFF, where cuemslogger is never added.
if(ENABLE_CUEMS_LOGGER AND TARGET cuemslogger)
    target_compile_definitions(gradient_motion PUBLIC HAVE_CUEMS_LOGGER)
    target_link_libraries(gradient_motion PUBLIC cuemslogger)
endif()

target_link_libraries(gradient_motion PUBLIC
    nlohmann_json::nlohmann_json
)

if(LIBLO_FOUND)
    target_link_libraries(gradient_motion PUBLIC ${LIBLO_LIBRARIES})
    target_include_directories(gradient_motion PUBLIC ${LIBLO_INCLUDE_DIRS})
else()
    message(FATAL_ERROR "liblo not found — required for gme::osc. Install liblo-dev.")
endif()
