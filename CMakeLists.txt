cmake_minimum_required(VERSION 3.21)

project(asciigen
    VERSION 0.1.0
    DESCRIPTION "Image/video to ASCII art converter"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Single-config generators (Ninja) leave this empty, which means "no flags at all".
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Everything lands in <build>/bin so the binary is always at a predictable path.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# For clangd / other tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(lib)        # vendored third-party libraries, one target each
add_subdirectory(asciigen)   # static library: asciigen/include + asciigen/src -> target `asciigen`
add_subdirectory(app)        # executable: app/*.cpp -> target `asciigen_cli`, links `asciigen`

# `cmake --build build --target run` builds if needed, then runs the program.
add_custom_target(run
    COMMAND $<TARGET_FILE:asciigen_cli>
    DEPENDS asciigen_cli
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    USES_TERMINAL
    VERBATIM)
