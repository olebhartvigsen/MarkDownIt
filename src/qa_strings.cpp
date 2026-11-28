// The QA strings of the document renderer.
//
// The reviewer harness reads one line for the pinned header band and one
// for the block list. They are built here, beside the state they read and
// named for the job, so the renderer keeps no reader-facing text and both
// runners format the same numbers the same way.

#include <cstdio>
#include <string>

#include "document_renderer.h"

namespace platform {

std::string DocumentRenderer::PinnedProbeForQA() const {
    char line[224];
    std::snprintf(line, sizeof(line),
                  "paints=%d viewport=%.0f block=%.0f block-height=%.0f "
                  "header-height=%.0f tables=%d pinned-now=%.0f editing=%d",
                  qaRenderCount_, (double)qaViewportTopSeen_,
                  (double)qaPinBlockY_, (double)qaPinBlockHeight_,
                  (double)qaPinHeaderHeight_, qaPinTableBlocks_,
                  (double)qaPinnedHeaderHeight_, editing_ ? 1 : 0);
    return std::string(line);
}

std::string DocumentRenderer::BlocksForQA() const {
    std::string out;
    char item[64];
    for (size_t i = 0; i < blocks_.size() && i < 8; ++i) {
        std::snprintf(item, sizeof(item), "%s%d:%.0f:%.0f", i ? " " : "",
                      static_cast<int>(blocks_[i].kind),
                      static_cast<double>(blocks_[i].y),
                      static_cast<double>(blocks_[i].height));
        out += item;
    }
    std::snprintf(item, sizeof(item), " n=%zu", blocks_.size());
    out += item;
    return out;
}

}  // namespace platform
