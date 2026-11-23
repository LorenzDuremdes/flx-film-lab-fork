# Decode the integer camera samples in PureRAW's High-Fidelity DNG tiles.
# Build only libjxl's decoder and its pinned dependencies, statically.
include(FetchContent)
FetchContent_Declare(flx_jxl
    GIT_REPOSITORY https://github.com/libjxl/libjxl.git
    GIT_TAG 332feb17d17311c748445f7ee75c4fb55cc38530 # v0.11.2
    GIT_SUBMODULES third_party/brotli third_party/highway third_party/lcms
    GIT_SUBMODULES_RECURSE FALSE
    SOURCE_SUBDIR flx-download-only)
FetchContent_MakeAvailable(flx_jxl)

function(flx_add_jxl)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_TESTING OFF)
    set(CMAKE_AUTOMOC OFF)
    foreach(feature TOOLS DEVTOOLS JPEGLI DOXYGEN MANPAGES BENCHMARK EXAMPLES JNI SJPEG OPENEXR SKCMS)
        set(JPEGXL_ENABLE_${feature} OFF CACHE BOOL "" FORCE)
    endforeach()
    add_subdirectory("${flx_jxl_SOURCE_DIR}" "${flx_jxl_BINARY_DIR}" EXCLUDE_FROM_ALL)
endfunction()
flx_add_jxl()
