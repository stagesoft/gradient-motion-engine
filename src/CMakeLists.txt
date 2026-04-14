set(GME_SOURCES
    time/time.cpp
    motion/motion.cpp
    gradient/gradient.cpp
    signal/signal.cpp
    osc/osc.cpp
    engine/engine.cpp
)

add_library(gradient_motion STATIC ${GME_SOURCES})

target_include_directories(gradient_motion PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
