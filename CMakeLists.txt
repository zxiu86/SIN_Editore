cmake_minimum_required(VERSION 3.22)
project(SINEditor VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD   11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ─────────────────────────────────────────────────────────────────────────────
#  Platform detection
# ─────────────────────────────────────────────────────────────────────────────
if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(PLATFORM_ANDROID TRUE)
else()
    set(PLATFORM_ANDROID FALSE)
endif()

# ─────────────────────────────────────────────────────────────────────────────
#  Build type
# ─────────────────────────────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

message(STATUS "=== SIN Editor ===")
message(STATUS "Platform  : ${CMAKE_SYSTEM_NAME}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler  : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

# ─────────────────────────────────────────────────────────────────────────────
#  Raylib via FetchContent
# ─────────────────────────────────────────────────────────────────────────────
include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

# Suppress Raylib's own example/game builds
set(BUILD_EXAMPLES  OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES     OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING   OFF CACHE BOOL "" FORCE)

if(PLATFORM_ANDROID)
    set(PLATFORM "Android" CACHE STRING "" FORCE)
    set(OPENGL_VERSION "ES 2.0" CACHE STRING "" FORCE)
    set(USE_AUDIO       OFF CACHE BOOL "" FORCE)
else()
    set(PLATFORM "Desktop" CACHE STRING "" FORCE)
endif()

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.0
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(raylib)

# ─────────────────────────────────────────────────────────────────────────────
#  Sources (shared between all platforms)
# ─────────────────────────────────────────────────────────────────────────────
set(SIN_SOURCES
    src/main.cpp
    src/piece_table.cpp
    
)

set(SIN_HEADERS
    src/piece_table.h
    src/highlighter.h
    src/editor.h
)

# ─────────────────────────────────────────────────────────────────────────────
#  Android — shared library loaded by the Java activity
# ──────────────────────────────────────────────────────────────────[...]
#  Android — shared library loaded by the Java activity
# ──────────────────────────────────────────────────────────────────[...]
if(PLATFORM_ANDROID)

    # Raylib provides a pre-made android_native_app_glue wrapper
    # We must link against the Raylib native_app_glue as well
    add_library(sin_editor SHARED ${SIN_SOURCES} ${SIN_HEADERS})

    target_compile_definitions(sin_editor PRIVATE
        PLATFORM_ANDROID
        GRAPHICS_API_OPENGL_ES2
    )

    target_include_directories(sin_editor PRIVATE src)

    # Android system libraries
    find_library(lib_log     log)
    find_library(lib_android android)
    find_library(lib_egl     EGL)
    find_library(lib_gles2   GLESv2)
    find_library(lib_opensl  OpenSLES)

    target_link_libraries(sin_editor PRIVATE
        raylib
        android_native_app_glue
        ${lib_log}
        ${lib_android}
        ${lib_egl}
        ${lib_gles2}
        ${lib_opensl}
    )

    # Package resources into assets/ — handled by Gradle; nothing extra here
    set_target_properties(sin_editor PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/libs/${ANDROID_ABI}"
    )

endif()



# ─────────────────────────────────────────────────────────────────────────────
#  Desktop (Linux / Windows / macOS)
# ─────────────────────────────────────────────────────────────────────────────
else()

    add_executable(sin_editor ${SIN_SOURCES} ${SIN_HEADERS})

    target_compile_options(sin_editor PRIVATE
        -Wall -Wextra
        $<$<CONFIG:Release>:-O3 -march=native>
        $<$<CONFIG:Debug>:-g>
    )

    target_include_directories(sin_editor PRIVATE src)

    target_link_libraries(sin_editor PRIVATE raylib)

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(sin_editor PRIVATE
            GL m pthread dl rt X11
        )
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        target_link_libraries(sin_editor PRIVATE
            "-framework OpenGL"
            "-framework Cocoa"
            "-framework IOKit"
            "-framework CoreFoundation"
        )
    endif()

    if(WIN32)
        target_link_libraries(sin_editor PRIVATE
            opengl32 gdi32 winmm
        )
        # On Windows suppress console window in Release
        if(CMAKE_BUILD_TYPE STREQUAL "Release")
            target_link_options(sin_editor PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
        endif()
    endif()

    install(TARGETS sin_editor RUNTIME DESTINATION bin)

endif()
