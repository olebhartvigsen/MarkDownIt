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
# Suppress md4c warnings under MSVC /W4 (it is C99 clean, but /W4 is noisy).
if(MSVC)
    target_compile_options(md4c PRIVATE /W1)
endif()

# Main application
add_executable(MarkDownIt WIN32
    src/main.cpp
    src/app.cpp
    src/renderer.cpp
    src/parser.cpp
)

target_link_libraries(MarkDownIt PRIVATE
    md4c
    d2d1
    dwrite
    user32
    gdi32
    ole32
    shell32
    shlwapi
    windowscodecs
)

target_compile_definitions(MarkDownIt PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Tests: standalone console exe, no Win32 entry point.
option(BUILD_TESTS "Build test binary" OFF)
if(BUILD_TESTS)
    add_executable(MarkDownIt.tests
        tests/parser_smoke.cpp
        src/parser.cpp
    )
    target_link_libraries(MarkDownIt.tests PRIVATE md4c)
    target_include_directories(MarkDownIt.tests PRIVATE
        src
        tests
        third_party/md4c
    )
    target_compile_definitions(MarkDownIt.tests PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    # Tests are a console app, not a GUI app (needs main(), not wWinMain).
    set_target_properties(MarkDownIt.tests PROPERTIES WIN32_EXECUTABLE OFF)
endif()
