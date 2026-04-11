cmake_minimum_required(VERSION 3.11)
project(SIN_Editor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── تجلب مكتبة Raylib تلقائياً إذا مو موجودة ──────────────────────────────
include(FetchContent)
FetchContent_Declare(
    raylib
    URL https://github.com/raysan5/raylib/archive/refs/tags/5.0.tar.gz
)
FetchContent_MakeAvailable(raylib)

# ── تحديد ملفات الكود بداخل مجلد src ──────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/piece_table.cpp
)

# ── بناء البرنامج ────────────────────────────────────────────────────────
add_executable(sin_editor ${SOURCES})

# ── ربط المكتبات ─────────────────────────────────────────────────────────
target_include_directories(sin_editor PRIVATE src)
target_link_libraries(sin_editor raylib)

# ── إعدادات خاصة للأندرويد (تلقائي) ───────────────────────────────────────
if(PLATFORM STREQUAL "Android")
    add_definitions(-DPLATFORM_ANDROID)
    # هنا يتم إضافة إعدادات الـ APK للأندرويد
endif()

# ── إعدادات للويندوز لضمان عدم ظهور شاشة الكونسول السوداء ───────────────
if(WIN32)
    set_target_properties(sin_editor PROPERTIES WIN32_GRAPHIC_APP ON)
endif()
