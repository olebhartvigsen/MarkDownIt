#pragma once

// Reading the disk's own description of a file, for the mac window's file
// watch. The Windows counterpart lives in src/filewatch.cpp; both fill the
// same watch::Stamp, because the policy that reads them is shared
// (filewatch_model.h). The window polls this and asks the policy what a
// difference means.

#include <string>

#include "filewatch_model.h"

// Bytes, and the modification time in nanoseconds. `valid` is false when the
// path cannot be read: deleted, unmounted, or not a regular file.
watch::Stamp CurrentFileStamp(const std::string& path);
