#include "filewatch_mac.h"

#include <sys/stat.h>

watch::Stamp CurrentFileStamp(const std::string& path) {
    watch::Stamp stamp;
    if (path.empty()) return stamp;
    struct stat info = {};
    if (::stat(path.c_str(), &info) != 0) return stamp;
    // A directory is not a document. Without this a folder opened by
    // mistake would report every write inside it as a change to the file.
    if (!S_ISREG(info.st_mode)) return stamp;
    stamp.valid = true;
    stamp.size = static_cast<uint64_t>(info.st_size);
    stamp.modified =
        static_cast<int64_t>(info.st_mtimespec.tv_sec) * 1000000000LL +
        static_cast<int64_t>(info.st_mtimespec.tv_nsec);
    return stamp;
}
