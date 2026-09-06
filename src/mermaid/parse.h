#pragma once
#include "model.h"
#include <string_view>

namespace mermaid {

// Parse a mermaid flowchart source into a Flowchart.
// On success, `error` is empty. On failure (non-flowchart diagram, malformed header),
// `error` contains a short message and the caller should fall back to rendering the
// source as a plain code block.
Flowchart ParseFlowchart(std::string_view src);

}  // namespace mermaid
