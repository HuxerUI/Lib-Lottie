if (TARGET lottie_thorvg)
    return()
endif ()
if (POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif ()
include(FetchContent)

# The pinned CPU/Lottie subset builds with the consuming project's compiler and toolchain.
FetchContent_Declare(lottie_tvg
    URL "https://codeload.github.com/thorvg/thorvg/tar.gz/589bc4120f2097fb1b135e552c2ef39f6581612b"
    URL_HASH SHA256=2380c7f1c00ee189b0f28a78a00112c3b290305e6a321641dd527977e9ef6fa3
)
FetchContent_MakeAvailable(lottie_tvg)

set(LOTTIE_THORVG_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/thorvg-config")
file(MAKE_DIRECTORY "${LOTTIE_THORVG_CONFIG_DIR}")
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
    #if !defined(__aarch64__) && !defined(_M_ARM64)
    #error ARM64 baseline required
    #endif
    #include <arm_neon.h>
    int main() { return vgetq_lane_u32(vdupq_n_u32(0), 0); }
" LOTTIE_THORVG_NEON)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../thorvg_config.h.in" "${LOTTIE_THORVG_CONFIG_DIR}/config.h")

set(LOTTIE_THORVG_DIRECTORIES
    common
    renderer
    renderer/cpu_engine
    loaders/raw
    loaders/lottie
)
set(LOTTIE_THORVG_SOURCES)
set(LOTTIE_THORVG_INCLUDES)
foreach (directory IN LISTS LOTTIE_THORVG_DIRECTORIES)
    set(directory "${lottie_tvg_SOURCE_DIR}/src/${directory}")
    file(GLOB sources CONFIGURE_DEPENDS "${directory}/*.cpp")
    list(APPEND LOTTIE_THORVG_SOURCES ${sources})
    list(APPEND LOTTIE_THORVG_INCLUDES "${directory}")
endforeach ()
if (NOT LOTTIE_THORVG_NEON)
    list(FILTER LOTTIE_THORVG_SOURCES EXCLUDE REGEX "/tvgSwRasterSimd\\.cpp$")
endif ()

# Embed the renderer objects in Lib-Lottie so packaged static libraries include the backend.
add_library(lottie_thorvg OBJECT ${LOTTIE_THORVG_SOURCES})
target_compile_features(lottie_thorvg PRIVATE cxx_std_14)
target_compile_definitions(lottie_thorvg PUBLIC TVG_STATIC)
option(LOTTIE_THORVG_OPTIMIZE "Optimize the ThorVG renderer in every build configuration" ON)
if (LOTTIE_THORVG_OPTIMIZE)
    if (MSVC)
        # This directory owns only ThorVG; application debug flags and runtime libraries remain intact.
        foreach (flags IN ITEMS CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG)
            string(REGEX REPLACE "(^|[ \t])[-/](RTC[^ \t]*|Od|Ob[0-3])" " " ${flags} "${${flags}}")
        endforeach ()
        set_property(TARGET lottie_thorvg PROPERTY MSVC_RUNTIME_CHECKS "")
        target_compile_options(lottie_thorvg PRIVATE /O2 /Ob2)
    else ()
        target_compile_options(lottie_thorvg PRIVATE -O3)
    endif ()
endif ()
if (WIN32)
    target_compile_definitions(lottie_thorvg PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif ()
target_include_directories(lottie_thorvg
    PUBLIC "${lottie_tvg_SOURCE_DIR}/inc"
    PRIVATE "${LOTTIE_THORVG_CONFIG_DIR}" ${LOTTIE_THORVG_INCLUDES}
)
set_target_properties(lottie_thorvg PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)
