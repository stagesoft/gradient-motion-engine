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

target_link_libraries(gradient_motion PUBLIC
    nlohmann_json::nlohmann_json
)

if(LIBLO_FOUND)
    target_link_libraries(gradient_motion PUBLIC ${LIBLO_LIBRARIES})
    target_include_directories(gradient_motion PUBLIC ${LIBLO_INCLUDE_DIRS})
else()
    message(FATAL_ERROR "liblo not found — required for gme::osc. Install liblo-dev.")
endif()
