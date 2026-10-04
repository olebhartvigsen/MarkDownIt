#include "imagehelper.h"

#include <windows.h>
#include <wincodec.h>
#include <wincodecsdk.h>
#include <winhttp.h>
#include <d2d1.h>
#include <string>
#include <cstring>
#include <vector>

// Download an image from HTTP/HTTPS via WinHTTP. Returns the raw bytes.
static bool DownloadImage(const std::wstring& url, std::vector<BYTE>& out) {
    // Parse URL to get server, path, and port.
    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    wchar_t server[256] = {0};
    wchar_t path[1024] = {0};
    uc.lpszHostName = server;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 1024;

    std::wstring lower = url;
    for (auto& c : lower) c = towlower(c);
    bool https = (lower.find(L"https://") == 0);

    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &uc))
        return false;

    HINTERNET hSession = WinHttpOpen(L"MarkDownIt/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    INTERNET_PORT port = uc.nPort ? uc.nPort : (https ? 443 : 80);

    HINTERNET hConnect = WinHttpConnect(hSession, server, port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect,
        L"GET", path, nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        https ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // The defaults are 30 s each for connect, send and receive, and they
    // apply here on the paint thread: one unreachable image in a document
    // can wedge the window for a minute at a time. Bound each phase, and
    // keep a total deadline via the receive timeout.
    //
    // A redirect is no longer allowed to downgrade an https request to
    // plaintext http, which is the hop an on-path observer would use to
    // take over the image request. Same-scheme redirects are still
    // followed, so this is not a fix for redirect-based address
    // confusion; that needs the remote-image setting, below.
    WinHttpSetTimeouts(hRequest, 5000, 5000, 5000, 10000);
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY,
                     &redirectPolicy, sizeof(redirectPolicy));

    BOOL bResult = WinHttpSendRequest(hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!bResult || !WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD fileSize = 0;
    DWORD avail = 0;
    bool anyData = false;
    // Cap the download: a document can point at any URL, so refuse to
    // buffer unlimited bytes (memory exhaustion from one image tag).
    constexpr size_t kMaxImageBytes = 32u * 1024u * 1024u;
    do {
        avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &avail)) break;
        if (avail > 0) {
            anyData = true;
            if (out.size() + avail > kMaxImageBytes) {
                anyData = false;
                break;
            }
            size_t oldSize = out.size();
            out.resize(oldSize + avail);
            if (!WinHttpReadData(hRequest, out.data()+oldSize, avail, nullptr))
                break;
        }
    } while (avail > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return anyData;
}

// Create a D2D bitmap from raw image bytes via WIC.
static ID2D1Bitmap* BitmapFromBytes(ID2D1RenderTarget* rt,
    IWICImagingFactory* wicFactory, const BYTE* data, DWORD size) {
    if (!rt || !wicFactory || !data || size == 0) return nullptr;

    IWICStream* stream = nullptr;
    HRESULT hr = wicFactory->CreateStream(&stream);
    if (FAILED(hr) || !stream) return nullptr;

    hr = stream->InitializeFromMemory(const_cast<BYTE*>(data), size);
    if (FAILED(hr)) { stream->Release(); return nullptr; }

    IWICBitmapDecoder* decoder = nullptr;
    hr = wicFactory->CreateDecoderFromStream(stream, nullptr,
        WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr) || !decoder) { stream->Release(); return nullptr; }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) {
        decoder->Release(); stream->Release(); return nullptr;
    }

    IWICFormatConverter* converter = nullptr;
    hr = wicFactory->CreateFormatConverter(&converter);
    if (FAILED(hr) || !converter) {
        frame->Release(); decoder->Release(); stream->Release();
        return nullptr;
    }

    hr = converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        converter->Release(); frame->Release(); decoder->Release();
        stream->Release(); return nullptr;
    }

    ID2D1Bitmap* bitmap = nullptr;
    hr = rt->CreateBitmapFromWicBitmap(converter, nullptr, &bitmap);

    converter->Release(); frame->Release(); decoder->Release();
    stream->Release();
    return bitmap;
}

ID2D1Bitmap* ImageHelper::LoadBitmapFromUrl(ID2D1RenderTarget* rt,
    const std::string& url8, float maxWidth) {
    if (!rt || url8.empty()) return nullptr;

    // Convert UTF-8 to UTF-16 for Win32 APIs.
    if (url8.size() > 4096) return nullptr;
    std::wstring url;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, url8.c_str(),
        static_cast<int>(url8.size()), nullptr, 0);
    if (wlen > 0) {
        url.resize(wlen);
        MultiByteToWideChar(CP_UTF8, 0, url8.c_str(),
            static_cast<int>(url8.size()), &url[0], wlen);
    }

    // Check if it's an HTTP(S) URL.
    std::wstring lower = url;
    for (auto& c : lower) c = towlower(c);
    bool isHttp = (lower.find(L"http://") == 0 || lower.find(L"https://") == 0);

    IWICImagingFactory* wicFactory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));
    if (FAILED(hr) || !wicFactory) return nullptr;

    ID2D1Bitmap* bitmap = nullptr;

    if (isHttp) {
        // Download via WinHTTP, then decode from memory.
        std::vector<BYTE> imageData;
        if (DownloadImage(url, imageData) && !imageData.empty()) {
            bitmap = BitmapFromBytes(rt, wicFactory,
                imageData.data(), static_cast<DWORD>(imageData.size()));
        }
    } else {
        // Local file path.
        IWICBitmapDecoder* decoder = nullptr;
        hr = wicFactory->CreateDecoderFromFilename(url.c_str(), nullptr,
            GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
        if (SUCCEEDED(hr) && decoder) {
            IWICBitmapFrameDecode* frame = nullptr;
            hr = decoder->GetFrame(0, &frame);
            if (SUCCEEDED(hr) && frame) {
                IWICFormatConverter* converter = nullptr;
                hr = wicFactory->CreateFormatConverter(&converter);
                if (SUCCEEDED(hr) && converter) {
                    hr = converter->Initialize(frame,
                        GUID_WICPixelFormat32bppPBGRA,
                        WICBitmapDitherTypeNone, nullptr, 0.0,
                        WICBitmapPaletteTypeCustom);
                    if (SUCCEEDED(hr))
                        rt->CreateBitmapFromWicBitmap(converter, nullptr, &bitmap);
                    converter->Release();
                }
                frame->Release();
            }
            decoder->Release();
        }
    }

    wicFactory->Release();
    return bitmap;
}

std::string ImageHelper::LoadSvgText(const std::string& url8) {
    if (url8.empty()) return {};

    // Check if HTTP(S) URL.
    std::string lower = url8;
    for (auto& c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    bool isHttp = (lower.find("http://") == 0 || lower.find("https://") == 0);

    if (isHttp) {
        // Convert to wide for WinHTTP
        std::wstring url;
        int wlen = MultiByteToWideChar(CP_UTF8, 0, url8.c_str(),
            static_cast<int>(url8.size()), nullptr, 0);
        if (wlen > 0) {
            url.resize(wlen);
            MultiByteToWideChar(CP_UTF8, 0, url8.c_str(),
                static_cast<int>(url8.size()), &url[0], wlen);
        }
        std::vector<BYTE> data;
        if (!DownloadImage(url, data) || data.empty()) return {};
        return std::string(reinterpret_cast<const char*>(data.data()), data.size());
    }

    // Local file: also handle data:image/svg+xml;base64,...
    if (lower.find("data:image/svg+xml") == 0) {
        // Base64 decode after the comma
        size_t comma = url8.find(',');
        if (comma == std::string::npos) return {};
        std::string b64 = url8.substr(comma + 1);
        // Decode base64
        static const int8_t b64tab[256] = {
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
            52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
            -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
            15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
            -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
            41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        };
        std::string out;
        int val = 0, bits = 0;
        for (char ch : b64) {
            if (ch == '=' || ch == '\n' || ch == '\r' || ch == ' ') continue;
            int d = b64tab[static_cast<unsigned char>(ch)];
            if (d < 0) continue;
            val = (val << 6) | d;
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                out.push_back(static_cast<char>((val >> bits) & 0xFF));
            }
        }
        return out;
    }

    // Local file path: convert to wide and read.
    std::wstring path;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, url8.c_str(),
        static_cast<int>(url8.size()), nullptr, 0);
    if (wlen > 0) {
        path.resize(wlen);
        MultiByteToWideChar(CP_UTF8, 0, url8.c_str(),
            static_cast<int>(url8.size()), &path[0], wlen);
    }

    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return {};

    DWORD fileSize = GetFileSize(hFile, nullptr);
    if (fileSize == 0 || fileSize > 10 * 1024 * 1024) {
        CloseHandle(hFile);
        return {};
    }

    std::string content(fileSize, '\0');
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hFile, &content[0], fileSize, &bytesRead, nullptr);
    CloseHandle(hFile);
    if (!ok || bytesRead == 0) return {};
    content.resize(bytesRead);
    return content;
}
