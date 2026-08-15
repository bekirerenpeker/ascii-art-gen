# stb (https://github.com/nothings/stb) -- vendored as individual headers
# rather than the whole upstream repo, which bundles ~20 unrelated libraries
# plus test assets we don't want.
#
# The include root is lib/ (this folder's parent), so consumers write
# #include "stb/stb_image.h", never a bare #include "stb_image.h".

add_library(stb STATIC stb_impl.cpp)
target_include_directories(stb PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

# stb's headers are warning-noisy under -Wall/-Wextra and /W4; that's stb's
# problem, not its callers', so it's suppressed only for this target.
if(MSVC)
    target_compile_options(stb PRIVATE /w)
else()
    target_compile_options(stb PRIVATE -w)
endif()
