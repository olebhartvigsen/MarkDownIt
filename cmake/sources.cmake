# The one source manifest for every MarkDownIt target.
#
# The Windows exe, the macOS core library and both test targets all
# compose their sources from these groups, so the shared pools cannot
# drift apart list by list. The Windows UI files and the mac UI files
# stay in their target blocks in CMakeLists.txt.
#
# Every group carries a `usage:` comment listing the surfaces allowed
# to use it. tools/check_sources_listed.py reads those comments and
# fails when a group is used somewhere else, or nowhere. The surface
# names:
#
#   windows-app    the MarkDownIt exe target
#   windows-tests  the MarkDownIt.tests target on Windows
#   macos-core     the markdownit_core static library
#   macos-app      the MarkDownItMac app bundle
#   macos-tests    the MarkDownIt.tests target on macOS

# The shared core: no Windows headers, fed to the exe and the mac core.
# usage: windows-app macos-core windows-tests macos-tests
set(MARKDOWNIT_CORE_SOURCES
    src/parser.cpp
    src/textbuffer.cpp
    src/layout/break_heading_layout.cpp
    src/layout/text_runs.cpp
    src/layout/paragraph_layout.cpp
    src/layout/quote_layout.cpp
    src/layout/list_layout.cpp
    src/layout/code_layout.cpp
    src/layout/table_layout.cpp
    src/layout/svg_mermaid_layout.cpp
    src/layout/display_player.cpp
    src/layoutcache.cpp
    src/editcontroller.cpp
    src/documentcontroller.cpp
    src/commandstate.cpp
    src/documentfile.cpp
    src/navigation.cpp
    src/tableedit.cpp
    src/undostack.cpp
    src/clipboard.cpp
    src/formatting.cpp
    src/autoformat.cpp
    src/inputfilter.cpp
    src/searchreplace.cpp
    src/unicode_fold.cpp
    src/findstate.cpp
    src/findbar_model.cpp
    src/markers.cpp
    src/outline.cpp
    src/textdrag.cpp
    src/svgtext.cpp
    src/settings.cpp
)

# The SVG reader. It lives under src/macos/ for historical reasons but
# has no platform headers, so both platforms compile it. The mac core
# library does not take it: the mac app target compiles it directly.
# usage: windows-app macos-app windows-tests macos-tests
set(MARKDOWNIT_SVG_SHARED_SOURCES
    src/macos/svg_mac.cpp
)

# The Mermaid engine: diagram parsing and the dagre port, shared by the
# exe and the mac core without platform files.
# usage: windows-app macos-core windows-tests macos-tests
set(MARKDOWNIT_MERMAID_SOURCES
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
    src/mermaid/parse.cpp
    src/mermaid/pie_parse.cpp
    src/mermaid/pie_layout.cpp
    src/mermaid/seq_parse.cpp
    src/mermaid/seq_layout.cpp
    src/mermaid/gantt_parse.cpp
    src/mermaid/gantt_layout.cpp
    src/mermaid/swimlanes.cpp
)

# The state diagram engine. The macOS app and both test suites build
# it; the Windows exe does not list it. That difference is a row in
# references/platform-parity.md.
# usage: macos-core windows-tests macos-tests
set(MARKDOWNIT_MERMAID_STATE_SOURCES
    src/mermaid/state_parse.cpp
    src/mermaid/state_layout.cpp
)

# DirectWrite measurement for the Mermaid engine, Windows only.
# usage: windows-app windows-tests
set(MARKDOWNIT_MERMAID_WINDOWS_SOURCES
    src/mermaid/measure_dwrite.cpp
)

# The pre-refactor Mermaid entry points, used only by the Windows test
# suite while its tests are migrated to the shared engine.
# usage: windows-tests
set(MARKDOWNIT_MERMAID_LEGACY_SOURCES
    src/mermaid.cpp
    src/mermaidlayout.cpp
    src/diagramcache.cpp
)

# The document bridge for the Office formats.
# usage: windows-app macos-core windows-tests macos-tests
set(MARKDOWNIT_OFFICE_SOURCES
    src/office/docx_import.cpp
    src/office/docx_export.cpp
    src/office/pdf_export.cpp
    src/office/pdf_import.cpp
    src/office/markdown_bridge.cpp
    src/office/office_session.cpp
)

# Windows platform half of a shared interface: registry settings and
# Win32 clipboard, behind the structs in src/settings.h / src/clipboard.h.
# usage: windows-app windows-tests
set(MARKDOWNIT_WINDOWS_APP_SOURCES
    src/settings_win.cpp
    src/clipboard_win.cpp
    src/windows/text_engine_win.cpp
    src/windows/render_target_win.cpp
)
