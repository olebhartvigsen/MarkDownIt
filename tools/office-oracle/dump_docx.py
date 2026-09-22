#!/usr/bin/env python3
"""Independent .docx content-model dump built on python-docx.

This script is the oracle reader for the MarkDownIt Word interop work:
whatever it reports for a .docx is the structure the C++ importer is
expected to extract.

Output model (JSON)

  format    always "markdownit-office-docx-dump"
  version   model version, currently 1
  blocks    document body in document order
  sections  page setup of every section, in points

Block kinds

  title       paragraph using the "Title" style
  heading     paragraph using a "Heading N" style, carries "level"
  paragraph   any other paragraph
  list_item   paragraph with numbering, carries "list" {kind, level, num_fmt}
  table       table; "rows" is a list of rows, a row is a list of cells

Every block except tables carries "text" plus "runs". Run flags are DIRECT
formatting only; style-inherited formatting is not resolved, which keeps the
contract small and testable:

  text       run text
  bold       w:b present and on
  italic     w:i present and on
  mono       w:rFonts ascii/hAnsi/cs names a font in MONO_FONTS
  underline  w:u present and not "none"
  link       hyperlink target URL, null for a plain run

Table cells report text only.

Usage:
    python3 dump_docx.py <file.docx> [out.json]
"""
import argparse
import json
import re
import sys

from docx import Document
from docx.oxml.ns import qn
from docx.table import Table
from docx.text.hyperlink import Hyperlink
from docx.text.paragraph import Paragraph

MODEL_FORMAT = "markdownit-office-docx-dump"
MODEL_VERSION = 1

# Fonts treated as monospace. The set is part of the contract: a producer
# that wants a monospace run must use one of these names.
MONO_FONTS = frozenset(
    [
        "consolas",
        "courier",
        "courier new",
        "lucida console",
        "menlo",
        "monaco",
        "cascadia mono",
        "dejavu sans mono",
        "liberation mono",
        "monospace",
    ]
)

HEADING_RE = re.compile(r"^heading (\d+)$", re.IGNORECASE)


def iter_block_items(document):
    """Yield Paragraph and Table objects in body order."""
    body = document.element.body
    for child in body.iterchildren():
        if child.tag == qn("w:p"):
            yield Paragraph(child, document)
        elif child.tag == qn("w:tbl"):
            yield Table(child, document)


def numbering_map(document):
    """Return {numId: {ilvl: numFmt}} for the document numbering part."""
    try:
        numbering = document.part.numbering_part.element
    except (AttributeError, KeyError, NotImplementedError, ValueError):
        return {}

    abstract = {}
    for abstract_num in numbering.findall(qn("w:abstractNum")):
        levels = {}
        for lvl in abstract_num.findall(qn("w:lvl")):
            ilvl = int(lvl.get(qn("w:ilvl")) or 0)
            fmt = lvl.find(qn("w:numFmt"))
            levels[ilvl] = fmt.get(qn("w:val")) if fmt is not None else None
        abstract[abstract_num.get(qn("w:abstractNumId"))] = levels

    result = {}
    for num in numbering.findall(qn("w:num")):
        ref = num.find(qn("w:abstractNumId"))
        if ref is None:
            continue
        result[num.get(qn("w:numId"))] = abstract.get(ref.get(qn("w:val")), {})
    return result


def _num_pr_from_style(paragraph):
    """Return the w:numPr of the paragraph style chain, or None."""
    style = paragraph.style
    depth = 0
    while style is not None and depth < 8:
        element = style.element
        p_pr = element.find(qn("w:pPr"))
        if p_pr is not None:
            num_pr = p_pr.find(qn("w:numPr"))
            if num_pr is not None:
                return num_pr
        style = style.base_style
        depth += 1
    return None


def paragraph_list_info(paragraph, numbers):
    """Return {"kind", "level", "num_fmt"} for a numbered paragraph, else None."""
    p_pr = paragraph._p.pPr
    num_pr = p_pr.numPr if p_pr is not None else None
    if num_pr is None:
        num_pr = _num_pr_from_style(paragraph)
    if num_pr is None:
        return None

    ilvl_el = num_pr.find(qn("w:ilvl"))
    num_id_el = num_pr.find(qn("w:numId"))
    if num_id_el is None:
        return None
    level = int(ilvl_el.get(qn("w:val")) or 0) if ilvl_el is not None else 0
    num_id = num_id_el.get(qn("w:val"))
    if num_id is None or int(num_id) == 0:
        return None

    levels = numbers.get(num_id, {})
    num_fmt = levels.get(level, levels.get(0))
    kind = "bullet" if (num_fmt or "").lower() == "bullet" else "ordered"
    return {"kind": kind, "level": level, "num_fmt": num_fmt}


def _run_font_names(run):
    r_pr = run._r.rPr
    if r_pr is None:
        return set()
    fonts = r_pr.find(qn("w:rFonts"))
    if fonts is None:
        return set()
    names = set()
    for attr in ("w:ascii", "w:hAnsi", "w:cs"):
        value = fonts.get(qn(attr))
        if value:
            names.add(value)
    return names


def run_model(run, link_url=None):
    names = _run_font_names(run)
    underline = run.underline
    return {
        "text": run.text,
        "bold": run.bold is True,
        "italic": run.italic is True,
        "mono": any(name.casefold() in MONO_FONTS for name in names),
        "underline": bool(underline) if underline is not None else False,
        "link": link_url,
    }


def inline_runs(paragraph):
    """Return [(Run, link_url|None)] in document order, hyperlinks included."""
    items = []
    for content in paragraph.iter_inner_content():
        if isinstance(content, Hyperlink):
            for run in content.runs:
                items.append((run, content.url))
        else:
            items.append((content, None))
    return items


def paragraph_model(paragraph, numbers):
    pairs = inline_runs(paragraph)
    runs = [run_model(run, url) for run, url in pairs]
    text = "".join(run.text for run, _ in pairs)

    style_name = paragraph.style.name if paragraph.style is not None else ""
    list_info = paragraph_list_info(paragraph, numbers)
    heading = HEADING_RE.match(style_name or "")

    if list_info is not None:
        return {"kind": "list_item", "text": text, "list": list_info, "runs": runs}
    if heading:
        return {
            "kind": "heading",
            "level": int(heading.group(1)),
            "text": text,
            "runs": runs,
        }
    if (style_name or "").casefold() == "title":
        return {"kind": "title", "text": text, "runs": runs}
    return {"kind": "paragraph", "text": text, "runs": runs}


def table_model(table):
    rows = []
    for row in table.rows:
        cells = []
        for cell in row.cells:
            parts = [paragraph.text for paragraph in cell.paragraphs]
            cells.append({"text": "\n".join(parts)})
        rows.append(cells)
    return {"kind": "table", "rows": rows}


def section_model(section):
    def pt(value):
        return None if value is None else round(float(value.pt), 2)

    return {
        "page_width_pt": pt(section.page_width),
        "page_height_pt": pt(section.page_height),
        "margins_pt": {
            "top": pt(section.top_margin),
            "right": pt(section.right_margin),
            "bottom": pt(section.bottom_margin),
            "left": pt(section.left_margin),
        },
    }


def dump_docx(path):
    document = Document(path)
    numbers = numbering_map(document)

    blocks = []
    for block in iter_block_items(document):
        if isinstance(block, Table):
            blocks.append(table_model(block))
        else:
            blocks.append(paragraph_model(block, numbers))

    return {
        "format": MODEL_FORMAT,
        "version": MODEL_VERSION,
        "blocks": blocks,
        "sections": [section_model(section) for section in document.sections],
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("docx", help="input .docx file")
    parser.add_argument("out", nargs="?", help="write JSON here instead of stdout")
    args = parser.parse_args(argv)

    payload = json.dumps(dump_docx(args.docx), indent=2, ensure_ascii=False) + "\n"
    if args.out:
        with open(args.out, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(payload)
    else:
        sys.stdout.write(payload)
    return 0


if __name__ == "__main__":
    sys.exit(main())
