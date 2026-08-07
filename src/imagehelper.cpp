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

    if (!WinCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &uc))
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
    do {
        avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &avail)) break;
        if (avail > 0) {
            anyData = true;
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
