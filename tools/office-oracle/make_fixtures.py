#!/usr/bin/env python3
"""Deterministic fixture generator for the MarkDownIt office interop oracle.

Writes the reference fixtures and the goldens that MarkDownIt's Word and PDF
support is measured against:

  <fixtures>/reference.docx    canonical Word document
  <fixtures>/reference.pdf     same content model, produced by reportlab
  <fixtures>/large.docx        240-section document, over 200 Word pages
  <golden>/reference_docx.json python-docx content model of reference.docx
  <golden>/reference_pdf.json  pypdf page dump of reference.pdf
  <golden>/reference.md        reference.docx through the docx to Markdown map
  <golden>/large_docx.json     size summary of large.docx

Both fixtures carry the same content: title, Heading 1 with a formatted
paragraph, Heading 2 with a bullet list (one nested item) and a numbered
list, Heading 2 with a 3x3 table, and a hyperlink. The Word file uses A4
portrait with 2 cm margins; the PDF is A4 portrait with two pages, a forced
page break and a "Side N" footer.

Byte determinism
----------------
python-docx stamps the current time into the zip members and reportlab
stamps the current time into the PDF, so both files are post-processed:

  * core properties get fixed timestamps and a fixed author
  * the saved docx package is rewritten: members sorted by name, deflate
    level 9, fixed member timestamps, fixed external attributes, and the
    template thumbnail part is dropped (this fixture carries no images)
  * the PDF is built in reportlab invariant mode with pageCompression=0, so
    no clock value and no zlib version can change a byte, and
    SOURCE_DATE_EPOCH is neutralised for the duration of the build

Running the generator twice must produce byte-identical files. The CI job
office-oracle regenerates everything and compares it with the committed
copies.

Usage:
    python3 make_fixtures.py [--fixtures-dir DIR] [--golden-dir DIR]
"""
import argparse
import datetime
import json
import os
import sys
import xml.sax.saxutils as saxutils
import zipfile

from docx import Document
from docx.enum.section import WD_ORIENT
from docx.oxml import parse_xml
from docx.oxml.ns import nsdecls, qn
from docx.opc.constants import RELATIONSHIP_TYPE as RT
from docx.shared import Cm, RGBColor

import docx_to_md
import dump_docx
import dump_pdf

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
DEFAULT_FIXTURES_DIR = os.path.join(REPO_ROOT, "tests", "office", "fixtures")
DEFAULT_GOLDEN_DIR = os.path.join(REPO_ROOT, "tests", "office", "golden")

# --------------------------------------------------------------------------
# Content model, shared by the Word and the PDF fixture
# --------------------------------------------------------------------------

TITLE_TEXT = "MarkDownIt Office Interop Reference"
HEADING_ONE = "Indledning"
HEADING_TWO_LISTS = "Punktliste"
HEADING_TWO_TABLE = "Tabel"

# (text, flags) with flags from bold, italic, mono, underline, link
INTRO_RUNS = [
    ("Dette er ", {}),
    ("fed tekst", {"bold": True}),
    (", ", {}),
    ("kursiv tekst", {"italic": True}),
    (" og ", {}),
    ("monospace tekst", {"mono": True}),
    (". Understreget: ", {}),
    ("understreget tekst", {"underline": True}),
    (". Danske tegn: \u00e6 \u00f8 \u00e5 \u00c6 \u00d8 \u00c5.", {}),
]

BULLET_ITEMS = ["F\u00f8rste punkt", "Andet punkt", "Tredje punkt"]
NESTED_ITEM_PARENT = "Andet punkt"
NESTED_ITEM_TEXT = "Underelement til andet punkt"
ORDERED_ITEMS = ["Trin et", "Trin to", "Trin tre"]

TABLE_DATA = [
    ["Navn", "Antal", "Note"],
    ["\u00c6ble", "3", "Moden"],
    ["\u00d8l", "12", "Kold"],
    ["\u00c5l", "1", "Fersk"],
]

LINK_URL = "https://example.com"
LINK_TEXT = "example.com"
LINK_PREFIX = "L\u00e6s mere p\u00e5 "
LINK_SUFFIX = " for detaljer."

MONO_FONT_NAME = "Consolas"

LARGE_SECTIONS = 240
LARGE_BODY_LINES = 30

# Page estimate model: points per block under the fixture document's own
# style metrics. Body text is 11 pt with 1.15 line spacing and 10 pt space
# after; Heading 1 is 14 pt with 24 pt space before. Line height uses the
# template theme font metrics (Cambria: 1.172 em), which is the smaller of
# the plausible metrics and therefore a conservative lower bound.
CAMBRIA_LINE_EM = 1.172
BODY_FONT_PT = 11.0
BODY_LINE_MULTIPLE = 1.15
BODY_SPACE_AFTER_PT = 10.0
HEADING_FONT_PT = 14.0
HEADING_SPACE_BEFORE_PT = 24.0
USABLE_TEXT_HEIGHT_PT = 841.89 - 2 * 56.69

# --------------------------------------------------------------------------
# Word fixture
# --------------------------------------------------------------------------

FIXED_TIMESTAMP = datetime.datetime(2020, 1, 1, 0, 0, 0, tzinfo=datetime.timezone.utc)
FIXED_AUTHOR = "MarkDownIt office oracle"

BULLET_NUM_ID = 10
ORDERED_NUM_ID = 11
BULLET_ABSTRACT_ID = 100
ORDERED_ABSTRACT_ID = 101

BULLET_ABSTRACT_XML = (
    '<w:abstractNum %s w:abstractNumId="%d">'
    '<w:multiLevelType w:val="multilevel"/>'
    '<w:lvl w:ilvl="0"><w:start w:val="1"/><w:numFmt w:val="bullet"/>'
    '<w:lvlText w:val="\u2022"/><w:lvlJc w:val="left"/>'
    '<w:pPr><w:ind w:left="720" w:hanging="360"/></w:pPr></w:lvl>'
    '<w:lvl w:ilvl="1"><w:start w:val="1"/><w:numFmt w:val="bullet"/>'
    '<w:lvlText w:val="o"/><w:lvlJc w:val="left"/>'
    '<w:pPr><w:ind w:left="1440" w:hanging="360"/></w:pPr></w:lvl>'
    "</w:abstractNum>"
) % (nsdecls("w"), BULLET_ABSTRACT_ID)

ORDERED_ABSTRACT_XML = (
    '<w:abstractNum %s w:abstractNumId="%d">'
    '<w:multiLevelType w:val="singleLevel"/>'
    '<w:lvl w:ilvl="0"><w:start w:val="1"/><w:numFmt w:val="decimal"/>'
    '<w:lvlText w:val="%%1."/><w:lvlJc w:val="left"/>'
    '<w:pPr><w:ind w:left="720" w:hanging="360"/></w:pPr></w:lvl>'
    "</w:abstractNum>"
) % (nsdecls("w"), ORDERED_ABSTRACT_ID)

BULLET_NUM_XML = (
    '<w:num %s w:numId="%d"><w:abstractNumId w:val="%d"/></w:num>'
) % (nsdecls("w"), BULLET_NUM_ID, BULLET_ABSTRACT_ID)

ORDERED_NUM_XML = (
    '<w:num %s w:numId="%d"><w:abstractNumId w:val="%d"/></w:num>'
) % (nsdecls("w"), ORDERED_NUM_ID, ORDERED_ABSTRACT_ID)

FIXED_ZIP_DATE = (1980, 1, 1, 0, 0, 0)
THUMBNAIL_PART = "docProps/thumbnail.jpeg"
THUMBNAIL_CONTENT_TYPE = b'<Default Extension="jpeg" ContentType="image/jpeg"/>'
THUMBNAIL_RELATIONSHIP = (
    b'<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/'
    b'relationships/metadata/thumbnail" Target="docProps/thumbnail.jpeg"/>'
)


def _set_core_properties(document, title):
    properties = document.core_properties
    properties.title = title
    properties.author = FIXED_AUTHOR
    properties.last_modified_by = FIXED_AUTHOR
    properties.created = FIXED_TIMESTAMP
    properties.modified = FIXED_TIMESTAMP
    properties.revision = 1
    properties.comments = "Deterministic fixture for the MarkDownIt office interop oracle."


def _set_page_setup(document):
    section = document.sections[0]
    section.orientation = WD_ORIENT.PORTRAIT
    section.page_width = Cm(21.0)
    section.page_height = Cm(29.7)
    section.left_margin = Cm(2.0)
    section.right_margin = Cm(2.0)
    section.top_margin = Cm(2.0)
    section.bottom_margin = Cm(2.0)


def _add_numbering(document):
    """Add the fixture's own bullet and decimal numbering definitions.

    The template definitions are kept so the template styles stay valid; the
    fixture paragraphs point at numId 10 (bullets) and numId 11 (decimal).
    """
    numbering = document.part.numbering_part.element
    first_num = numbering.find(qn("w:num"))
    for xml in (BULLET_ABSTRACT_XML, ORDERED_ABSTRACT_XML):
        node = parse_xml(xml)
        if first_num is not None:
            first_num.addprevious(node)
        else:
            numbering.append(node)
    numbering.append(parse_xml(BULLET_NUM_XML))
    numbering.append(parse_xml(ORDERED_NUM_XML))


def _apply_numbering(paragraph, num_id, level):
    p_pr = paragraph._p.get_or_add_pPr()
    num_pr = p_pr.get_or_add_numPr()
    num_pr.get_or_add_ilvl().val = level
    num_pr.get_or_add_numId().val = num_id


def _style_run(run, flags):
    if flags.get("bold"):
        run.bold = True
    if flags.get("italic"):
        run.italic = True
    if flags.get("underline"):
        run.underline = True
    if flags.get("mono"):
        run.font.name = MONO_FONT_NAME


def _add_hyperlink(paragraph, url, text):
    relationship_id = paragraph.part.relate_to(url, RT.HYPERLINK, is_external=True)
    xml = (
        '<w:hyperlink %s r:id="%s">'
        '<w:r><w:rPr><w:color w:val="0563C1"/><w:u w:val="single"/></w:rPr>'
        '<w:t xml:space="preserve">%s</w:t></w:r>'
        "</w:hyperlink>"
    ) % (nsdecls("w", "r"), relationship_id, saxutils.escape(text))
    paragraph._p.append(parse_xml(xml))


def _add_intro_paragraph(document):
    paragraph = document.add_paragraph()
    for text, flags in INTRO_RUNS:
        run = paragraph.add_run(text)
        _style_run(run, flags)
    return paragraph


def _add_list(document, items, num_id, level=0):
    for text in items:
        paragraph = document.add_paragraph(text, style="List Paragraph")
        _apply_numbering(paragraph, num_id, level)


def _add_bullet_list(document):
    first, second, third = BULLET_ITEMS
    for text in (first, second):
        paragraph = document.add_paragraph(text, style="List Paragraph")
        _apply_numbering(paragraph, BULLET_NUM_ID, 0)
    nested = document.add_paragraph(NESTED_ITEM_TEXT, style="List Paragraph")
    _apply_numbering(nested, BULLET_NUM_ID, 1)
    paragraph = document.add_paragraph(third, style="List Paragraph")
    _apply_numbering(paragraph, BULLET_NUM_ID, 0)


def _add_table(document):
    table = document.add_table(rows=len(TABLE_DATA), cols=len(TABLE_DATA[0]))
    table.style = "Table Grid"
    for row_index, row_data in enumerate(TABLE_DATA):
        for column_index, cell_text in enumerate(row_data):
            cell = table.cell(row_index, column_index)
            paragraph = cell.paragraphs[0]
            run = paragraph.add_run(cell_text)
            if row_index == 0:
                run.bold = True
    return table


def _add_link_paragraph(document):
    paragraph = document.add_paragraph()
    paragraph.add_run(LINK_PREFIX)
    _add_hyperlink(paragraph, LINK_URL, LINK_TEXT)
    paragraph.add_run(LINK_SUFFIX)
    return paragraph


def build_reference_docx(path):
    document = Document()
    _set_core_properties(document, TITLE_TEXT)
    _set_page_setup(document)
    _add_numbering(document)

    document.add_heading(TITLE_TEXT, level=0)
    document.add_heading(HEADING_ONE, level=1)
    _add_intro_paragraph(document)
    document.add_heading(HEADING_TWO_LISTS, level=2)
    _add_bullet_list(document)
    _add_list(document, ORDERED_ITEMS, ORDERED_NUM_ID)
    document.add_heading(HEADING_TWO_TABLE, level=2)
    _add_table(document)
    _add_link_paragraph(document)

    save_document(document, path)
    return path


def build_large_docx(path):
    document = Document()
    _set_core_properties(document, "MarkDownIt 200-page interop fixture")
    _set_page_setup(document)

    for section in range(1, LARGE_SECTIONS + 1):
        document.add_heading("Afsnit %d" % section, level=1)
        for line in range(1, LARGE_BODY_LINES + 1):
            document.add_paragraph(
                "Side %d, linje %d: MarkDownIt 200-side interop test." % (section, line)
            )

    save_document(document, path)
    return path


def estimate_large_pages():
    """Conservative page estimate for large.docx, in A4 pages."""
    body_height = BODY_FONT_PT * CAMBRIA_LINE_EM * BODY_LINE_MULTIPLE + BODY_SPACE_AFTER_PT
    heading_height = (
        HEADING_FONT_PT * CAMBRIA_LINE_EM * BODY_LINE_MULTIPLE + HEADING_SPACE_BEFORE_PT
    )
    total = LARGE_SECTIONS * (heading_height + LARGE_BODY_LINES * body_height)
    return total / USABLE_TEXT_HEIGHT_PT


# --------------------------------------------------------------------------
# Deterministic package writing
# --------------------------------------------------------------------------


def _strip_once(members, name, needle):
    payload = members[name]
    count = payload.count(needle)
    if count != 1:
        raise RuntimeError(
            "expected exactly one %r in %s, found %d; the python-docx template changed"
            % (needle[:40], name, count)
        )
    members[name] = payload.replace(needle, b"", 1)


def normalize_package(source_path, target_path):
    """Rewrite a saved docx package into a byte-stable zip archive."""
    with zipfile.ZipFile(source_path) as archive:
        members = {info.filename: archive.read(info.filename) for info in archive.infolist()}

    members.pop(THUMBNAIL_PART, None)
    _strip_once(members, "[Content_Types].xml", THUMBNAIL_CONTENT_TYPE)
    _strip_once(members, "_rels/.rels", THUMBNAIL_RELATIONSHIP)

    with zipfile.ZipFile(
        target_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
    ) as archive:
        for name in sorted(members):
            info = zipfile.ZipInfo(name, date_time=FIXED_ZIP_DATE)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = 0o600 << 16
            info.internal_attr = 0
            archive.writestr(info, members[name])


def save_document(document, path):
    temporary = path + ".tmp"
    document.save(temporary)
    normalize_package(temporary, path)
    os.remove(temporary)


# --------------------------------------------------------------------------
# PDF fixture
# --------------------------------------------------------------------------


def _reportlab_inline(runs):
    parts = []
    for text, flags in runs:
        text = saxutils.escape(text)
        if flags.get("mono"):
            text = '<font face="Courier">%s</font>' % text
        if flags.get("bold"):
            text = "<b>%s</b>" % text
        if flags.get("italic"):
            text = "<i>%s</i>" % text
        if flags.get("underline"):
            text = "<u>%s</u>" % text
        if flags.get("link"):
            text = '<link href="%s">%s</link>' % (flags["link"], text)
        parts.append(text)
    return "".join(parts)


def build_reference_pdf(path):
    import reportlab.rl_config as rl_config
    from reportlab.lib.pagesizes import A4
    from reportlab.lib.styles import ParagraphStyle
    from reportlab.lib.units import cm
    from reportlab.platypus import (
        BaseDocTemplate,
        Frame,
        PageBreak,
        PageTemplate,
        Paragraph,
        Spacer,
        Table,
        TableStyle,
    )

    # Neutralise SOURCE_DATE_EPOCH: reportlab honours it even in invariant
    # mode, which would make the file depend on the environment.
    saved_epoch = os.environ.pop("SOURCE_DATE_EPOCH", None)
    saved_invariant = rl_config.invariant
    try:
        rl_config.invariant = 1

        title_style = ParagraphStyle(
            "FixtureTitle", fontName="Helvetica-Bold", fontSize=18, leading=22, spaceAfter=12
        )
        heading_one = ParagraphStyle(
            "FixtureHeading1",
            fontName="Helvetica-Bold",
            fontSize=14,
            leading=17,
            spaceBefore=6,
            spaceAfter=8,
        )
        heading_two = ParagraphStyle(
            "FixtureHeading2",
            fontName="Helvetica-Bold",
            fontSize=12,
            leading=15,
            spaceBefore=4,
            spaceAfter=8,
        )
        body = ParagraphStyle("FixtureBody", fontName="Helvetica", fontSize=11, leading=14, spaceAfter=8)
        bullet = ParagraphStyle(
            "FixtureBullet",
            parent=body,
            leftIndent=18,
            bulletIndent=6,
            spaceAfter=2,
        )
        bullet_nested = ParagraphStyle(
            "FixtureBulletNested",
            parent=body,
            leftIndent=36,
            bulletIndent=24,
            spaceAfter=2,
        )
        numbered = ParagraphStyle(
            "FixtureNumbered", parent=body, leftIndent=18, bulletIndent=6, spaceAfter=2
        )

        def footer(canvas, doc):
            canvas.saveState()
            canvas.setFont("Helvetica", 9)
            canvas.drawCentredString(A4[0] / 2.0, 1.2 * cm, "Side %d" % canvas.getPageNumber())
            canvas.restoreState()

        frame = Frame(
            2 * cm,
            2 * cm,
            A4[0] - 4 * cm,
            A4[1] - 4 * cm,
            leftPadding=0,
            rightPadding=0,
            topPadding=0,
            bottomPadding=0,
        )
        document = BaseDocTemplate(
            path,
            pagesize=A4,
            leftMargin=2 * cm,
            rightMargin=2 * cm,
            topMargin=2 * cm,
            bottomMargin=2 * cm,
            title=None,
            author=None,
            invariant=1,
            pageCompression=0,
        )
        document.addPageTemplates([PageTemplate(id="fixture", frames=[frame], onPage=footer)])

        story = [
            Paragraph(TITLE_TEXT, title_style),
            Paragraph(HEADING_ONE, heading_one),
            Paragraph(_reportlab_inline(INTRO_RUNS), body),
            Paragraph(HEADING_TWO_LISTS, heading_two),
        ]
        story.append(Paragraph(BULLET_ITEMS[0], bullet, bulletText="\u2022"))
        story.append(Paragraph(BULLET_ITEMS[1], bullet, bulletText="\u2022"))
        story.append(Paragraph(NESTED_ITEM_TEXT, bullet_nested, bulletText="o"))
        story.append(Paragraph(BULLET_ITEMS[2], bullet, bulletText="\u2022"))
        for index, item in enumerate(ORDERED_ITEMS, start=1):
            story.append(Paragraph(item, numbered, bulletText="%d." % index))

        story.append(PageBreak())
        story.append(Paragraph(HEADING_TWO_TABLE, heading_two))
        table = Table(
            TABLE_DATA,
            colWidths=[160, 90, A4[0] - 4 * cm - 250],
            hAlign="LEFT",
        )
        table.setStyle(
            TableStyle(
                [
                    ("GRID", (0, 0), (-1, -1), 0.5, (0, 0, 0)),
                    ("FONTNAME", (0, 0), (-1, 0), "Helvetica-Bold"),
                    ("FONTNAME", (0, 1), (-1, -1), "Helvetica"),
                    ("FONTSIZE", (0, 0), (-1, -1), 11),
                    ("LEFTPADDING", (0, 0), (-1, -1), 6),
                    ("RIGHTPADDING", (0, 0), (-1, -1), 6),
                    ("TOPPADDING", (0, 0), (-1, -1), 3),
                    ("BOTTOMPADDING", (0, 0), (-1, -1), 3),
                ]
            )
        )
        story.append(table)
        story.append(Spacer(1, 12))
        story.append(
            Paragraph(
                _reportlab_inline(
                    [(LINK_PREFIX, {}), (LINK_TEXT, {"link": LINK_URL}), (LINK_SUFFIX, {})]
                ),
                body,
            )
        )

        document.build(story)
    finally:
        rl_config.invariant = saved_invariant
        if saved_epoch is not None:
            os.environ["SOURCE_DATE_EPOCH"] = saved_epoch
    return path


# --------------------------------------------------------------------------
# Goldens
# --------------------------------------------------------------------------


def write_json(path, payload):
    text = json.dumps(payload, indent=2, ensure_ascii=False) + "\n"
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    return path


def build_large_summary(large_docx_path):
    model = dump_docx.dump_docx(large_docx_path)
    blocks = model["blocks"]
    paragraph_kinds = {"title", "heading", "paragraph", "list_item"}
    paragraphs = [block for block in blocks if block["kind"] in paragraph_kinds]
    headings = [block for block in blocks if block["kind"] == "heading"]
    return {
        "paragraph_count": len(paragraphs),
        "heading_count": len(headings),
        "first_paragraph_text": paragraphs[0]["text"],
        "last_paragraph_text": paragraphs[-1]["text"],
        "estimated_page_count": int(round(estimate_large_pages())),
        "page_estimate_model": (
            "A4 text height with 2 cm margins, Cambria line metrics, body 11 pt with "
            "1.15 line spacing and 10 pt space after, Heading 1 14 pt with 24 pt space before"
        ),
    }


def generate(fixtures_dir, golden_dir):
    os.makedirs(fixtures_dir, exist_ok=True)
    os.makedirs(golden_dir, exist_ok=True)

    reference_docx = os.path.join(fixtures_dir, "reference.docx")
    reference_pdf = os.path.join(fixtures_dir, "reference.pdf")
    large_docx = os.path.join(fixtures_dir, "large.docx")

    build_reference_docx(reference_docx)
    build_reference_pdf(reference_pdf)
    build_large_docx(large_docx)

    estimate = estimate_large_pages()
    if estimate < 205:
        raise RuntimeError(
            "large.docx is only about %d Word pages; the fixture must exceed 200" % estimate
        )

    written = [
        reference_docx,
        reference_pdf,
        large_docx,
        write_json(
            os.path.join(golden_dir, "reference_docx.json"),
            dump_docx.dump_docx(reference_docx),
        ),
        write_json(
            os.path.join(golden_dir, "reference_pdf.json"),
            dump_pdf.dump_pdf(reference_pdf),
        ),
        write_json(
            os.path.join(golden_dir, "large_docx.json"),
            build_large_summary(large_docx),
        ),
    ]

    markdown_path = os.path.join(golden_dir, "reference.md")
    with open(markdown_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(docx_to_md.docx_to_md(reference_docx))
    written.append(markdown_path)

    return written


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--fixtures-dir", default=DEFAULT_FIXTURES_DIR)
    parser.add_argument("--golden-dir", default=DEFAULT_GOLDEN_DIR)
    args = parser.parse_args(argv)

    for path in generate(args.fixtures_dir, args.golden_dir):
        with open(path, "rb") as handle:
            digest = __import__("hashlib").sha256(handle.read()).hexdigest()
        print("%s  %s" % (digest, os.path.relpath(path, REPO_ROOT)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
