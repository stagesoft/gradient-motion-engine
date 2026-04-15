cmake_minimum_required(VERSION 3.10)
# Allow target_link_libraries() on targets defined in other directories
# (needed to link cuemslogger into mtcreceiver from the root CMakeLists)
cmake_policy(SET CMP0079 NEW)
project(gradient-motion-engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
add_compile_options(-Wall -O3 -pthread)

# ===========================================================
# External dependencies via pkg-config
# ===========================================================
find_package(PkgConfig REQUIRED)

# Required: RtMidi (needed by mtcreceiver)
pkg_check_modules(RTMIDI REQUIRED rtmidi)

# Optional in Phase 0 — checked for availability, not linked yet
pkg_check_modules(NNG nng)
pkg_check_modules(TINYXML2 tinyxml2)
pkg_check_modules(LIBLO liblo)

# Header-only: nlohmann-json
find_package(nlohmann_json QUIET)

# ===========================================================
# CuemsLogger option
# ===========================================================
option(ENABLE_CUEMS_LOGGER "Build with CuemsLogger syslog backend" ON)

# ===========================================================
# Submodule presence checks
# ===========================================================
if(ENABLE_CUEMS_LOGGER)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/cuemslogger/CMakeLists.txt")
        message(FATAL_ERROR
            "cuemslogger submodule not found. "
            "Run: git submodule update --init --recursive")
    endif()
endif()

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/mtcreceiver/CMakeLists.txt")
    message(FATAL_ERROR
        "mtcreceiver submodule not found. "
        "Run: git submodule update --init --recursive")
endif()

# ===========================================================
# Submodule targets
# ===========================================================
if(ENABLE_CUEMS_LOGGER)
    add_subdirectory(cuemslogger)
endif()

add_subdirectory(mtcreceiver)

if(ENABLE_CUEMS_LOGGER)
    target_compile_definitions(mtcreceiver PRIVATE HAVE_CUEMS_LOGGER)
    target_link_libraries(mtcreceiver cuemslogger)
endif()

# ===========================================================
# Documentation (Doxygen)
# ===========================================================
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    set(DOXYGEN_PROJECT_NAME "gradient-motion-engine")
    set(DOXYGEN_PROJECT_BRIEF "Node-level fade engine with mathematical curve evaluation")
    set(DOXYGEN_RECURSIVE YES)
    set(DOXYGEN_EXTRACT_ALL YES)
    set(DOXYGEN_WARN_AS_ERROR YES)
    set(DOXYGEN_GENERATE_HTML YES)
    set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/docs")
    doxygen_add_docs(docs
        "${CMAKE_SOURCE_DIR}/src"
        COMMENT "Generating API documentation with Doxygen"
    )
    message(STATUS "Doxygen found — run 'cmake --build . --target docs' to generate API docs")
else()
    message(STATUS "Doxygen not found — 'docs' target unavailable. Install doxygen to enable API doc generation.")
endif()

# ===========================================================
# Library target
# ===========================================================
add_subdirectory(src)

# ===========================================================
# Daemon executable
# ===========================================================
add_executable(gradient-motiond
    daemon/main.cpp
    daemon/GradientEngineApplication.cpp
    daemon/config/ConfigurationManager.cpp
)

target_include_directories(gradient-motiond PRIVATE
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(gradient-motiond PRIVATE
    gradient_motion
    mtcreceiver
    ${RTMIDI_LIBRARIES}
    pthread
)

if(ENABLE_CUEMS_LOGGER)
    target_compile_definitions(gradient-motiond PRIVATE HAVE_CUEMS_LOGGER)
    target_link_libraries(gradient-motiond PRIVATE cuemslogger)
endif()
