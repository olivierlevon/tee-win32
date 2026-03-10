# ==========================================================================
# tee for Windows - CMake build
#
# Usage (MSVC):
#   cmake -B build -G "Visual Studio 17 2022" -A x64
#   cmake --build build --config Release
#
# Usage (Clang/LLVM via clang-cl, from a VS Developer Command Prompt):
#   cmake -B build -G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#
# Or with Ninja + MSVC (from a VS Developer Command Prompt):
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#
# Requires:
#   - Windows SDK 10.0
#   - PCRE2 source in deps/pcre2 (see README.md)
# ==========================================================================
cmake_minimum_required(VERSION 3.20)

project(tee-win32
    VERSION 1.4.0
    LANGUAGES C
    DESCRIPTION "tee for Windows"
)

# --------------------------------------------------------------------------
# Windows 10 minimum requirement
# --------------------------------------------------------------------------
set(CMAKE_SYSTEM_VERSION "10.0" CACHE STRING "Minimum Windows version")

# --------------------------------------------------------------------------
# PCRE2 static library (8-bit only, minimal config)
# --------------------------------------------------------------------------
set(PCRE2_SRC_DIR "${CMAKE_SOURCE_DIR}/deps/pcre2")
if(NOT EXISTS "${PCRE2_SRC_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "PCRE2 source not found in ${PCRE2_SRC_DIR}/\n"
        "  git clone https://github.com/PCRE2Project/pcre2.git deps/pcre2"
    )
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2_8 ON CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2_16 OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2_32 OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2GREP OFF CACHE BOOL "" FORCE)
set(PCRE2_SUPPORT_JIT OFF CACHE BOOL "" FORCE)
set(PCRE2_SUPPORT_UNICODE OFF CACHE BOOL "" FORCE)
set(PCRE2_NEWLINE "ANYCRLF" CACHE STRING "" FORCE)
set(PCRE2_SUPPORT_BSR_ANYCRLF ON CACHE BOOL "" FORCE)

add_subdirectory("${PCRE2_SRC_DIR}" "${CMAKE_BINARY_DIR}/pcre2" EXCLUDE_FROM_ALL)

# --------------------------------------------------------------------------
# tee executable
# --------------------------------------------------------------------------
add_executable(tee
    tee.c
    ansiconv.c
    etc/resources/tee.rc
)

target_include_directories(tee PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_BINARY_DIR}/pcre2"          # generated pcre2.h
    "${PCRE2_SRC_DIR}/src"               # pcre2_internal.h etc.
)

target_compile_definitions(tee PRIVATE
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
    WIN32_LEAN_AND_MEAN=1
    UNICODE
    _UNICODE
    PCRE2_STATIC
    PCRE2_CODE_UNIT_WIDTH=8
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

target_link_libraries(tee PRIVATE
    pcre2-8-static
    kernel32
    Shell32
    user32
)

# --------------------------------------------------------------------------
# Compiler-specific options (MSVC and clang-cl)
# --------------------------------------------------------------------------
if(MSVC)
    target_compile_options(tee PRIVATE
        /W4
        /utf-8
        $<$<CONFIG:Release>:/O2 /Oi /GL /GS- /Oy /Zi>
        $<$<CONFIG:Debug>:/W3 /Od /Zi>
    )
    target_link_options(tee PRIVATE
        /SUBSYSTEM:CONSOLE,10.0
        /MANIFEST:EMBED
        /DEBUG
        "/MANIFESTINPUT:${CMAKE_SOURCE_DIR}/etc/resources/compatibility.manifest"
        $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
    )
    # Static CRT for Release, dynamic for Debug
    set_property(TARGET tee PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
elseif(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    # Clang with GNU-like driver (e.g. clang --target=...-windows-msvc)
    target_compile_options(tee PRIVATE
        -Wall -Wextra
        $<$<CONFIG:Release>:-O2 -g>
        $<$<CONFIG:Debug>:-O0 -g>
    )
    target_link_options(tee PRIVATE
        /SUBSYSTEM:CONSOLE,10.0
        /MANIFEST:EMBED
        /DEBUG
        "/MANIFESTINPUT:${CMAKE_SOURCE_DIR}/etc/resources/compatibility.manifest"
        $<$<CONFIG:Release>:/OPT:REF /OPT:ICF>
    )
endif()
