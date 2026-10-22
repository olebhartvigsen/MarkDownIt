#pragma once

// The mermaid diagram painter for the macOS port.
//
// The Windows build paints the mermaid diagrams with Direct2D. This one
// paints the same geometry through the RenderTarget seam, so the two
// platforms draw the same diagrams. The layout is the shared one
// (mermaid::MermaidRender), so only the drawing calls differ.

#include "mermaid/layout_cache.h"
#include "seams.h"
#include "theme.h"

namespace mac {

// Paints a mermaid diagram.
//
// The engine measures the diagram text and the target draws it. The
// diagram is drawn at the given zoom, and scaled down when it is wider
// than the available width. The origin is the top left corner of the
// block, and the padding is the space the diagram keeps between the
// block edge and the diagram.
//
// The engine has to be one the renderer does not use for the document
// text: the text engine keeps only its last measurement, and the hit
// tester needs the one the layout pass made.
void PaintMermaidDiagram(TextEngine* engine,
                          RenderTarget* target,
                          const mermaid::MermaidRender& diagram,
                          float zoom,
                          float availWidth,
                          float originX,
                          float originY,
                          float padding,
                          const Palette& palette,
                          const FontSpec& bodyFont);

}  // namespace mac
