cmake_minimum_required(VERSION 3.22)
project(SINEditor VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD            11)
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

message(STATUS "=== SIN Editor ===")
message(STATUS "System   : ${CMAKE_SYSTEM_NAME}")
message(STATUS "Compiler : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Build    : ${CMAKE_BUILD_TYPE}")

# ---- Raylib settings (must be set BEFORE FetchContent) ----------------------
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING  OFF CACHE BOOL "" FORCE)

if(ANDROID)
    # ANDROID variable is set automatically by the NDK toolchain file
    set(PLATFORM  "Android"                CACHE STRING "" FORCE)
    set(GRAPHICS  "GRAPHICS_API_OPENGL_ES2" CACHE STRING "" FORCE)
    set(USE_AUDIO OFF                       CACHE BOOL   "" FORCE)
    message(STATUS "Android ABI: ${ANDROID_ABI}")
    message(STATUS "Android API: ${ANDROID_PLATFORM}")
else()
    set(PLATFORM  "Desktop"                CACHE STRING "" FORCE)
endif()

# ---- Fetch Raylib 5.0 -------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.0
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(raylib)

# ---- Source files -----------------------------------------------------------
set(SIN_SOURCES
    src/main.cpp
    src/piece_table.cpp
)

# ---- Android: shared library loaded by NativeActivity ----------------------
if(ANDROID)
    add_library(sin_editor SHARED ${SIN_SOURCES})

    target_compile_definitions(sin_editor PRIVATE
        PLATFORM_ANDROID
        GRAPHICS_API_OPENGL_ES2
    )
    target_include_directories(sin_editor PRIVATE src)

    find_library(LIB_LOG     log)
    find_library(LIB_ANDROID android)
    find_library(LIB_EGL     EGL)
    find_library(LIB_GLES2   GLESv2)
    find_library(LIB_OPENSL  OpenSLES)

    target_link_libraries(sin_editor PRIVATE
        raylib
        ${LIB_LOG}
        ${LIB_ANDROID}
        ${LIB_EGL}
        ${LIB_GLES2}
        ${LIB_OPENSL}
    )

# ---- Desktop: executable ----------------------------------------------------
else()
    add_executable(sin_editor ${SIN_SOURCES})

    target_compile_options(sin_editor PRIVATE
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
        $<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<CONFIG:Release>>:-O3>
    )
    target_include_directories(sin_editor PRIVATE src)
    target_link_libraries(sin_editor PRIVATE raylib)

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(sin_editor PRIVATE GL m pthread dl rt X11)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        target_link_libraries(sin_editor PRIVATE
            "-framework OpenGL" "-framework Cocoa" "-framework IOKit" "-framework CoreFoundation")
    elseif(WIN32)
        target_link_libraries(sin_editor PRIVATE opengl32 gdi32 winmm)
    endif()

    install(TARGETS sin_editor RUNTIME DESTINATION bin)
endif()
