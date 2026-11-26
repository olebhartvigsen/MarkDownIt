// The WinHTTP transport for the one analytics request the app makes.
//
// One POST, no retry, two second timeouts, and the answer reduced to "was
// it 2xx". Everything that could fail is a return value, not an exception:
// the caller runs on a detached thread at startup and a network problem
// must never reach the reader (PRIVACY.md, src/analytics.h).
//
// The payload is built by shared code and handed in; this file only knows
// how to put bytes on the wire on Windows.

#include "windows/telemetry_win.h"

// The seam first, before <windows.h>. windows.h defines DrawText as a
// macro, and a seam parsed with that macro in place declares DrawTextW
// instead of DrawText, which makes the renderer's target abstract
// (AGENTS.md, "Build gotcha"). This translation unit never includes a
// renderer header, so the order is all the protection it needs.
#include "platform/seams.h"

#include <windows.h>
#include <winhttp.h>
#include <winver.h>

#include <cstdio>
#include <string>
#include <vector>

#include "crash_trace.h"

namespace platform {
namespace {

// UTF-8 to UTF-16. The payload fields are short and come from the OS, so a
// plain conversion is enough; a failure yields the empty string and the
// caller reports it rather than posting a mangled request.
std::wstring Widen(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                           static_cast<int>(text.size()),
                                           nullptr, 0);
    if (length <= 0) return std::wstring();
    std::wstring out(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        &out[0], length);
    return out;
}

std::string Narrow(const wchar_t* text) {
    if (text == nullptr || *text == L'\0') return std::string();
    const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0,
                                           nullptr, nullptr);
    if (length <= 1) return std::string();
    // The length includes the terminator; the string drops it.
    std::string out(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, &out[0], length, nullptr,
                        nullptr);
    out.resize(static_cast<size_t>(length - 1));
    return out;
}

// One WinHTTP handle, closed on every path out of Post.
class InternetHandle {
public:
    InternetHandle() = default;
    explicit InternetHandle(HINTERNET handle) : handle_(handle) {}
    ~InternetHandle() {
        if (handle_ != nullptr) WinHttpCloseHandle(handle_);
    }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;

    HINTERNET get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

private:
    HINTERNET handle_ = nullptr;
};

// "https://host/path" into the host and the path WinHTTP wants. A URL that
// is not https is refused here: the payload never travels in the clear.
bool SplitHttpsUrl(const std::string& url, std::wstring* host,
                   std::wstring* path) {
    const std::string prefix = "https://";
    if (url.compare(0, prefix.size(), prefix) != 0) return false;
    const std::string rest = url.substr(prefix.size());
    const size_t slash = rest.find('/');
    const std::string hostPart =
        slash == std::string::npos ? rest : rest.substr(0, slash);
    const std::string pathPart =
        slash == std::string::npos ? std::string("/") : rest.substr(slash);
    if (hostPart.empty()) return false;
    *host = Widen(hostPart);
    *path = Widen(pathPart);
    return !host->empty() && !path->empty();
}

// The one transport. Stateless, so one object serves every call.
class WinHttpTelemetrySink final : public TelemetrySink {
public:
    bool Post(const std::string& url, const std::string& appKey,
              const std::string& body) override {
        std::wstring host;
        std::wstring path;
        if (!SplitHttpsUrl(url, &host, &path)) {
            diag::TraceFmt("analytics: refused a non https url");
            return false;
        }

        const InternetHandle session(WinHttpOpen(
            L"MarkDownIt", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        if (!session) {
            diag::TraceFmt("analytics: WinHttpOpen failed (%lu)",
                           static_cast<unsigned long>(GetLastError()));
            return false;
        }
        // Resolve, connect, send and receive, two seconds each. The caller
        // runs off the startup path, but a dead network must still not keep
        // the thread alive for long.
        WinHttpSetTimeouts(session.get(), 2000, 2000, 2000, 2000);

        const InternetHandle connect(WinHttpConnect(
            session.get(), host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
        if (!connect) {
            diag::TraceFmt("analytics: WinHttpConnect failed (%lu)",
                           static_cast<unsigned long>(GetLastError()));
            return false;
        }

        const InternetHandle request(WinHttpOpenRequest(
            connect.get(), L"POST", path.c_str(), nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
        if (!request) {
            diag::TraceFmt("analytics: WinHttpOpenRequest failed (%lu)",
                           static_cast<unsigned long>(GetLastError()));
            return false;
        }

        const std::wstring headers =
            L"Content-Type: application/json\r\nApp-Key: " + Widen(appKey) +
            L"\r\n";
        const DWORD bodyLength = static_cast<DWORD>(body.size());
        if (!WinHttpSendRequest(request.get(), headers.c_str(),
                                static_cast<DWORD>(-1L),
                                const_cast<char*>(body.data()), bodyLength,
                                bodyLength, 0)) {
            diag::TraceFmt("analytics: WinHttpSendRequest failed (%lu)",
                           static_cast<unsigned long>(GetLastError()));
            return false;
        }
        if (!WinHttpReceiveResponse(request.get(), nullptr)) {
            diag::TraceFmt("analytics: WinHttpReceiveResponse failed (%lu)",
                           static_cast<unsigned long>(GetLastError()));
            return false;
        }

        DWORD status = 0;
        DWORD statusSize = static_cast<DWORD>(sizeof(status));
        if (!WinHttpQueryHeaders(request.get(),
                                 WINHTTP_QUERY_STATUS_CODE |
                                     WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status,
                                 &statusSize, WINHTTP_NO_HEADER_INDEX)) {
            diag::TraceFmt("analytics: WinHttpQueryHeaders failed (%lu)",
                           static_cast<unsigned long>(GetLastError()));
            return false;
        }
        const bool accepted = status >= 200 && status < 300;
        if (!accepted) {
            // The answer is the only diagnostic there is: Aptabase says 4xx
            // for a payload it rejects, and the app keeps working either way.
            diag::TraceFmt("analytics: app_start answered %lu",
                           static_cast<unsigned long>(status));
        }
        return accepted;
    }
};

// RtlGetVersion is not in a public import library, so it is resolved from
// ntdll at run time. The struct is declared here rather than pulled from
// <winternl.h>, which keeps the header count down and matches the layout the
// function documents.
struct OsVersionInfo {
    ULONG size;
    ULONG major;
    ULONG minor;
    ULONG build;
    ULONG platformId;
    wchar_t servicePack[128];
};
using RtlGetVersionFn = LONG(WINAPI*)(OsVersionInfo*);

std::string FormatVersion(unsigned long major, unsigned long minor,
                          unsigned long build) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%lu.%lu.%lu", major, minor, build);
    return std::string(buf);
}

}  // namespace

std::unique_ptr<TelemetrySink> CreateTelemetrySink() {
    return std::unique_ptr<TelemetrySink>(new WinHttpTelemetrySink());
}

std::string WindowsOsVersion() {
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll != nullptr) {
        const auto fn = reinterpret_cast<RtlGetVersionFn>(
            GetProcAddress(ntdll, "RtlGetVersion"));
        if (fn != nullptr) {
            OsVersionInfo info = {};
            info.size = static_cast<ULONG>(sizeof(info));
            if (fn(&info) == 0) {
                return FormatVersion(info.major, info.minor, info.build);
            }
        }
    }
    return "0.0.0";
}

std::string WindowsLocale() {
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    const int length = GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH);
    if (length <= 1) return std::string();
    return Narrow(name);
}

std::string WindowsAppVersion() {
    wchar_t path[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) return "0.0.0";

    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path, &ignored);
    if (size == 0) return "0.0.0";
    std::vector<unsigned char> data(size);
    if (!GetFileVersionInfoW(path, 0, size, data.data())) return "0.0.0";

    VS_FIXEDFILEINFO* info = nullptr;
    UINT infoLength = 0;
    if (!VerQueryValueW(data.data(), L"\\",
                        reinterpret_cast<void**>(&info), &infoLength) ||
        info == nullptr || infoLength < sizeof(VS_FIXEDFILEINFO)) {
        return "0.0.0";
    }
    // FILEVERSION 0,2,1,0 arrives as two 32-bit halves, high word first.
    return FormatVersion(HIWORD(info->dwFileVersionMS),
                         LOWORD(info->dwFileVersionMS),
                         HIWORD(info->dwFileVersionLS));
}

analytics::SystemProps CurrentSystemProps() {
    analytics::SystemProps props;
    props.osName = "Windows";
    props.osVersion = WindowsOsVersion();
    props.appVersion = WindowsAppVersion();
    props.locale = WindowsLocale();
    props.debug = analytics::IsDebugBuild();
    return props;
}

}  // namespace platform
