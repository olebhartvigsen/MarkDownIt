// PDF import: text-based PDFs become editable document models (office
// spec, PDF import). Portable C++17 with no Windows dependency; pdfio
// does the reading, and the caller supplies the OCR seam for scanned
// pages plus the password for protected files.
#pragma once

#include <cstddef>
#include <string>

#include "office_model.h"

namespace office {

// OCR seam: when a page carries no extractable text, the importer asks
// the caller for recognized text for that page. macOS fills this with
// Vision (Danish included); a null function means "no OCR here", and
// the import warns instead of failing.
using PdfPageOcr = std::string (*)(void* data, const std::string& path, std::size_t page_index);

struct PdfImportOptions {
    std::string password;      // used when the file is protected
    PdfPageOcr ocr = nullptr;  // recognized text for a scanned page
    void* ocr_data = nullptr;
};

// Imports the content of a PDF as a document model: the text, headings
// by relative size, lists, hyperlinks from link annotations, and a page
// break between source pages, with the page setup taken from page 1.
// Scanned pages are reported, and when ocr is set their recognized text
// is imported too. Everything the model cannot reproduce lands in the
// report.
bool PdfImport(const std::string& path, const PdfImportOptions& opt, DocModel& out,
               CompatReport& report, std::string& error);

}  // namespace office
