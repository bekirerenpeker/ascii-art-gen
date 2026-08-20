# asciigen_cli -- the executable, links the asciigen static library.
#
# Same include/src split as asciigen/: headers under include/, sources under
# src/, included from either side as the include/ root, e.g.
# include/argparser.hpp is #include "argparser.hpp" from src/main.cpp.

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(asciigen_cli ${APP_SOURCES})
target_include_directories(asciigen_cli PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(asciigen_cli PRIVATE asciigen)

# Absolute path to the repo root, baked in at configure time, so hardcoded test
# paths resolve no matter which directory the binary is launched from. Scratch-
# testing convenience only -- real inputs come from argv, where CWD-relative is
# the correct behaviour.
target_compile_definitions(asciigen_cli PRIVATE SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# Stamped into every trace, so a Debug timing is never mistaken for a Release one.
target_compile_definitions(asciigen_cli PRIVATE
    ASCIIGEN_BUILD_TYPE="${CMAKE_BUILD_TYPE}")

# Default font candidates, per OS, tried in order at runtime -- the first that
# exists wins. Detected here rather than guessed at runtime, and kept as a list
# rather than one path because distributions disagree about where fonts live.
#
# This is why no font needs to be redistributed with the project: every desktop
# already has a monospace face, and shipping someone else's font means shipping
# their licence terms along with it.
if(WIN32)
    set(ASCIIGEN_FONTS
        "C:/Windows/Fonts/consola.ttf"
        "C:/Windows/Fonts/CascadiaMono.ttf"
        "C:/Windows/Fonts/lucon.ttf"
        "C:/Windows/Fonts/cour.ttf")
elseif(APPLE)
    set(ASCIIGEN_FONTS
        "/System/Library/Fonts/Menlo.ttc"
        "/System/Library/Fonts/SFNSMono.ttf"
        "/System/Library/Fonts/Monaco.ttf"
        "/Library/Fonts/Courier New.ttf")
else()
    set(ASCIIGEN_FONTS
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf"
        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf"
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf"
        "/usr/share/fonts/liberation/LiberationMono-Regular.ttf"
        "/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf")
endif()

# Joined with '|' because a semicolon is CMake's own list separator and would
# not survive being written into a compile definition.
string(REPLACE ";" "|" ASCIIGEN_FONTS_JOINED "${ASCIIGEN_FONTS}")
target_compile_definitions(asciigen_cli PRIVATE DEFAULT_FONTS="${ASCIIGEN_FONTS_JOINED}")

# Target is asciigen_cli (distinct from the asciigen library target), but the
# binary you actually run stays named asciigen(.exe).
set_target_properties(asciigen_cli PROPERTIES OUTPUT_NAME asciigen)

if(MSVC)
    target_compile_options(asciigen_cli PRIVATE /W4 /permissive-)
else()
    target_compile_options(asciigen_cli PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
endif()

# Installation. Nothing here runs during a build -- `cmake --install <builddir>`
# is the only thing that triggers it, which is what makes installing explicit.
#
# Layout matches what Assets.cpp looks for at runtime:
#     <prefix>/bin/asciigen.exe
#     <prefix>/share/asciigen/help/*.txt
#     <prefix>/share/asciigen/fonts/*.ttf
#
# The binary finds those relative to itself, so an installed copy keeps working
# after the source tree moves or goes away.
include(GNUInstallDirs)

install(TARGETS asciigen_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(
    DIRECTORY "${CMAKE_SOURCE_DIR}/assets/help" "${CMAKE_SOURCE_DIR}/assets/fonts"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/asciigen
)
