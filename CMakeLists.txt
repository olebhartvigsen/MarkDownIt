cmake_minimum_required(VERSION 3.20)
project(MarkDownIt LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static runtime so the exe has zero redistributable deps.
set(CMAKE_MSVC_RUNTIME_LIBRARY
    "$<$<CONFIG:Debug>:MultiThreadedDebug>$<$<CONFIG:Release>:MultiThreaded>$<$<CONFIG:RelWithDebInfo>:MultiThreaded>$<$<CONFIG:MinSizeRel>:MultiThreaded>"
)

# md4c: vendored markdown parser (MIT, release-0.5.3, tag commit 472c417).
add_library(md4c STATIC third_party/md4c/md4c.c)
target_include_directories(md4c PUBLIC third_party/md4c)
if(MSVC)
    target_compile_options(md4c PRIVATE /W1)
endif()

# Windows Ribbon Framework: compile ribbon.xml with UICC.exe
if(MSVC)
    if(NOT UICC_EXE)
        file(GLOB UICC_CANDIDATES
            "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/uicc.exe"
            "C:/Program Files/Windows Kits/10/bin/*/x64/uicc.exe"
            "C:/Program Files (x86)/Windows Kits/10/bin/*/x86/uicc.exe"
        )
        if(UICC_CANDIDATES)
            list(SORT UICC_CANDIDATES COMPARE NATURAL ORDER DESCENDING)
            list(GET UICC_CANDIDATES 0 UICC_EXE)
        endif()
    endif()

    if(NOT UICC_EXE)
        message(WARNING "UICC.exe not found. Ribbon will not be built.")
        set(RIBBON_AVAILABLE FALSE)
    else()
        message(STATUS "Using UICC: ${UICC_EXE}")
        set(RIBBON_AVAILABLE TRUE)
    endif()

    set(RIBBON_XML ${CMAKE_CURRENT_SOURCE_DIR}/src/ribbon.xml)
    set(RIBBON_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/ribbon)
    set(RIBBON_HEADER ${RIBBON_OUT_DIR}/ribbonUI.h)
    set(RIBBON_RC2 ${RIBBON_OUT_DIR}/ribbonUI.rc2)
    set(RIBBON_BML ${RIBBON_OUT_DIR}/ribbonUI.bml)

    if(RIBBON_AVAILABLE)
        # Step 1: Compile ribbon.xml -> ribbonUI.h + ribbonUI.rc2 + ribbonUI.bml
        add_custom_command(
            OUTPUT ${RIBBON_HEADER} ${RIBBON_RC2} ${RIBBON_BML}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${RIBBON_OUT_DIR}
            COMMAND ${UICC_EXE} ${RIBBON_XML} ${RIBBON_BML}
                /header:${RIBBON_HEADER}
                /res:${RIBBON_RC2}
                /name:MARKDOWNIT
            DEPENDS ${RIBBON_XML}
            COMMENT "Compiling Ribbon markup (UICC)"
            VERBATIM
        )

        # Step 2: Compile the RC file to a .res using rc.exe.
        # Find rc.exe in the Windows SDK.
        file(GLOB RC_CANDIDATES
            "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/rc.exe"
            "C:/Program Files (x86)/Windows Kits/10/bin/*/x86/rc.exe"
        )
        list(SORT RC_CANDIDATES COMPARE NATURAL ORDER DESCENDING)
        if(RC_CANDIDATES)
            list(GET RC_CANDIDATES 0 RC_EXE)
        endif()
        # The app.rc includes ribbonUI.rc2 which references ribbonUI.bml.
        # Both are in RIBBON_OUT_DIR.
        set(RIBBON_RC ${CMAKE_CURRENT_SOURCE_DIR}/src/app.rc)
        set(RIBBON_RES ${RIBBON_OUT_DIR}/app.res)
        add_custom_command(
            OUTPUT ${RIBBON_RES}
            COMMAND ${RC_EXE} /nologo /r /I${RIBBON_OUT_DIR} /I${CMAKE_CURRENT_SOURCE_DIR}/src /fo${RIBBON_RES} ${RIBBON_RC}
            DEPENDS ${RIBBON_RC} ${RIBBON_RC2} ${RIBBON_BML} ${CMAKE_CURRENT_SOURCE_DIR}/src/app.manifest
            COMMENT "Compiling Ribbon RC resource"
            VERBATIM
        )
        add_custom_target(ribbon_markup ALL DEPENDS ${RIBBON_RES})
    endif()
endif()

# Main application
add_executable(MarkDownIt WIN32
    src/main.cpp
    src/app.cpp
    src/renderer.cpp
    src/renderer_pie.cpp
    src/colortext.cpp
    src/imagehelper.cpp
    src/filewatch.cpp
    src/parser.cpp
    src/mermaid/layout_acyclic.cpp
    src/mermaid/layout_edges.cpp
    src/mermaid/layout_normalize.cpp
    src/mermaid/layout_order.cpp
    src/mermaid/layout_position.cpp
    src/mermaid/layout_rank.cpp
    src/mermaid/layout.cpp
    src/mermaid/layout_measure.cpp
    src/mermaid/measure_dwrite.cpp
    src/mermaid/parse.cpp
    src/mermaid/pie_parse.cpp
    src/mermaid/pie_layout.cpp
    src/mermaid/seq_parse.cpp
    src/mermaid/seq_layout.cpp
    src/mermaid/swimlanes.cpp
    src/ribbon.cpp
    src/textbuffer.cpp
    src/layoutcache.cpp
    src/editcontroller.cpp
    src/navigation.cpp
    src/undostack.cpp
    src/clipboard.cpp
    src/formatting.cpp
    src/autoformat.cpp
    src/inputfilter.cpp
    src/fileassoc.cpp
    src/settings.cpp
    src/welcomescreen.cpp
    src/svgtext.cpp
    src/svgdoc.cpp
)

if(MSVC AND RIBBON_AVAILABLE)
    target_include_directories(MarkDownIt PRIVATE ${RIBBON_OUT_DIR})
    add_dependencies(MarkDownIt ribbon_markup)

    # Pass the .res file directly to the linker via LINK_FLAGS.
    # CMake's VS generator doesn't handle .res in target_sources properly,
    # so we add it to LINK_FLAGS which passes it directly to link.exe.
    # Convert to Windows-style path for MSVC.
    file(TO_NATIVE_PATH "${RIBBON_RES}" RIBBON_RES_NATIVE)
    set_property(TARGET MarkDownIt APPEND PROPERTY LINK_FLAGS "${RIBBON_RES_NATIVE}")

    # uiribbon.lib and propsys.lib
    if(NOT UIRIBBON_LIB)
        file(GLOB SDK_LIB_DIRS
            "C:/Program Files (x86)/Windows Kits/10/Lib/*/um/x64"
            "C:/Program Files/Windows Kits/10/Lib/*/um/x64"
        )
        list(SORT SDK_LIB_DIRS COMPARE NATURAL ORDER DESCENDING)
        if(SDK_LIB_DIRS)
            list(GET SDK_LIB_DIRS 0 SDK_LIB_DIR)
            find_library(UIRIBBON_LIB NAMES uiribbon HINTS ${SDK_LIB_DIR})
        endif()
    endif()
    if(NOT PROPSYS_LIB)
        if(NOT SDK_LIB_DIR)
            file(GLOB SDK_LIB_DIRS
                "C:/Program Files (x86)/Windows Kits/10/Lib/*/um/x64"
                "C:/Program Files/Windows Kits/10/Lib/*/um/x64"
            )
            list(SORT SDK_LIB_DIRS COMPARE NATURAL ORDER DESCENDING)
            if(SDK_LIB_DIRS)
                list(GET SDK_LIB_DIRS 0 SDK_LIB_DIR)
            endif()
        endif()
        if(SDK_LIB_DIR)
            find_library(PROPSYS_LIB NAMES propsys HINTS ${SDK_LIB_DIR})
        endif()
    endif()
    if(UIRIBBON_LIB)
        set(UIRIBBON_LINK_LIB ${UIRIBBON_LIB})
    endif()
    if(PROPSYS_LIB)
        set(PROPSYS_LINK_LIB ${PROPSYS_LIB})
    endif()
    message(STATUS "Using uiribbon.lib: ${UIRIBBON_LIB}")
    message(STATUS "Using propsys.lib: ${PROPSYS_LIB}")
endif() # MSVC AND RIBBON_AVAILABLE

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
    ${UIRIBBON_LINK_LIB}
    ${PROPSYS_LINK_LIB}
)

target_compile_definitions(MarkDownIt PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# The linker auto-generates a manifest from the
# #pragma comment(linker, "/manifestdependency:...") in main.cpp,
# which enables ComCtl32 v6 (visual styles) for the ribbon framework.

# Tests
option(BUILD_TESTS "Build test binary" OFF)
if(BUILD_TESTS)
    add_executable(MarkDownIt.tests
        tests/parser_smoke.cpp
        tests/textbuffer_test.cpp
        tests/caret_test.cpp
        tests/hittest_test.cpp
        tests/editcontroller_test.cpp
        tests/navigation_test.cpp
        tests/undo_test.cpp
        tests/incremental_test.cpp
        tests/clipboard_test.cpp
        tests/formatting_test.cpp
        tests/autoformat_test.cpp
        tests/inputfilter_test.cpp
        tests/mermaid_model_test.cpp
        tests/mermaid_parse_test.cpp
        tests/mermaid_layout_test.cpp
        tests/mermaid_golden_test.cpp
        tests/mermaid_pie_golden_test.cpp
        tests/mermaid_seq_golden_test.cpp
        tests/mermaid_swimlanes_test.cpp
        tests/mermaid_measure_test.cpp
        tests/dom_mermaid_test.cpp
        tests/mermaid_cache_test.cpp
        tests/perf_harness.cpp
        tests/test_main.cpp
        tests/svgtext_test.cpp
        src/parser.cpp
        src/textbuffer.cpp
        src/layoutcache.cpp
        src/editcontroller.cpp
        src/navigation.cpp
        src/undostack.cpp
        src/clipboard.cpp
        src/formatting.cpp
        src/autoformat.cpp
        src/inputfilter.cpp
        src/mermaid/layout_acyclic.cpp
        src/mermaid/layout_edges.cpp
        src/mermaid/layout_normalize.cpp
        src/mermaid/layout_order.cpp
        src/mermaid/layout_position.cpp
        src/mermaid/layout_rank.cpp
        src/mermaid/layout.cpp
        src/mermaid/layout_measure.cpp
        src/mermaid/parse.cpp
        src/mermaid/pie_parse.cpp
        src/mermaid/pie_layout.cpp
        src/mermaid/seq_parse.cpp
        src/mermaid/seq_layout.cpp
        src/mermaid/swimlanes.cpp
        src/svgtext.cpp
    )
    target_link_libraries(MarkDownIt.tests PRIVATE md4c)
    target_include_directories(MarkDownIt.tests PRIVATE
        src
        tests
        tests/mermaid
        third_party/md4c
    )
    target_compile_definitions(MarkDownIt.tests PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    set_target_properties(MarkDownIt.tests PROPERTIES WIN32_EXECUTABLE OFF)
endif()

