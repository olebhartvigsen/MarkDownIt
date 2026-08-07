cmake_minimum_required(VERSION 3.20)
project(MarkDownIt LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static runtime so the exe has zero redistributable deps.
# MultiThreaded for Release, MultiThreadedDebug for Debug.
set(CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

# md4c: vendored markdown parser (MIT, release-0.5.3, tag commit 472c417).
# Core parser only (md4c.c + md4c.h), HTML renderer (entity.c) not needed.
add_library(md4c STATIC third_party/md4c/md4c.c)
target_include_directories(md4c PUBLIC third_party/md4c)

add_executable(MarkDownIt WIN32
    src/main.cpp
)

target_link_libraries(MarkDownIt PRIVATE
    md4c
    d2d1
    dwrite
    user32
    gdi32
    ole32
    shlwapi
    windowscodecs
)

target_compile_definitions(MarkDownIt PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
