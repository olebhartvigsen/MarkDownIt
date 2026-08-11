cmake_minimum_required(VERSION 3.20)
project(MarkDownIt LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static runtime so the exe has zero redistributable deps.
# MultiThreaded for Release, MultiThreadedDebug for Debug.
set(CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<STREQUAL:${CMAKE_BUILD_TYPE},Debug>"
)

# md4c: vendored markdown parser (MIT, release-0.5.3, tag commit 472c417).
# Core parser only (md4c.c + md4c.h), HTML renderer (entity.c) not needed.
add_library(md4c STATIC third_party/md4c/md4c.c)
target_include_directories(md4c PUBLIC third_party/md4c)
# Suppress md4c warnings under MSVC /W4 (it is C99 clean, but /W4 is noisy).
if(MSVC)
    target_compile_options(md4c PRIVATE /W1)
endif()

# ── Windows Ribbon Framework: compile ribbon.xml with UICC.exe ──────────
# UICC.exe is the UI Command Compiler from the Windows SDK.
# It generates: ribbonUI.h (command IDs), ribbonUI.rc2 (resource include),
# and ribbonUI.bml (compiled binary markup).
if(MSVC)
    # Find UICC.exe in the Windows SDK.
    find_program(UICC_EXE NAMES uicc
        HINTS "$ENV{WindowsSdkVerBinPath}" "$ENV{WindowsSdkBinPath}"
        PATHS "C:/Program Files (x86)/Windows Kits/10/bin"
        PATH_SUFFIXES "x64" "x86"
        REQUIRED
    )

    set(RIBBON_XML ${CMAKE_CURRENT_SOURCE_DIR}/src/ribbon.xml)
    set(RIBBON_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/ribbon)
    set(RIBBON_HEADER ${RIBBON_OUT_DIR}/ribbonUI.h)
    set(RIBBON_RC2 ${RIBBON_OUT_DIR}/ribbonUI.rc2)
    set(RIBBON_BML ${RIBBON_OUT_DIR}/ribbonUI.bml)

    add_custom_command(
        OUTPUT ${RIBBON_HEADER} ${RIBBON_RC2} ${RIBBON_BML}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RIBBON_OUT_DIR}
        COMMAND ${UICC_EXE} ${RIBBON_XML} ${RIBBON_BML}
            /header:${RIBBON_HEADER}
            /res:${RIBBON_RC2}
            /name:MARKDOWNIT_RIBBON
        DEPENDS ${RIBBON_XML}
        COMMENT "Compiling Ribbon markup (UICC)"
        VERBATIM
    )

    # Generate the .rc file from the template, substituting the rc2 path.
    set(RIBBON_RC ${CMAKE_CURRENT_BINARY_DIR}/ribbon/app.rc)
    add_custom_command(
        OUTPUT ${RIBBON_RC}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/ribbon
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/src/app.rc ${RIBBON_RC}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/app.rc
        COMMENT "Preparing ribbon RC file"
        VERBATIM
    )

    # Custom target so the RC generation runs before the main build.
    add_custom_target(ribbon_markup DEPENDS ${RIBBON_HEADER} ${RIBBON_RC2} ${RIBBON_BML} ${RIBBON_RC})
endif()

# Main application
add_executable(MarkDownIt WIN32
    src/main.cpp
    src/app.cpp
    src/renderer.cpp
    src/colortext.cpp
    src/imagehelper.cpp
    src/filewatch.cpp
    src/parser.cpp
    src/ribbon.cpp
)

if(MSVC)
    # Add the ribbon resource file and generated header to the build.
    target_sources(MarkDownIt PRIVATE ${RIBBON_RC})
    target_include_directories(MarkDownIt PRIVATE ${RIBBON_OUT_DIR})
    add_dependencies(MarkDownIt ribbon_markup)
endif()

target_link_libraries(MarkDownIt PRIVATE
    md4c
    d2d1
    dwrite
    user32
    gdi32
    ole32
    oleaut32
    shell32
    shlwapi
    windowscodecs
    winhttp
    comdlg32
    uiribbon
    propsys
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
