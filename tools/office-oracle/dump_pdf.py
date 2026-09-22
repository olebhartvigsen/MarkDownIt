#!/usr/bin/env python3
"""Independent .pdf dump built on pypdf.

Reports the facts MarkDownIt's PDF work is measured against: how many pages
a PDF has, what size every page is, and what text each page carries.

Output model (JSON)

  format      always "markdownit-office-pdf-dump"
  version     model version, currently 1
  page_count  number of pages
  pages       one entry per page, in order:
                media_box_pt  {left, bottom, right, top, width, height}
                text          extracted text, verbatim

Usage:
    python3 dump_pdf.py <file.pdf> [out.json]
"""
import argparse
import json
import sys

from pypdf import PdfReader

MODEL_FORMAT = "markdownit-office-pdf-dump"
MODEL_VERSION = 1


def page_model(page):
    box = page.mediabox
    left = round(float(box.left), 2)
    bottom = round(float(box.bottom), 2)
    right = round(float(box.right), 2)
    top = round(float(box.top), 2)
    return {
        "media_box_pt": {
            "left": left,
            "bottom": bottom,
            "right": right,
            "top": top,
            "width": round(right - left, 2),
            "height": round(top - bottom, 2),
        },
        "text": page.extract_text() or "",
    }


def dump_pdf(path):
    reader = PdfReader(path)
    return {
        "format": MODEL_FORMAT,
        "version": MODEL_VERSION,
        "page_count": len(reader.pages),
        "pages": [page_model(page) for page in reader.pages],
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("pdf", help="input .pdf file")
    parser.add_argument("out", nargs="?", help="write JSON here instead of stdout")
    args = parser.parse_args(argv)

    payload = json.dumps(dump_pdf(args.pdf), indent=2, ensure_ascii=False) + "\n"
    if args.out:
        with open(args.out, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(payload)
    else:
        sys.stdout.write(payload)
    return 0


if __name__ == "__main__":
    sys.exit(main())
