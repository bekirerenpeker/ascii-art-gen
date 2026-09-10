# Third-party libraries. stb is vendored directly (single headers, no build
# system of its own -- lib/stb/CMakeLists.txt is ours). FreeType is a git
# submodule instead: it's a real source tree that ships its own
# CMakeLists.txt, so add_subdirectory(freetype) below runs FreeType's own
# build script, not one we authored.

add_subdirectory(stb)

# Must be set before add_subdirectory() pulls in FreeType's CMakeLists.txt.
# Trims it to "just rasterize TTF/OTF": turns off support this project never
# uses (compressed/embedded-bitmap fonts, text shaping) to keep the build
# smaller and free of transitive dependencies of its own.
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)

add_subdirectory(freetype)

add_subdirectory(ffmpeg)
