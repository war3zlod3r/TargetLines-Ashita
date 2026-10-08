####################################################################################################
# TargetLines for Ashita v4
#
# Builds the 32-bit plugin module 'targetlines.dll' into ./bin.
#
# Requirements:
#   - Visual Studio 2022 or newer with the C++ desktop workload (MSVC, x86 target).
#   - CMake 3.22 or newer.
#   - The Ashita v4 plugin SDK (the 'plugins/sdk' folder of an Ashita v4 install), located via the
#     ASHITA4_SDK_PATH environment variable or the -DASHITA4_SDK_PATH=<path> cache variable.
####################################################################################################

cmake_minimum_required(VERSION 3.22)

set(ASHITA_PROJECT_NAME "targetlines")
project(${ASHITA_PROJECT_NAME} CXX)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# Allow the SDK path to be passed on the command line instead of the environment..
if (DEFINED ASHITA4_SDK_PATH AND NOT DEFINED ENV{ASHITA4_SDK_PATH})
    set(ENV{ASHITA4_SDK_PATH} "${ASHITA4_SDK_PATH}")
endif()

# Enable parallel building..
if (NOT CMAKE_BUILD_PARALLEL_LEVEL)
    include(ProcessorCount)
    ProcessorCount(N)
    if (NOT N EQUAL 0)
        set(CMAKE_BUILD_PARALLEL_LEVEL ${N})
    endif()
endif()

# Require the Ashita SDK..
find_package(AshitaSDK REQUIRED)
ashita_sdk_initialize()

set(${ASHITA_PROJECT_NAME}_src
    "src/exports.def"
    "src/action_packet.cpp"
    "src/action_packet.hpp"
    "src/game.cpp"
    "src/game.hpp"
    "src/logger.cpp"
    "src/logger.hpp"
    "src/renderer.cpp"
    "src/renderer.hpp"
    "src/settings.cpp"
    "src/settings.hpp"
    "src/state.hpp"
    "src/targetlines.cpp"
    "src/targetlines.hpp"
    "src/tracker.cpp"
    "src/tracker.hpp"
    "src/scenehook/SceneHook.h"
    )

add_library(${ASHITA_PROJECT_NAME} MODULE ${${ASHITA_PROJECT_NAME}_src})
set_target_properties(${ASHITA_PROJECT_NAME} PROPERTIES
    CXX_EXTENSIONS ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    LINKER_LANGUAGE CXX
    OUTPUT_NAME ${ASHITA_PROJECT_NAME}
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    )

target_include_directories(${ASHITA_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(${ASHITA_PROJECT_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)

# Attach and configure the Ashita SDK..
ashita_sdk_attach(${ASHITA_PROJECT_NAME})
ashita_sdk_set_compiler_defaults(${ASHITA_PROJECT_NAME})
ashita_sdk_set_compiler_warn_level(${ASHITA_PROJECT_NAME} 4)
ashita_sdk_set_linker_defaults(${ASHITA_PROJECT_NAME})

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    ashita_sdk_set_linker_enable_debug_linking(${ASHITA_PROJECT_NAME})
else()
    ashita_sdk_set_linker_disable_debug_linking(${ASHITA_PROJECT_NAME})
endif()

# /SAFESEH is intentionally not enabled: the legacy Direct3D 8 import libraries shipped with the
# SDK do not carry SafeSEH tables and the linker refuses to combine them with /SAFESEH.
