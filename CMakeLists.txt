# ***
# SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
# SPDX-License-Identifier: GPL-3.0-or-later
# ***
#
# Copyright (C) 2026 Stagelab Coop SCCL
# Authors: Adrià Masip <adria@stagelab.coop>
#
# This file is part of gradient-motion-engine.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.10)
# Allow target_link_libraries() on targets defined in other directories
# (needed to link cuemslogger into mtcreceiver from the root CMakeLists)
cmake_policy(SET CMP0079 NEW)
project(gradient-motion-engine VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
add_compile_options(-Wall -O3 -pthread)

# ===========================================================
# Build options
# ===========================================================
option(ENABLE_CUEMS_LOGGER "Build with CuemsLogger syslog backend" ON)
option(BUILD_DAEMON "Build the gradient-motiond daemon (requires rtmidi)" ON)

# ===========================================================
# External dependencies via pkg-config
# ===========================================================
find_package(PkgConfig REQUIRED)

# On Debian/Ubuntu the multiarch pkgconfig directory is not always in the
# default PKG_CONFIG_PATH. Append it when CMake reports a library architecture
# (e.g. x86_64-linux-gnu, aarch64-linux-gnu). No-op on macOS / non-multiarch.
if(CMAKE_LIBRARY_ARCHITECTURE)
    set(ENV{PKG_CONFIG_PATH}
        "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/pkgconfig:$ENV{PKG_CONFIG_PATH}")
endif()

if(BUILD_DAEMON)
    # Required for daemon: RtMidi (needed by mtcreceiver)
    pkg_check_modules(RTMIDI REQUIRED rtmidi)
endif()

pkg_check_modules(TINYXML2 tinyxml2)
pkg_check_modules(LIBLO liblo)

# Header-only: nlohmann-json
find_package(nlohmann_json QUIET)

# ===========================================================
# Submodule presence checks
# ===========================================================
if(BUILD_DAEMON)
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
endif()

# ===========================================================
# Submodule targets
# ===========================================================
if(BUILD_DAEMON)
    if(ENABLE_CUEMS_LOGGER)
        add_subdirectory(cuemslogger)
    endif()

    # Enable test-only helpers so the test suite can use invokeTickForTesting
    # and the SkipPortOpenTag constructor (FR-007).
    set(MTCRECV_TESTING ON CACHE BOOL "Expose mtcreceiver test-only helpers" FORCE)
    add_subdirectory(mtcreceiver)

    if(ENABLE_CUEMS_LOGGER)
        target_compile_definitions(mtcreceiver PRIVATE HAVE_CUEMS_LOGGER)
        target_link_libraries(mtcreceiver cuemslogger)
    endif()
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
    # Show file paths in HTML relative to the repo root (not absolute build-tree paths)
    set(DOXYGEN_STRIP_FROM_PATH "${CMAKE_SOURCE_DIR}")
    doxygen_add_docs(docs
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/daemon"
        COMMENT "Generating API documentation with Doxygen (lib + daemon)"
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
# Tests
# ===========================================================
enable_testing()
add_subdirectory(tests)

# ===========================================================
# Version header (generated from project VERSION above)
# ===========================================================
configure_file(
    "${CMAKE_SOURCE_DIR}/daemon/config/version.h.in"
    "${CMAKE_BINARY_DIR}/daemon/config/version.h"
)

# ===========================================================
# Daemon executable
# ===========================================================
if(BUILD_DAEMON)
    add_executable(gradient-motiond
        daemon/main.cpp
        daemon/GradientEngineApplication.cpp
        daemon/config/ConfigurationManager.cpp
        daemon/comms/OscServer.cpp
        src/engine/GradientEngine.cpp
    )

    target_include_directories(gradient-motiond PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/daemon
        ${CMAKE_BINARY_DIR}/daemon/config
    )

    target_link_libraries(gradient-motiond PRIVATE
        gradient_motion
        mtcreceiver
        ${RTMIDI_LIBRARIES}
        pthread
    )

    if(LIBLO_FOUND)
        target_link_libraries(gradient-motiond PRIVATE ${LIBLO_LIBRARIES})
        target_include_directories(gradient-motiond PRIVATE ${LIBLO_INCLUDE_DIRS})
    endif()

    if(ENABLE_CUEMS_LOGGER)
        target_compile_definitions(gradient-motiond PRIVATE HAVE_CUEMS_LOGGER)
        target_link_libraries(gradient-motiond PRIVATE cuemslogger)
    endif()

    # Install rules — places the daemon at <prefix>/bin/gradient-motiond.
    # debian/rules sets CMAKE_INSTALL_PREFIX=/usr so the .deb ships
    # /usr/bin/gradient-motiond, matching the path that
    # cuems-common's cuems-gradient-motiond.service ExecStart= expects.
    include(GNUInstallDirs)
    install(TARGETS gradient-motiond
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
