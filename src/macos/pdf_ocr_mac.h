// The OCR seam for the PDF import on macOS: the page is rasterized with
// Core Graphics and read with Vision, Danish among the languages. The
// shared importer stays portable and calls this through a function
// pointer.
#pragma once

#include <cstddef>
#include <string>

namespace mac {

// Recognizes the text of one page (0 based). An empty result means the
// recognizer found nothing or is unavailable.
std::string RecognizePdfPage(const std::string& path, std::size_t page_index);

}  // namespace mac
