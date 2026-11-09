#!/usr/bin/env python3
"""Tests for check_sources_listed.py, each on a deliberately bad tree."""

import contextlib
import io
import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_sources_listed as csl  # noqa: E402

REPO = os.path.dirname(HERE)

MANIFEST = """# group one
# usage: windows-app macos-core windows-tests macos-tests
set(MARKDOWNIT_CORE_SOURCES
    src/parser.cpp
)

# group two
# usage: windows-app
set(MARKDOWNIT_WINDOWS_SOURCES
    src/win.cpp
)
"""

CMAKE = """project(x)
add_executable(MarkDownIt WIN32
    ${MARKDOWNIT_CORE_SOURCES}
    ${MARKDOWNIT_WINDOWS_SOURCES}
    src/main.cpp
)

if(BUILD_TESTS AND APPLE)
    add_executable(MarkDownIt.tests
        ${MARKDOWNIT_CORE_SOURCES}
        src/textlayout_coretext.cpp
    )
elseif(BUILD_TESTS)
    add_executable(MarkDownIt.tests
        ${MARKDOWNIT_CORE_SOURCES}
    )
endif()

# Both platforms record the unchanged macOS SVG scene
if(APPLE)
    add_library(markdownit_core STATIC
        ${MARKDOWNIT_CORE_SOURCES}
    )
    add_executable(MarkDownItMac
        src/macos/main_mac.mm
    )
endif()
"""

LEDGER = """# Platform parity ledger

## Sources built by no target

- `src/textlayout_generic.cpp`: the third engine, owned by no target.
"""

FILES = ("src/parser.cpp", "src/win.cpp", "src/main.cpp",
         "src/textlayout_coretext.cpp", "src/textlayout_generic.cpp",
         "src/macos/main_mac.mm")


class SourcesListed(unittest.TestCase):
    def make_tree(self, manifest=MANIFEST, cmake=CMAKE, ledger=LEDGER,
                  files=FILES):
        from pathlib import Path
        root = tempfile.mkdtemp()
        for rel in files:
            path = Path(root) / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        for rel, text in (
                ("cmake/sources.cmake", manifest),
                ("CMakeLists.txt", cmake),
                ("references/platform-parity.md", ledger)):
            path = Path(root) / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        for d in ("src/mermaid", "src/office"):
            (Path(root) / d).mkdir(parents=True, exist_ok=True)
        return root

    def _run_on(self, root):
        hold = sys.argv
        sys.argv = ["x", root]
        buf = io.StringIO()
        try:
            with contextlib.redirect_stdout(buf):
                rc = csl.main()
        finally:
            sys.argv = hold
        return rc, buf.getvalue()

    def tearDown(self):
        shutil.rmtree(getattr(self, "root", "/nonexistent"), ignore_errors=True)

    def test_repo_is_clean(self):
        rc, out = self._run_on(REPO)
        self.assertEqual(rc, 0, out)

    def test_minimal_tree_passes(self):
        self.root = self.make_tree()
        rc, out = self._run_on(self.root)
        self.assertEqual(rc, 0, out)

    def test_unlisted_source_fails(self):
        self.root = self.make_tree()
        from pathlib import Path
        (Path(self.root) / "src" / "office" /
         "docx_import.cpp").write_text("// new\n")
        rc, out = self._run_on(self.root)
        self.assertEqual(rc, 1)
        self.assertIn("src/office/docx_import.cpp is in no manifest group", out)

    def test_group_on_wrong_surface_fails(self):
        self.root = self.make_tree(cmake=CMAKE.replace(
            "if(BUILD_TESTS AND APPLE)\n    add_executable(MarkDownIt.tests\n"
            "        ${MARKDOWNIT_CORE_SOURCES}\n",
            "if(BUILD_TESTS AND APPLE)\n    add_executable(MarkDownIt.tests\n"
            "        ${MARKDOWNIT_CORE_SOURCES}\n"
            "        ${MARKDOWNIT_WINDOWS_SOURCES}\n"))
        rc, out = self._run_on(self.root)
        self.assertEqual(rc, 1)
        self.assertIn("MARKDOWNIT_WINDOWS_SOURCES is used on macos-tests", out)

    def test_group_missing_from_surface_fails(self):
        self.root = self.make_tree(cmake=CMAKE.replace(
            "add_executable(MarkDownIt WIN32\n"
            "    ${MARKDOWNIT_CORE_SOURCES}\n"
            "    ${MARKDOWNIT_WINDOWS_SOURCES}\n",
            "add_executable(MarkDownIt WIN32\n"
            "    ${MARKDOWNIT_CORE_SOURCES}\n"))
        rc, out = self._run_on(self.root)
        self.assertEqual(rc, 1)
        self.assertIn("MARKDOWNIT_WINDOWS_SOURCES is declared for "
                      "windows-app but no target there uses it", out)

    def test_literal_duplicates_group_fails(self):
        self.root = self.make_tree(cmake=CMAKE.replace(
            "    ${MARKDOWNIT_WINDOWS_SOURCES}\n",
            "    ${MARKDOWNIT_WINDOWS_SOURCES}\n    src/win.cpp\n"))
        rc, out = self._run_on(self.root)
        self.assertEqual(rc, 1)
        self.assertIn("src/win.cpp is a literal in windows-app", out)

    def test_file_dropped_from_manifest_fails(self):
        self.root = self.make_tree(
            manifest=MANIFEST.replace("    src/win.cpp\n", ""))
        rc, out = self._run_on(self.root)
        self.assertEqual(rc, 1)
        self.assertIn("src/win.cpp is in no manifest group", out)

    def test_unbuilt_without_ledger_row_fails(self):
        self.root = self.make_tree(ledger="# Platform parity ledger\n\n"
                                "## Sources built by no target\n\n"
                                "(nothing)\n")
        rc, out = self._run_on(self.root)
        self.assertEqual(rc, 1)
        self.assertIn("src/textlayout_generic.cpp is in no manifest group "
                      "and no target list", out)

    def test_stale_unbuilt_row_fails(self):
        self.root = self.make_tree(ledger=LEDGER +
             "- `src/win.cpp`: says unbuilt, but the manifest builds it.\n")
        rc, out = self._run_on(self.root)
        self.assertEqual(rc, 1)
        self.assertIn("src/win.cpp is listed as unbuilt in the ledger but "
                      "it is built", out)


if __name__ == "__main__":
    unittest.main()
