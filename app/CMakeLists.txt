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

# Target is asciigen_cli (distinct from the asciigen library target), but the
# binary you actually run stays named asciigen(.exe).
set_target_properties(asciigen_cli PROPERTIES OUTPUT_NAME asciigen)

if(MSVC)
    target_compile_options(asciigen_cli PRIVATE /W4 /permissive-)
else()
    target_compile_options(asciigen_cli PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
endif()
