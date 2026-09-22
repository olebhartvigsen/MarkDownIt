#ifndef MARKDOWNIT_VENDORED_ZLIB_H
#define MARKDOWNIT_VENDORED_ZLIB_H

// Routes pdfio's unconditional #include <zlib.h> to the vendored miniz
// zlib-compatible API in this directory (pdfio-private.h includes zlib.h).
#include "miniz.h"

#endif  // MARKDOWNIT_VENDORED_ZLIB_H
