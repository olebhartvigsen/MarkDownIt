// The macOS crash breadcrumbs.
//
// The Windows build writes a breadcrumb for every stage of a risky
// operation, so a silent crash can be read back from the log. The mac
// build does the same, into ~/Library/Logs/MarkDownIt/crash_trace.log.
//
// The breadcrumb API is shared, so the code that logs is the same on both
// platforms.

#include "crash_trace_mac.h"

#include <cstdio>
#include <ctime>
#include <string>

#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include "textenc.h"

namespace mac {
namespace {

// The log file, opened on first use.
FILE* g_traceFile = nullptr;
bool g_triedOpen = false;

std::string HomeDirectory() {
    const char* home = getenv("HOME");
    if (home && *home) return home;
    struct passwd* pw = getpwuid(getuid());
    if (pw && pw->pw_dir) return pw->pw_dir;
    return ".";
}

// Opens the log file, creating the directory if it has to.
FILE* TraceFile() {
    if (g_triedOpen) return g_traceFile;
    g_triedOpen = true;

    const std::string dir = HomeDirectory() +
                            "/Library/Logs/MarkDownIt";
    // The directory is created one level at a time, because mkdir does not
    // create the parents.
    const std::string library = HomeDirectory() + "/Library";
    mkdir(library.c_str(), 0755);
    mkdir((library + "/Logs").c_str(), 0755);
    mkdir(dir.c_str(), 0755);

    const std::string path = dir + "/crash_trace.log";
    g_traceFile = fopen(path.c_str(), "a");
    return g_traceFile;
}

}  // namespace

void TraceMac(const char* fmt, ...) {
    FILE* file = TraceFile();
    if (!file) return;

    // The timestamp, so the log can be read after the fact.
    time_t now = time(nullptr);
    struct tm local {};
    localtime_r(&now, &local);
    char stamp[32];
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
    fprintf(file, "[%s] ", stamp);

    va_list args;
    va_start(args, fmt);
    vfprintf(file, fmt, args);
    va_end(args);

    fputc('\n', file);
    fflush(file);
}

void TraceMacWide(const std::wstring& text) {
    TraceMac("%s", textenc::WideToUtf8(text).c_str());
}

}  // namespace mac
