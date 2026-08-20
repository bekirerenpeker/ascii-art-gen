# asciigen -- the library. Pipeline stages (image_loading, filters, resample,
# dithering, edges, bitmap, color, output) live here as a static library. No
# dependency on app/; nothing here may include CLI code.
#
# Headers live directly under include/ (no extra "asciigen/" nesting -- this
# folder already is that namespace), e.g. include/core/image.hpp is
# #include "core/image.hpp" from both src/ and app/.

file(GLOB_RECURSE ASCIIGEN_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(asciigen STATIC ${ASCIIGEN_SOURCES})
target_include_directories(asciigen PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(asciigen PUBLIC cxx_std_20)
# PUBLIC, not PRIVATE: Image.hpp is a public header and includes stb/stb_image.h
# for stbi_image_free, so stb's include path has to propagate to consumers too.
target_link_libraries(asciigen PUBLIC stb)
# Same reasoning: Font.hpp is expected to store FT_Face/FT_Library members
# directly (matching how it already stores stbtt_fontinfo), so it needs
# FreeType's headers visible too. Linking the `freetype` target also pulls in
# its include directories automatically -- no separate include path to add.
target_link_libraries(asciigen PUBLIC freetype)

# Compiles every ASCIIGEN_PROFILE scope out when OFF, so a build without it
# carries no timing code at all. PUBLIC because app/ uses the same macros and
# both sides must agree, or a scope opened in one would not close in the other.
option(ASCIIGEN_PROFILING "Compile in the profiler" ON)
if(ASCIIGEN_PROFILING)
    target_compile_definitions(asciigen PUBLIC ASCIIGEN_PROFILING)
endif()

if(MSVC)
    target_compile_options(asciigen PRIVATE /W4 /permissive-)
else()
    target_compile_options(asciigen PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
endif()

# STATIC with zero sources currently in src/ still needs one translation unit
# to produce a valid archive; drop this once the first real .cpp lands there.
if(NOT ASCIIGEN_SOURCES)
    target_sources(asciigen PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/_empty.cpp")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/_empty.cpp" "")
endif()
