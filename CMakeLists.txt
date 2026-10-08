cmake_minimum_required(VERSION 3.28)

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Target architecture (arm64 only)" FORCE)
endif()

project(ptsl_gui VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(PTSLGUI_BUILD_TESTS "Build unit tests" ON)
option(PTSLGUI_BUILD_UI "Build the Qt user interface (macOS)" ${APPLE})
option(PTSLGUI_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(PTSLGUI_CLANG_TIDY "Run clang-tidy while compiling" OFF)
set(PTSLGUI_SANITIZE "" CACHE STRING "Sanitizers to enable: address, undefined, thread (comma separated)")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(PtslGuiOptions)
include(PtslGuiSdk)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(protobuf CONFIG QUIET)
if(NOT protobuf_FOUND)
    find_package(Protobuf REQUIRED)
endif()
find_package(nlohmann_json 3.11 REQUIRED)

if(PTSLGUI_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE};--warnings-as-errors=*")
endif()

add_subdirectory(core)

if(PTSLGUI_BUILD_UI)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Concurrent Test)
    qt_standard_project_setup()
    add_subdirectory(ui)
    if(PTSLGUI_SDK_FOUND)
        add_subdirectory(sdk)
        add_subdirectory(app)
    else()
        message(STATUS "PTSL GUI app disabled: PTSL SDK client framework not available")
    endif()
endif()

if(PTSLGUI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
