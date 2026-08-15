# Third-party libraries, vendored directly (no submodules, no package
# manager). Each one gets its own subdirectory and its own CMakeLists.txt, so
# adding the next dependency means one more add_subdirectory() line here.

add_subdirectory(stb)
