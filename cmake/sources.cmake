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
    src/analytics.cpp
    src/parser.cpp
    src/csv.cpp
    src/csvfilter.cpp
    src/csvdocument.cpp
    src/json.cpp
    src/imagepath.cpp
    src/documentparse.cpp
    src/textbuffer.cpp
    src/layout/break_heading_layout.cpp
    src/layout/text_runs.cpp
    src/layout/paragraph_layout.cpp
    src/layout/quote_layout.cpp
    src/layout/list_layout.cpp
    src/layout/code_layout.cpp
    src/layout/table_layout.cpp
    src/layout/range_highlights.cpp
    src/layout/block_overlays.cpp
    src/layout/chips.cpp
    src/layout/frame.cpp
    src/layout/svg_mermaid_layout.cpp
    src/layout/display_player.cpp
    src/layout/print_pages.cpp
    src/print_pdf.cpp
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
    src/svglink.cpp
    src/settings.cpp
)

# The document renderer: the frame of one laid out document, in the shared
# root and free of platform headers. It reads no theme alias directly; the
# palette and the font names go through the portable helpers, so both
# ports build it from this one group. The plan's slice 10 entry and the
# parity ledger say why it took until the Windows frame adoption.
# usage: windows-app macos-core windows-tests macos-tests
set(MARKDOWNIT_DOCUMENT_RENDERER_SOURCES
    src/document_renderer.cpp
    src/qa_strings.cpp
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
    src/mermaid/elk_profile.cpp
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

# The state diagram engine. The parser promotes state fences on both
# ports, so every target that builds the parser needs it.
# usage: windows-app macos-core windows-tests macos-tests
set(MARKDOWNIT_MERMAID_STATE_SOURCES
    src/mermaid/state_parse.cpp
    src/mermaid/state_layout.cpp
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
# Win32 clipboard, behind the structs in src/settings.h / src/clipboard.h,
# and the WinHTTP telemetry transport behind platform::TelemetrySink.
# usage: windows-app windows-tests
set(MARKDOWNIT_WINDOWS_APP_SOURCES
    src/settings_win.cpp
    src/clipboard_win.cpp
    src/windows/text_engine_win.cpp
    src/windows/render_target_win.cpp
    src/windows/text_range_source_win.cpp
    src/windows/telemetry_win.cpp
)

# The Windows halves of the seams the shared document renderer draws
# through: the Direct2D SVG scene, the WIC image service and the QA log.
# They are named for their job and carry no shared logic; the renderer
# itself stays platform blind.
# usage: windows-app windows-tests
set(MARKDOWNIT_WINDOWS_RENDERER_SOURCES
    src/windows/svg_raster_win.cpp
    src/windows/image_source_win.cpp
    src/windows/qa_trace_win.cpp
)
