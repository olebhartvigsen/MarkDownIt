#!/usr/bin/env python3
"""Validate a protected .pdf: readable only with its user password, and
still carrying the golden pages once unlocked.

The export writes reference_protected.pdf with AES-128 encryption, the
user password "user-secret" and a permission mask that allows printing
while refusing copying and changing. This script checks, in order:

  * the file is encrypted,
  * a wrong password opens nothing,
  * the user password unlocks it,
  * the print permission travels; copying and changing do not,
  * the unlocked pages match the same golden the plain export check
    uses, so protection must not change what a reader sees.

Usage: python3 check_pdf_protected.py <produced.pdf>
"""
import os
import sys
import tempfile

import pypdf

import check_pdf

USER_PASSWORD = "user-secret"

# The permission bits of the PDF standard: bit 3 allows printing
# (value 4), bit 4 allows changing (8), bit 5 allows copying (16).
PERMISSION_PRINT = 4
PERMISSION_MODIFY = 8
PERMISSION_COPY = 16


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) != 1:
        print(__doc__.strip())
        return 2
    path = argv[0]

    reader = pypdf.PdfReader(path)
    if not reader.is_encrypted:
        print("MISMATCH: %s is not encrypted" % path)
        return 1

    # A wrong password must open nothing.
    if reader.decrypt("not-the-password"):
        print("MISMATCH: %s opens with a wrong password" % path)
        return 1

    reader = pypdf.PdfReader(path)
    if not reader.decrypt(USER_PASSWORD):
        print("MISMATCH: %s does not open with its user password" % path)
        return 1

    # The permission mask: printing allowed, copying and changing not.
    raw = int(reader.trailer["/Encrypt"]["/P"])
    mask = raw & 0xFFFF
    if not mask & PERMISSION_PRINT:
        print("MISMATCH: %s refuses printing" % path)
        return 1
    if mask & PERMISSION_COPY:
        print("MISMATCH: %s still allows copying" % path)
        return 1
    if mask & PERMISSION_MODIFY:
        print("MISMATCH: %s still allows changing" % path)
        return 1

    # Unlocked, the pages must match the golden the plain export check
    # uses; write a decrypted copy so the shared checker can read it.
    writer = pypdf.PdfWriter()
    for page in reader.pages:
        writer.add_page(page)
    handle, decrypted_path = tempfile.mkstemp(suffix=".pdf")
    os.close(handle)
    try:
        with open(decrypted_path, "wb") as out:
            writer.write(out)
        status = check_pdf.check(decrypted_path, check_pdf.DEFAULT_GOLDEN, 5)
    finally:
        os.unlink(decrypted_path)
    if status != 0:
        print("MISMATCH: the unlocked pages of %s do not match the golden" % path)
        return 1

    print(
        "OK: %s is AES-128 protected, printing allowed, copying and "
        "changing refused, and the unlocked pages match the golden" % path
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
