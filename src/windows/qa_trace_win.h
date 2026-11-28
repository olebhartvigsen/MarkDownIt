#pragma once

// The QA log every capture run reads.
//
// One line, appended and flushed, to the path in MARKDOWNIT_QA_LOG. An
// unset or empty path writes nothing, so an ordinary launch never touches
// a file. The Windows shell and the shared frame both write through this,
// so a state that quietly did nothing and one that ran look different in
// the log instead of only in a picture.

#include <string>

namespace platform {

void QaTraceLine(const std::string& line);

}  // namespace platform
