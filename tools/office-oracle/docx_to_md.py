#!/usr/bin/env python3
"""Reference .docx to Markdown mapping for MarkDownIt.

This file IS the contract: the mapping documented here is what MarkDownIt's
Word importer must reproduce, and what a MarkDownIt Markdown export is
compared against. The rules are deliberately literal, so every rule can be
implemented and tested on its own.

Mapping rules
-------------
Blocks
  title       "# " plus the text
  heading     ATX heading, one level lower than the Word level, because the
              document title owns "#": Word "Heading 1" becomes "## ",
              Word "Heading 2" becomes "### ", and so on
  paragraph   the inline text of the paragraph, on one line
  list_item   bullet marker "- " per item; ordered marker "N. " numbered
              from 1 inside each list; nesting is two spaces per level
  table       GFM pipe table; the first row is the header row, followed by
              the separator row, then every remaining row in order

Inline runs
  bold        **text**
  italic      *text*
  monospace   `text`
  underline   no Markdown equivalent, emitted as plain text
  link        [text](url), wrapping the formatted text

Formatting nests inside out: monospace, then bold, then italic, then the
link. A run that is both bold and italic therefore becomes ***text***.

Blank lines separate blocks, except between consecutive list items, which
stay on adjacent lines. File ends with a single trailing newline.

Usage:
    python3 docx_to_md.py <file.docx> [out.md]
"""
import argparse
import re
import sys

import dump_docx

WHITESPACE_RE = re.compile(r"\s+")


def inline_markdown(runs):
    """Render runs to inline Markdown, applying the nesting rules."""
    parts = []
    for run in runs:
        text = run["text"]
        if not text:
            continue
        if run["mono"]:
            text = "`" + text + "`"
        if run["bold"]:
            text = "**" + text + "**"
        if run["italic"]:
            text = "*" + text + "*"
        if run["link"]:
            text = "[" + text + "](" + run["link"] + ")"
        parts.append(text)
    return "".join(parts)


def cell_text(text):
    """Collapse whitespace in a table cell and escape the pipe character."""
    return WHITESPACE_RE.sub(" ", text).strip().replace("|", "\\|")


def table_lines(block):
    """Render a table block as a GFM pipe table."""
    rows = block["rows"]
    if not rows:
        return []
    width = len(rows[0])
    lines = ["| " + " | ".join(cell_text(cell["text"]) for cell in rows[0]) + " |"]
    lines.append("| " + " | ".join(["---"] * width) + " |")
    for row in rows[1:]:
        lines.append("| " + " | ".join(cell_text(cell["text"]) for cell in row) + " |")
    return lines


def docx_to_md(path):
    model = dump_docx.dump_docx(path)

    chunks = []
    previous_kind = None
    previous_list_kind = None
    counters = {}

    for block in model["blocks"]:
        kind = block["kind"]

        if kind == "table":
            chunk = "\n".join(table_lines(block))
            glue = "\n\n" if previous_kind is not None else ""
            previous_list_kind = None

        elif kind == "list_item":
            list_kind = block["list"]["kind"]
            level = block["list"]["level"]
            if previous_kind != "list_item" or previous_list_kind != list_kind:
                counters = {}
            counters = {key: value for key, value in counters.items() if key <= level}
            if list_kind == "ordered":
                counters[level] = counters.get(level, 0) + 1
                marker = "%d. " % counters[level]
            else:
                marker = "- "
            chunk = "  " * level + marker + inline_markdown(block["runs"])
            glue = "\n" if previous_kind == "list_item" else ("\n\n" if previous_kind else "")
            previous_list_kind = list_kind

        elif kind == "title":
            chunk = "# " + inline_markdown(block["runs"])
            glue = "\n\n" if previous_kind else ""
            previous_list_kind = None

        elif kind == "heading":
            chunk = "#" * (block["level"] + 1) + " " + inline_markdown(block["runs"])
            glue = "\n\n" if previous_kind else ""
            previous_list_kind = None

        else:
            chunk = inline_markdown(block["runs"])
            glue = "\n\n" if previous_kind else ""
            previous_list_kind = None

        chunks.append(glue + chunk)
        previous_kind = kind

    return "".join(chunks) + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("docx", help="input .docx file")
    parser.add_argument("out", nargs="?", help="write Markdown here instead of stdout")
    args = parser.parse_args(argv)

    payload = docx_to_md(args.docx)
    if args.out:
        with open(args.out, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(payload)
    else:
        sys.stdout.write(payload)
    return 0


if __name__ == "__main__":
    sys.exit(main())
