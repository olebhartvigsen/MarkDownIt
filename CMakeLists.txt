cmake_minimum_required(VERSION 3.20)
project(MarkDownIt LANGUAGES C CXX)

# The oldest macOS the app supports. It has to be set before the
# targets are created, because it decides the compiler flags.
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0")
endif()

# The architectures the macOS build produces. Apple Silicon is the
# default, because that is what the runner has. A universal binary is
# one setting away:
#
#     cmake -B build-mac -DMARKDOWNIT_MAC_ARCHS="arm64;x86_64"
#
set(MARKDOWNIT_MAC_ARCHS "arm64" CACHE STRING
    "The macOS architectures to build")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "${MARKDOWNIT_MAC_ARCHS}")
endif()

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


# miniz: vendored zlib-compatible compression (MIT, 3.0.2 amalgamation).
# The zlib-compatible names stay enabled so pdfio's zlib include can be
# routed to third_party/miniz/zlib.h.
add_library(miniz STATIC third_party/miniz/miniz.c)
target_include_directories(miniz PUBLIC third_party/miniz)
if(MSVC)
    target_compile_options(miniz PRIVATE /W1)
endif()

# pugixml: vendored XML parser for the OOXML parts of a .docx (MIT, 1.15).
add_library(pugixml STATIC third_party/pugixml/pugixml.cpp)
target_include_directories(pugixml PUBLIC third_party/pugixml)
if(MSVC)
    target_compile_options(pugixml PRIVATE /W1)
endif()

# pdfio: vendored PDF reader/writer (Apache-2.0, 1.6.5).
# third_party/miniz is listed first so pdfio's unconditional zlib.h include
# resolves to the vendored miniz shim instead of a system zlib.
add_library(pdfio STATIC
    third_party/pdfio/pdfio-aes.c
    third_party/pdfio/pdfio-array.c
    third_party/pdfio/pdfio-common.c
    third_party/pdfio/pdfio-content.c
    third_party/pdfio/pdfio-crypto.c
    third_party/pdfio/pdfio-dict.c
    third_party/pdfio/pdfio-file.c
    third_party/pdfio/pdfio-md5.c
    third_party/pdfio/pdfio-object.c
    third_party/pdfio/pdfio-page.c
    third_party/pdfio/pdfio-rc4.c
    third_party/pdfio/pdfio-sha256.c
    third_party/pdfio/pdfio-stream.c
    third_party/pdfio/pdfio-string.c
    third_party/pdfio/pdfio-token.c
    third_party/pdfio/pdfio-value.c
    third_party/pdfio/ttf.c
)
set_target_properties(pdfio PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_include_directories(pdfio
    PRIVATE third_party/miniz
    PUBLIC third_party/pdfio
)
target_link_libraries(pdfio PRIVATE miniz)
if(MSVC)
    target_compile_options(pdfio PRIVATE /W1)
    target_compile_definitions(pdfio PRIVATE _CRT_SECURE_NO_WARNINGS)
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

# Main application.
#
# The Win32 sources only build on Windows, and the macOS build has its
# own target, so the Windows target is not defined there.
if(NOT APPLE)
add_executable(MarkDownIt WIN32
    src/main.cpp
    src/app.cpp
    src/renderer.cpp
    src/renderer_pie.cpp
    src/renderer_sequence.cpp
    src/colortext.cpp
    src/imagehelper.cpp
    src/filewatch.cpp
    src/parser.cpp
    src/mermaid/class_parse.cpp
    src/mermaid/class_layout.cpp
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
    src/textlayout_dwrite.cpp
    src/editcontroller.cpp
    src/navigation.cpp
    src/tableedit.cpp
    src/undostack.cpp
    src/clipboard.cpp
    src/formatting.cpp
    src/autoformat.cpp
    src/inputfilter.cpp
    src/searchreplace.cpp
    src/findstate.cpp
    src/findbar_model.cpp
    src/findbar.cpp
    src/markers.cpp
    src/outline.cpp
    src/outlinepane.cpp
    src/textdrag.cpp
    src/fileassoc.cpp
    src/settings.cpp
    src/welcomescreen.cpp
    src/svgtext.cpp
    src/svgdoc.cpp
    src/office/docx_import.cpp
    src/office/docx_export.cpp
    src/office/pdf_export.cpp
    src/office/markdown_bridge.cpp
)

if(MSVC AND RIBBON_AVAILABLE)
    # The Windows SDK keeps per-arch import libs under Lib/*/um/<arch>.
    # Cross builds (-A ARM64) must link the arm64 flavor of uiribbon and
    # propsys, not the x64 one.
    if(CMAKE_GENERATOR_PLATFORM MATCHES "ARM64")
        set(SDK_UM_ARCH arm64)
    else()
        set(SDK_UM_ARCH x64)
    endif()

    target_include_directories(MarkDownIt PRIVATE
        src
        ${RIBBON_OUT_DIR}
    )
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
            "C:/Program Files (x86)/Windows Kits/10/Lib/*/um/${SDK_UM_ARCH}"
            "C:/Program Files/Windows Kits/10/Lib/*/um/${SDK_UM_ARCH}"
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
                "C:/Program Files (x86)/Windows Kits/10/Lib/*/um/${SDK_UM_ARCH}"
                "C:/Program Files/Windows Kits/10/Lib/*/um/${SDK_UM_ARCH}"
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
    miniz
    pugixml
    pdfio
    d2d1
    dwrite
    user32
    imm32
    gdi32
    ole32
    oleaut32
    oleacc
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

endif() # NOT APPLE

# The linker auto-generates a manifest from the
# #pragma comment(linker, "/manifestdependency:...") in main.cpp,
# which enables ComCtl32 v6 (visual styles) for the ribbon framework.

# Tests
option(BUILD_TESTS "Build test binary" OFF)
if(BUILD_TESTS AND APPLE)
    # The macOS test target. It compiles the same suite the Windows build
    # compiles, with two differences:
    #   * findbar_window_test.cpp is a real Win32 window test, so it stays
    #     on Windows. The navigation it covers is tested through the
    #     portable FindBarModel instead, in findstate_test.cpp.
    #   * the layout cache asks the platform-neutral text layout seam,
    #     which the macOS build answers with Core Text.
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
        tests/clear_format_test.cpp
        tests/autoformat_test.cpp
        tests/inputfilter_test.cpp
        tests/editor_features_test.cpp
        tests/findstate_test.cpp
        tests/findbar_ids_test.cpp
        tests/markers_test.cpp
        tests/outline_test.cpp
        tests/table_interaction_test.cpp
        tests/tableedit_test.cpp
        tests/mermaid_model_test.cpp
        tests/mermaid_parse_test.cpp
        tests/mermaid_class_golden_test.cpp
        tests/mermaid_state_golden_test.cpp
        tests/mermaid_layout_test.cpp
        tests/mermaid_golden_test.cpp
        tests/mermaid_pie_golden_test.cpp
        tests/mermaid_seq_golden_test.cpp
        tests/mermaid_swimlanes_test.cpp
        tests/mermaid_measure_test.cpp
        tests/dom_mermaid_test.cpp
        tests/mermaid_cache_test.cpp
        tests/theme_test.cpp
        tests/perf_harness.cpp
        tests/test_main.cpp
        tests/svgtext_test.cpp
        tests/zoommodel_test.cpp
        tests/office_interop_smoke_test.cpp
        tests/office_docx_import_test.cpp
        tests/office_docx_export_test.cpp
        tests/office_pdf_export_test.cpp
        tests/office_markdown_bridge_test.cpp
        src/parser.cpp
        src/textbuffer.cpp
        src/layoutcache.cpp
        src/textlayout_coretext.cpp
        src/editcontroller.cpp
        src/navigation.cpp
        src/tableedit.cpp
        src/undostack.cpp
        src/clipboard.cpp
        src/formatting.cpp
        src/autoformat.cpp
        src/inputfilter.cpp
        src/searchreplace.cpp
        src/findstate.cpp
        src/findbar_model.cpp
        src/markers.cpp
        src/outline.cpp
        src/textdrag.cpp
        src/svgtext.cpp
        src/mermaid/class_layout.cpp
        src/mermaid/class_parse.cpp
        src/mermaid/layout.cpp
        src/mermaid/layout_acyclic.cpp
        src/mermaid/layout_edges.cpp
        src/mermaid/layout_measure.cpp
        src/mermaid/layout_normalize.cpp
        src/mermaid/layout_order.cpp
        src/mermaid/layout_position.cpp
        src/mermaid/layout_rank.cpp
        src/mermaid/parse.cpp
        src/mermaid/pie_layout.cpp
        src/mermaid/pie_parse.cpp
        src/mermaid/seq_layout.cpp
        src/mermaid/seq_parse.cpp
        src/mermaid/state_layout.cpp
        src/mermaid/state_parse.cpp
        src/mermaid/swimlanes.cpp
        src/office/docx_import.cpp
        src/office/docx_export.cpp
        src/office/pdf_export.cpp
        src/office/markdown_bridge.cpp
    )
    target_include_directories(MarkDownIt.tests PRIVATE
        src
        tests
        tests/mermaid
        third_party/md4c
        third_party/miniz
        third_party/pugixml
        third_party/pdfio
    )
    target_link_libraries(MarkDownIt.tests PRIVATE
        md4c
        miniz
        pugixml
        pdfio
        # The text layout of the shared core is Core Text on macOS, and
        # the text engine makes its colors and paths with Core Graphics.
        "-framework CoreGraphics"
        "-framework CoreText"
        "-framework CoreFoundation"
    )
    target_compile_features(MarkDownIt.tests PRIVATE cxx_std_17)
    set_target_properties(MarkDownIt.tests PROPERTIES
        MACOSX_BUNDLE OFF
        WIN32_EXECUTABLE OFF
    )
    # The app layer's text engine, with its test, so the Core Text
    # measure path runs in CI and not only on real hardware.
    target_sources(MarkDownIt.tests PRIVATE
        tests/text_engine_mac_test.cpp
        src/macos/text_engine_mac.cpp
        src/macos/fonts_mac.mm
    )
elseif(BUILD_TESTS)
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
        tests/editor_features_test.cpp
        tests/findstate_test.cpp
        tests/findbar_ids_test.cpp
        tests/findbar_window_test.cpp
        tests/markers_test.cpp
        tests/outline_test.cpp
        tests/table_interaction_test.cpp
        tests/mermaid_model_test.cpp
        tests/mermaid_parse_test.cpp
        tests/mermaid_class_golden_test.cpp
        tests/mermaid_state_golden_test.cpp
        tests/mermaid_layout_test.cpp
        tests/mermaid_golden_test.cpp
        tests/mermaid_pie_golden_test.cpp
        tests/mermaid_seq_golden_test.cpp
        tests/mermaid_swimlanes_test.cpp
        tests/mermaid_measure_test.cpp
        tests/dom_mermaid_test.cpp
        tests/mermaid_cache_test.cpp
        tests/theme_test.cpp
        tests/perf_harness.cpp
        tests/test_main.cpp
        tests/svgtext_test.cpp
        tests/zoommodel_test.cpp
        tests/office_interop_smoke_test.cpp
        tests/office_docx_import_test.cpp
        tests/office_docx_export_test.cpp
        tests/office_pdf_export_test.cpp
        tests/office_markdown_bridge_test.cpp
        src/parser.cpp
        src/textbuffer.cpp
        src/layoutcache.cpp
        src/textlayout_dwrite.cpp
        src/editcontroller.cpp
        src/navigation.cpp
        src/tableedit.cpp
        src/undostack.cpp
        src/clipboard.cpp
        src/formatting.cpp
        src/autoformat.cpp
        src/inputfilter.cpp
        src/searchreplace.cpp
        src/findstate.cpp
        src/findbar.cpp
        src/findbar_model.cpp
        src/markers.cpp
        src/outline.cpp
        src/textdrag.cpp
        src/mermaid/class_parse.cpp
        src/mermaid/class_layout.cpp
        src/mermaid/state_parse.cpp
        src/mermaid/state_layout.cpp
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
        src/office/docx_import.cpp
        src/office/docx_export.cpp
        src/office/pdf_export.cpp
        src/office/markdown_bridge.cpp
    )
    target_link_libraries(MarkDownIt.tests PRIVATE
        md4c
        miniz
        pugixml
        pdfio
        user32
        gdi32
    )
    target_include_directories(MarkDownIt.tests PRIVATE
        src
        tests
        tests/mermaid
        third_party/md4c
        third_party/miniz
        third_party/pugixml
        third_party/pdfio
    )
    target_compile_definitions(MarkDownIt.tests PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    set_target_properties(MarkDownIt.tests PROPERTIES WIN32_EXECUTABLE OFF)
endif()

# ============================================================================
# macOS
# ============================================================================
#
# The macOS app is a PARALLEL UI LAYER on the shared core, not a fork. The
# shared sources are compiled as they are; the files under src/macos/ add
# the platform pieces. The Windows build above is untouched.
if(APPLE)
    # The shared core. These sources have no Windows headers, so the same
    # files feed the Windows build, the macOS build and both test targets.
    set(MARKDOWNIT_CORE_SOURCES
        src/parser.cpp
        src/textbuffer.cpp
        src/layoutcache.cpp
        src/editcontroller.cpp
        src/navigation.cpp
        src/tableedit.cpp
        src/undostack.cpp
        src/clipboard.cpp
        src/formatting.cpp
        src/autoformat.cpp
        src/inputfilter.cpp
        src/searchreplace.cpp
        src/findstate.cpp
        src/findbar_model.cpp
        src/markers.cpp
        src/outline.cpp
        src/textdrag.cpp
        src/svgtext.cpp
        src/settings.cpp
    )

    # The Mermaid engine. The layout is shared and golden tested; the mac
    # side only adds a painter and a Core Text measurement seam.
    file(GLOB MARKDOWNIT_MERMAID_SOURCES
         ${CMAKE_SOURCE_DIR}/src/mermaid/*.cpp)
    list(REMOVE_ITEM MARKDOWNIT_MERMAID_SOURCES
         ${CMAKE_SOURCE_DIR}/src/mermaid/measure_dwrite.cpp)

    # The document bridge for the Office formats.
    set(MARKDOWNIT_OFFICE_SOURCES
        src/office/docx_import.cpp
        src/office/docx_export.cpp
        src/office/pdf_export.cpp
        src/office/markdown_bridge.cpp
    )

    add_library(markdownit_core STATIC
        ${MARKDOWNIT_CORE_SOURCES}
        ${MARKDOWNIT_MERMAID_SOURCES}
        ${MARKDOWNIT_OFFICE_SOURCES}
        src/textlayout_coretext.cpp
    )
    target_include_directories(markdownit_core
        PUBLIC
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/office
            ${CMAKE_SOURCE_DIR}/third_party/md4c
            ${CMAKE_SOURCE_DIR}/third_party/miniz
            ${CMAKE_SOURCE_DIR}/third_party/pugixml
            ${CMAKE_SOURCE_DIR}/third_party/pdfio
    )
    target_compile_features(markdownit_core PUBLIC cxx_std_17)

    add_executable(MarkDownItMac
        src/macos/main_mac.mm
        src/macos/app_mac.cpp
        src/macos/content_view_mac.mm
        src/macos/renderer_mac.cpp
        src/macos/mermaid_painter_mac.cpp
        src/macos/text_engine_mac.cpp
        src/macos/fonts_mac.mm
        src/macos/clipboard_mac.mm
        src/macos/settings_mac.cpp
            src/macos/menu_mac.mm
        src/macos/outline_pane_mac.cpp
        src/macos/outline_pane_mac.h
        src/macos/outline_nsview_mac.h
        src/macos/outline_nsview_mac.mm
        src/macos/crash_trace_mac.cpp
        src/macos/image_mac.mm
        src/macos/image_draw_mac.mm
        src/macos/findbar_mac.mm
        src/macos/svg_mac.cpp
        src/macos/welcome_mac.cpp
    )
    # The Objective-C++ sources need the Objective-C runtime and the
    # frameworks, which the C++ sources do not.
    set_source_files_properties(
        src/macos/main_mac.mm
        src/macos/content_view_mac.mm
        src/macos/clipboard_mac.mm
        src/macos/menu_mac.mm
        src/macos/image_mac.mm
        src/macos/image_draw_mac.mm
        src/macos/findbar_mac.mm
        src/macos/fonts_mac.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc"
    )
    target_link_libraries(MarkDownItMac
        PRIVATE
            markdownit_core
            md4c
            miniz
            pugixml
            pdfio
            "-framework Cocoa"
            "-framework CoreGraphics"
            "-framework CoreText"
            "-framework AppKit"
            "-framework CoreServices"
    )
    target_compile_features(MarkDownItMac PRIVATE cxx_std_17)

    # The version lives in the Windows resource script, so the two
    # platforms never drift apart.
    file(READ "${CMAKE_SOURCE_DIR}/src/app.rc" MARKDOWNIT_APP_RC)
    string(REGEX MATCH "FILEVERSION[ \t]+([0-9,]+)" _version_match
           "${MARKDOWNIT_APP_RC}")
    string(REPLACE "," "." _version "${CMAKE_MATCH_1}")
    if(NOT _version)
        set(_version "0.0.0")
    endif()

    # The icon is a resource in the bundle. It comes from the same art
    # the Windows and the Store icons are rendered from.
    set(MARKDOWNIT_MAC_ICON "${CMAKE_SOURCE_DIR}/installer/macos/app.icns")
    set_source_files_properties("${MARKDOWNIT_MAC_ICON}"
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

    # The app bundle. CMake lays out Contents/MacOS, Contents/Resources
    # and the Info.plist.
    set_target_properties(MarkDownItMac PROPERTIES
        OUTPUT_NAME MarkDownIt
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST
            "${CMAKE_SOURCE_DIR}/installer/macos/Info.plist"
        MACOSX_BUNDLE_ICON_FILE "app"
        MACOSX_BUNDLE_BUNDLE_NAME "MarkDownIt"
        MACOSX_BUNDLE_GUI_IDENTIFIER "dk.olebhartvigsen.markdownit"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${_version}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${_version}"
    )
    target_sources(MarkDownItMac PRIVATE "${MARKDOWNIT_MAC_ICON}")

    # The bundled fonts. They give the mac build the same faces and the
    # same metrics as the Windows build, so a document lays out the same
    # way on both systems (see src/macos/fonts_mac.h). The copy into the
    # bundle happens at build time, before the signing step runs.
    set(MARKDOWNIT_MAC_FONTS
        "${CMAKE_SOURCE_DIR}/third_party/selawik/Selawik-Regular.ttf"
        "${CMAKE_SOURCE_DIR}/third_party/selawik/Selawik-Bold.ttf"
        "${CMAKE_SOURCE_DIR}/third_party/selawik/Selawik-Semibold.ttf"
        "${CMAKE_SOURCE_DIR}/third_party/cascadia-mono/CascadiaMono-Regular.ttf"
        "${CMAKE_SOURCE_DIR}/third_party/cascadia-mono/CascadiaMono-Bold.ttf"
        "${CMAKE_SOURCE_DIR}/third_party/cascadia-mono/CascadiaMono-Italic.ttf"
        "${CMAKE_SOURCE_DIR}/third_party/cascadia-mono/CascadiaMono-BoldItalic.ttf"
    )
    set_source_files_properties(${MARKDOWNIT_MAC_FONTS}
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/Fonts")
    target_sources(MarkDownItMac PRIVATE ${MARKDOWNIT_MAC_FONTS})
endif()  # APPLE
