#pragma once
// Windows-only DirectWrite bridge. It uses void pointers in the declaration so
// portable layout tests do not need Windows SDK headers.
#include "layout.h"

namespace mermaid {
LaidOutFlowchart LayoutFlowchartWithDWrite(const Flowchart& flow,
                                           void* factory,
                                           void* format);
}  // namespace mermaid
