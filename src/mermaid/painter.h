#pragma once

// The mermaid diagram painter, shared by both ports.
//
// It paints the shared layout (mermaid::MermaidRender) through the
// RenderTarget and TextEngine seams, so the two platforms draw the same
// diagrams with the same code. There is no platform type in here, and
// nothing in here knows which platform it is drawing on.

#include "mermaid/layout_cache.h"
#include "platform/seams.h"
#include "theme.h"

namespace platform {

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
//
// `labelLineBoxEm` is the flowchart's label line box, as a multiple of
// the label size. Both ports pass their metrics' body line height, so a
// flowchart at the page zoom lays its labels out in the box the body
// text wears; zero keeps the font's natural box.
void PaintMermaidDiagram(TextEngine* engine,
                          RenderTarget* target,
                          const mermaid::MermaidRender& diagram,
                          float zoom,
                          float availWidth,
                          float originX,
                          float originY,
                          float padding,
                          const Palette& palette,
                          const FontSpec& bodyFont,
                          float labelLineBoxEm = 0.0f);

}  // namespace platform
