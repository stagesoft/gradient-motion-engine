set(GME_SOURCES
    time/time.cpp
    time/MtcTickSource.cpp
    motion/motion.cpp
    gradient/SigmoidCurve.cpp
    gradient/BezierCurve.cpp
    gradient/ResampledCurve.cpp
    gradient/CurveFactory.cpp
    signal/signal.cpp
    signal/FadeCommand.cpp
    osc/osc.cpp
    engine/engine.cpp
)

add_library(gradient_motion STATIC ${GME_SOURCES})

target_include_directories(gradient_motion PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(gradient_motion PUBLIC
    nlohmann_json::nlohmann_json
)
