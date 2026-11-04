cmake_minimum_required(VERSION 3.20)

# CMake initializes Apple deployment defaults during the first project() call.
# Explicit settings and toolchains retain ownership of their target platform.
if (CMAKE_HOST_APPLE AND (NOT CMAKE_SYSTEM_NAME OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        AND NOT CMAKE_TOOLCHAIN_FILE AND "$ENV{CMAKE_TOOLCHAIN_FILE}" STREQUAL ""
        AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET AND "$ENV{MACOSX_DEPLOYMENT_TARGET}" STREQUAL "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS deployment target")
endif ()

project(lib_lottie VERSION 0.1.0 LANGUAGES NONE)

include("${CMAKE_CURRENT_SOURCE_DIR}/HuxerUIProject.cmake")
if (HUXERUI_PROJECT_PLAN_ONLY)
    return()
endif ()

file(GLOB_RECURSE LIBRARY_SOURCE_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
)
if (EMSCRIPTEN)
    list(APPEND LIBRARY_SOURCE_FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/platform/web/work_queue.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/platform/web/frame_presenter.cpp"
    )
else ()
    list(APPEND LIBRARY_SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/platform/native/work_queue.cpp")
    if (APPLE)
        list(APPEND LIBRARY_SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/platform/apple/frame_presenter.cpp")
    elseif (ANDROID)
        list(APPEND LIBRARY_SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/platform/android/src/main/cpp/frame_presenter.cpp")
    elseif (WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND LIBRARY_SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/platform/desktop/frame_presenter.cpp")
    else ()
        message(FATAL_ERROR "Lib-Lottie has no frame presenter for ${CMAKE_SYSTEM_NAME}")
    endif ()
endif ()

huxerui_add_library(huxerui_lottie
        SOURCES
            ${LIBRARY_SOURCE_FILES}
        RESOURCES
            resources
        RESOURCE_NAMESPACE
            huxerui_lottie
)

add_library(HuxerUI::Lottie ALIAS huxerui_lottie)

if (HUXERUI_LIBRARY_GRAPH_ONLY)
    return()
endif ()

target_include_directories(huxerui_lottie PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)

add_subdirectory(cmake/thorvg)
target_include_directories(huxerui_lottie PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(huxerui_lottie PRIVATE lottie_thorvg)
if (APPLE)
    target_link_libraries(huxerui_lottie PRIVATE "-framework CoreVideo")
endif ()
if (ANDROID)
    target_link_libraries(huxerui_lottie PRIVATE jnigraphics)
endif ()
if (NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
    target_link_libraries(huxerui_lottie PRIVATE Threads::Threads)
endif ()

option(LOTTIE_BUILD_TESTS "Build Lib-Lottie tests" OFF)
if (LOTTIE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()
