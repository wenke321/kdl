cmake_minimum_required(VERSION 3.16)

project(KevinDownloader
    VERSION 1.0.0
    DESCRIPTION "A production-grade multi-threaded HTTP/HTTPS downloader for Windows"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Platform guard
# ---------------------------------------------------------------------------
if(NOT WIN32)
    message(FATAL_ERROR "KevinDownloader only supports Windows (WinHTTP backend).")
endif()

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ---------------------------------------------------------------------------
# Core static library (all reusable modules)
# ---------------------------------------------------------------------------
add_library(kdl STATIC
    src/core/Error.cpp
    src/core/Logger.cpp
    src/core/StringUtil.cpp
    src/core/ByteUtil.cpp
    src/core/Hash.cpp
    src/url/Url.cpp
    src/net/HttpClient.cpp
    src/io/FileWriter.cpp
    src/io/ResumeStore.cpp
    src/thread/ThreadPool.cpp
    src/download/Downloader.cpp
    src/cli/Options.cpp
)

target_include_directories(kdl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_definitions(kdl PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    _CRT_SECURE_NO_WARNINGS
)

# WinHTTP + Win32 system libraries required by the network/file backends.
target_link_libraries(kdl PUBLIC winhttp ws2_32)
target_link_libraries(kdl PUBLIC winhttp ws2_32 bcrypt)

if(MSVC)
    target_compile_options(kdl PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus /MP)
    target_compile_definitions(kdl PRIVATE _WIN32_WINNT=0x0601) # Windows 7+
else()
    target_compile_options(kdl PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_definitions(kdl PRIVATE _WIN32_WINNT=0x0601)
endif()

# ---------------------------------------------------------------------------
# Command line front-end
# ---------------------------------------------------------------------------
add_executable(kdl_cli src/main.cpp)
set_target_properties(kdl_cli PROPERTIES OUTPUT_NAME kdl)
target_link_libraries(kdl_cli PRIVATE kdl)

if(MSVC)
    target_compile_options(kdl_cli PRIVATE /utf-8)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS kdl_cli RUNTIME DESTINATION bin)
