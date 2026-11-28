// The QA trace writer on Windows: MARKDOWNIT_QA_LOG, appended and closed
// per line so a crash keeps what ran.

#include "windows/qa_trace_win.h"

#include <cstdio>
#include <cstdlib>

namespace platform {

void QaTraceLine(const std::string& line) {
    const char* path = std::getenv("MARKDOWNIT_QA_LOG");
    if (!path || !*path) return;
    if (FILE* log = std::fopen(path, "a")) {
        std::fprintf(log, "%s\n", line.c_str());
        std::fclose(log);
    }
}

}  // namespace platform
