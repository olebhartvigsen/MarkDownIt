cmake_minimum_required(VERSION 3.20)
project(MarkDownIt LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static runtime so the exe has zero redistributable deps.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(MarkDownIt WIN32
    src/main.cpp
)

target_link_libraries(MarkDownIt PRIVATE
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
