#include "imagehelper.h"

#include <windows.h>
#include <wincodec.h>
#include <wincodecsdk.h>
#include <d2d1.h>
#include <string>
#include <cstring>

// Convert UTF-32 (DOM) to UTF-16 (WCHAR).
static std::u16string U32ToU16(const std::u32string& s32) {
    std::u16string out;
    out.reserve(s32.size());
    for (char32_t cp : s32) {
        if (cp <= 0xFFFF) {
            out.push_back(static_cast<char16_t>(cp));
        } else {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        }
    }
    return out;
}

ID2D1Bitmap* ImageHelper::LoadBitmapFromFile(ID2D1RenderTarget* rt,
    const std::u32string& url32, float maxWidth) {
    if (!rt || url32.empty()) return nullptr;

    std::u16string u16 = U32ToU16(url32);
    std::wstring path(u16.begin(), u16.end());

    IWICImagingFactory* wicFactory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));
    if (FAILED(hr) || !wicFactory) return nullptr;

    IWICBitmapDecoder* decoder = nullptr;
    hr = wicFactory->CreateDecoderFromFilename(path.c_str(), nullptr,
        GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr) || !decoder) {
        wicFactory->Release();
        return nullptr;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) {
        decoder->Release();
        wicFactory->Release();
        return nullptr;
    }

    // Convert to 32bpp PBGRA.
    IWICFormatConverter* converter = nullptr;
    hr = wicFactory->CreateFormatConverter(&converter);
    if (FAILED(hr) || !converter) {
        frame->Release();
        decoder->Release();
        wicFactory->Release();
        return nullptr;
    }

    hr = converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        converter->Release();
        frame->Release();
        decoder->Release();
        wicFactory->Release();
        return nullptr;
    }

    ID2D1Bitmap* bitmap = nullptr;
    hr = rt->CreateBitmapFromWicBitmap(converter, nullptr, &bitmap);

    converter->Release();
    frame->Release();
    decoder->Release();
    wicFactory->Release();

    return bitmap;
}
