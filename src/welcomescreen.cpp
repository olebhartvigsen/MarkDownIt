#include "welcomescreen.h"
#include <d2d1.h>
#include <dwrite.h>
#include <algorithm>

// Card layout constants (DIPs)
const float WelcomeScreen::kCardW   = 280.0f;
const float WelcomeScreen::kCardH   = 180.0f;
const float WelcomeScreen::kCardGap = 24.0f;
const float WelcomeScreen::kTopMargin = 80.0f;
const float WelcomeScreen::kSideMargin = 60.0f;

WelcomeScreen::WelcomeScreen() {}
WelcomeScreen::~WelcomeScreen() {
    if (title_fmt_) title_fmt_->Release();
    if (cardTitle_fmt_) cardTitle_fmt_->Release();
    if (cardPreview_fmt_) cardPreview_fmt_->Release();
    if (cardFolder_fmt_) cardFolder_fmt_->Release();
}

void WelcomeScreen::Init(IDWriteFactory* dw) {
    if (dw_) return;
    dw_ = dw;

    // Heading: "Recent Documents" — 24pt semi-bold
    dw->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 24.0f, L"en-US", &title_fmt_);

    // Card title: filename — 14pt semi-bold
    dw->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"en-US", &cardTitle_fmt_);

    // Card preview: 12pt regular
    dw->CreateTextFormat(L"Consolas", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"en-US", &cardPreview_fmt_);

    // Card folder path: 11pt regular, lighter
    dw->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"en-US", &cardFolder_fmt_);
}

void WelcomeScreen::SetRecentFiles(const std::vector<RecentFile>& files) {
    cards_.clear();
    for (const auto& rf : files) {
        if (rf.path.empty()) continue;
        WelcomeCard c;
        c.filePath = rf.path;
        c.preview = rf.preview;
        // Extract filename
        size_t slash = rf.path.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            c.fileName = rf.path.substr(slash + 1);
            // Folder: parent directory name
            size_t prevSlash = rf.path.find_last_of(L"\\/", slash - 1);
            if (prevSlash != std::wstring::npos) {
                c.folder = rf.path.substr(prevSlash + 1, slash - prevSlash - 1);
            } else {
                c.folder = rf.path.substr(0, slash);
            }
        } else {
            c.fileName = rf.path;
            c.folder = L"";
        }
        cards_.push_back(c);
    }
    if (cards_.size() > kMaxCards) cards_.resize(kMaxCards);
}

void WelcomeScreen::Layout(float viewW, float viewH) {
    if (cards_.empty()) return;

    // Calculate number of columns based on viewport width.
    float availW = viewW - 2.0f * kSideMargin;
    if (availW < kCardW) availW = kCardW;
    int cols = static_cast<int>(availW / (kCardW + kCardGap));
    if (cols < 1) cols = 1;

    float totalGridW = static_cast<float>(cols) * kCardW +
        static_cast<float>(cols - 1) * kCardGap;
    float startX = (viewW - totalGridW) * 0.5f;
    if (startX < kSideMargin) startX = kSideMargin;

    for (size_t i = 0; i < cards_.size(); ++i) {
        int col = static_cast<int>(i) % cols;
        int row = static_cast<int>(i) / cols;
        cards_[i].x = startX + static_cast<float>(col) * (kCardW + kCardGap);
        cards_[i].y = kTopMargin + static_cast<float>(row) * (kCardH + kCardGap);
        cards_[i].w = kCardW;
        cards_[i].h = kCardH;
    }
}

static void DrawRoundedRect(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
    float x, float y, float w, float h, float radius, float strokeWidth) {
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
        D2D1::RectF(x, y, x + w, y + h), radius, radius);
    rt->DrawRoundedRectangle(rr, brush, strokeWidth);
}

static void FillRoundedRect(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
    float x, float y, float w, float h, float radius) {
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
        D2D1::RectF(x, y, x + w, y + h), radius, radius);
    rt->FillRoundedRectangle(rr, brush);
}

void WelcomeScreen::Render(ID2D1RenderTarget* rt, float viewW, float viewH,
    int hoverIndex) {
    if (cards_.empty() || !rt || !dw_) return;

    // ── Colors (light theme, clean and modern) ──
    D2D1_COLOR_F bgColor     = D2D1::ColorF(0xF7F7F8);
    D2D1_COLOR_F cardBgColor  = D2D1::ColorF(0xFFFFFF);
    D2D1_COLOR_F cardBorder   = D2D1::ColorF(0xE0E0E4);
    D2D1_COLOR_F hoverBorder  = D2D1::ColorF(0x0078D4);
    D2D1_COLOR_F hoverBg      = D2D1::ColorF(0xF0F7FF);
    D2D1_COLOR_F titleColor   = D2D1::ColorF(0x1F1F20);
    D2D1_COLOR_F previewColor = D2D1::ColorF(0x616161);
    D2D1_COLOR_F folderColor  = D2D1::ColorF(0x8A8A8A);
    D2D1_COLOR_F accentColor  = D2D1::ColorF(0x0078D4);

    // Brushes
    ID2D1SolidColorBrush* bgBrush = nullptr;
    rt->CreateSolidColorBrush(bgColor, &bgBrush);
    ID2D1SolidColorBrush* cardBgBrush = nullptr;
    rt->CreateSolidColorBrush(cardBgColor, &cardBgBrush);
    ID2D1SolidColorBrush* borderBrush = nullptr;
    rt->CreateSolidColorBrush(cardBorder, &borderBrush);
    ID2D1SolidColorBrush* hoverBorderBrush = nullptr;
    rt->CreateSolidColorBrush(hoverBorder, &hoverBorderBrush);
    ID2D1SolidColorBrush* hoverBgBrush = nullptr;
    rt->CreateSolidColorBrush(hoverBg, &hoverBgBrush);
    ID2D1SolidColorBrush* titleBrush = nullptr;
    rt->CreateSolidColorBrush(titleColor, &titleBrush);
    ID2D1SolidColorBrush* previewBrush = nullptr;
    rt->CreateSolidColorBrush(previewColor, &previewBrush);
    ID2D1SolidColorBrush* folderBrush = nullptr;
    rt->CreateSolidColorBrush(folderColor, &folderBrush);
    ID2D1SolidColorBrush* accentBrush = nullptr;
    rt->CreateSolidColorBrush(accentColor, &accentBrush);

    // Background fill
    rt->FillRectangle(D2D1::RectF(0, 0, viewW, viewH), bgBrush);

    // ── Heading: "Recent Documents" ──
    if (title_fmt_) {
        std::wstring heading = L"Recent Documents";
        IDWriteTextLayout* tl = nullptr;
        dw_->CreateTextLayout(heading.c_str(),
            static_cast<UINT32>(heading.size()),
            title_fmt_, viewW, 40.0f, &tl);
        if (tl) {
            // Center horizontally
            DWRITE_TEXT_METRICS tm = {};
            tl->GetMetrics(&tm);
            float hx = (viewW - tm.width) * 0.5f;
            rt->DrawTextLayout(D2D1::Point2F(hx, 28.0f), tl, titleBrush);
            tl->Release();
        }
    }

    // ── Cards ──
    for (int i = 0; i < static_cast<int>(cards_.size()); ++i) {
        const auto& c = cards_[i];
        bool hovered = (i == hoverIndex);
        float r = 8.0f;

        // Card background
        if (hovered) {
            FillRoundedRect(rt, hoverBgBrush, c.x, c.y, c.w, c.h, r);
            DrawRoundedRect(rt, hoverBorderBrush, c.x, c.y, c.w, c.h, r, 2.0f);
        } else {
            FillRoundedRect(rt, cardBgBrush, c.x, c.y, c.w, c.h, r);
            DrawRoundedRect(rt, borderBrush, c.x, c.y, c.w, c.h, r, 1.0f);
        }

        // Left accent bar
        float barW = 4.0f;
        D2D1_RECT_F barRect = D2D1::RectF(c.x, c.y + r, c.x + barW, c.y + c.h - r);
        rt->FillRectangle(barRect, accentBrush);

        // ── Filename (card title) ──
        float textX = c.x + barW + 12.0f;
        float textW = c.w - barW - 24.0f;
        float textY = c.y + 12.0f;

        if (cardTitle_fmt_) {
            IDWriteTextLayout* tl = nullptr;
            dw_->CreateTextLayout(c.fileName.c_str(),
                static_cast<UINT32>(c.fileName.size()),
                cardTitle_fmt_, textW, 20.0f, &tl);
            if (tl) {
                tl->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                tl->SetTrimming(
                    &(DWRITE_TRIMMING{DWRITE_TRIMMING_GRANULARITY_CHARACTER,
                        0, 0}), nullptr);
                rt->DrawTextLayout(D2D1::Point2F(textX, textY), tl, titleBrush);
                tl->Release();
            }
        }

        // ── Folder ──
        if (cardFolder_fmt_ && !c.folder.empty()) {
            textY += 22.0f;
            IDWriteTextLayout* tl = nullptr;
            dw_->CreateTextLayout(c.folder.c_str(),
                static_cast<UINT32>(c.folder.size()),
                cardFolder_fmt_, textW, 18.0f, &tl);
            if (tl) {
                tl->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                tl->SetTrimming(
                    &(DWRITE_TRIMMING{DWRITE_TRIMMING_GRANULARITY_CHARACTER,
                        0, 0}), nullptr);
                rt->DrawTextLayout(D2D1::Point2F(textX, textY), tl, folderBrush);
                tl->Release();
            }
        }

        // ── Preview: first few lines of markdown ──
        if (cardPreview_fmt_ && !c.preview.empty()) {
            // Convert preview to wide string, limit to first ~5 lines
            std::wstring preview16;
            int lineCount = 0;
            for (size_t k = 0; k < c.preview.size() && lineCount < 5; ++k) {
                unsigned char ch = static_cast<unsigned char>(c.preview[k]);
                if (ch == '\n') {
                    preview16 += L'\n';
                    lineCount++;
                    if (lineCount >= 5) break;
                } else if (ch < 0x80) {
                    if (ch == '\r' || ch == 0) continue;
                    preview16 += static_cast<wchar_t>(ch);
                }
                // Skip multi-byte UTF-8 for simplicity in preview
            }

            float pvY = c.y + 56.0f;
            float pvH = c.h - 56.0f - 12.0f;
            float pvW = c.w - barW - 24.0f;

            IDWriteTextLayout* tl = nullptr;
            dw_->CreateTextLayout(preview16.c_str(),
                static_cast<UINT32>(preview16.size()),
                cardPreview_fmt_, pvW, pvH, &tl);
            if (tl) {
                tl->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
                rt->DrawTextLayout(D2D1::Point2F(textX, pvY), tl, previewBrush);
                tl->Release();
            }
        }
    }

    // Cleanup
    if (bgBrush) bgBrush->Release();
    if (cardBgBrush) cardBgBrush->Release();
    if (borderBrush) borderBrush->Release();
    if (hoverBorderBrush) hoverBorderBrush->Release();
    if (hoverBgBrush) hoverBgBrush->Release();
    if (titleBrush) titleBrush->Release();
    if (previewBrush) previewBrush->Release();
    if (folderBrush) folderBrush->Release();
    if (accentBrush) accentBrush->Release();
}

int WelcomeScreen::HitTest(float x, float y) const {
    for (int i = 0; i < static_cast<int>(cards_.size()); ++i) {
        const auto& c = cards_[i];
        if (x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + c.h) {
            return i;
        }
    }
    return -1;
}

const std::wstring& WelcomeScreen::GetPath(int index) const {
    static std::wstring empty;
    if (index < 0 || index >= static_cast<int>(cards_.size())) return empty;
    return cards_[index].filePath;
}
