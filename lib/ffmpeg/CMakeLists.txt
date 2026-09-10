# Prebuilt FFmpeg (LGPL, shared), fetched per-platform at configure time -- not built
# from source, and not statically linked.
#
# Two things ruled out static linking specifically, not just "building it ourselves":
#
# 1. FFmpeg's own build system is a shell configure+make, not CMake, and on Windows
#    needs MSYS2 + NASM just to run -- fetching an already-compiled archive sidesteps
#    that regardless of static vs shared.
# 2. The community builds this project pulls from (BtbN/FFmpeg-Builds -- the common
#    choice; see video-roadmap.md item 1) are cross-compiled with MinGW-w64 for Windows.
#    Their "static" archives only bundle statically-linked CLI executables (ffmpeg.exe
#    itself); no linkable .lib/.a and no headers ship in them at all -- there is nothing
#    to actually static-link against. Their "shared" archives are the real dev SDK
#    (headers + import libs + DLLs), which is what this project needs since it calls
#    libav*/libsw* directly rather than shelling out to ffmpeg.exe.
#
#    Separately: this project's Windows toolchain is clang simulating the MSVC ABI,
#    linked with lld-link, not MinGW. Even if a MinGW-built static archive existed, the
#    two are different C runtimes and not the target for lld-link to consume directly. A
#    DLL's import library doesn't have that problem -- the DLL boundary only has to agree
#    on calling convention, which is toolchain-independent -- so shared linking is both
#    the only option BtbN ships and the one that was always going to work cleanly here.
#
# Net effect: a handful of DLLs/.so files ship next to the built executable (copied by
# app/CMakeLists.txt's post-build step) instead of being baked into one binary. Still no
# separate install step, no system-wide FFmpeg, nothing on PATH required.
#
# Pinned to one dated, immutable release (not the floating "latest" tag) with a SHA-256
# hash per archive, so a stale/replaced upload fails the build loudly instead of silently
# linking something different than what was reviewed here.

include(FetchContent)

set(_ffmpeg_release "autobuild-2026-08-25-13-06")
set(_ffmpeg_base "https://github.com/BtbN/FFmpeg-Builds/releases/download/${_ffmpeg_release}")

if(WIN32)
    set(_ffmpeg_url "${_ffmpeg_base}/ffmpeg-n9.0.1-6-g9d4ca21220-win64-lgpl-shared-9.0.zip")
    set(_ffmpeg_hash "SHA256=beea327434de459b16f4a8d1ad3a56c13dd787962d010b029bf767c3afb33542")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_ffmpeg_url "${_ffmpeg_base}/ffmpeg-n9.0.1-6-g9d4ca21220-linux64-lgpl-shared-9.0.tar.xz")
    set(_ffmpeg_hash "SHA256=f9938c58f4acdcde4912e4af49672822ba92b33bcc8e27971ca3a4092cc3ae51")
else()
    message(FATAL_ERROR
        "No prebuilt FFmpeg pinned for this platform (${CMAKE_SYSTEM_NAME}) yet -- "
        "add an entry in lib/ffmpeg/CMakeLists.txt.")
endif()

FetchContent_Declare(ffmpeg_prebuilt
    URL ${_ffmpeg_url}
    URL_HASH ${_ffmpeg_hash}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_MakeAvailable(ffmpeg_prebuilt)

# Only the libraries the roadmap's scope actually needs: demux/mux (avformat),
# encode/decode (avcodec), core (avutil), pixel format/scaling (swscale). Not linked:
# avdevice (capture devices -- we only ever read/write files), avfilter (filtergraphs --
# no plan to build one), swresample (audio resampling -- no audio in scope at all).
add_library(ffmpeg INTERFACE)
target_include_directories(ffmpeg INTERFACE "${ffmpeg_prebuilt_SOURCE_DIR}/include")

if(WIN32)
    target_link_libraries(ffmpeg INTERFACE
        "${ffmpeg_prebuilt_SOURCE_DIR}/lib/avformat.lib"
        "${ffmpeg_prebuilt_SOURCE_DIR}/lib/avcodec.lib"
        "${ffmpeg_prebuilt_SOURCE_DIR}/lib/avutil.lib"
        "${ffmpeg_prebuilt_SOURCE_DIR}/lib/swscale.lib")

    file(GLOB _ffmpeg_runtime_files "${ffmpeg_prebuilt_SOURCE_DIR}/bin/*.dll")
else()
    target_link_libraries(ffmpeg INTERFACE
        "${ffmpeg_prebuilt_SOURCE_DIR}/lib/libavformat.so"
        "${ffmpeg_prebuilt_SOURCE_DIR}/lib/libavcodec.so"
        "${ffmpeg_prebuilt_SOURCE_DIR}/lib/libavutil.so"
        "${ffmpeg_prebuilt_SOURCE_DIR}/lib/libswscale.so")

    file(GLOB _ffmpeg_runtime_files "${ffmpeg_prebuilt_SOURCE_DIR}/lib/*.so*")
endif()

# Consumed by app/CMakeLists.txt's post-build step, which copies these next to
# whatever executable links `ffmpeg` -- a DLL/.so has to sit beside the .exe (Windows)
# or be found at runtime (Linux) regardless of how it was linked at build time.
set(FFMPEG_RUNTIME_FILES "${_ffmpeg_runtime_files}" CACHE INTERNAL
    "FFmpeg shared libraries to copy next to any executable linking the ffmpeg target")
