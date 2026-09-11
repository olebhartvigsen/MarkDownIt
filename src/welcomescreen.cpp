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
    if (cardPreviewBold_fmt_) cardPreviewBold_fmt_->Release();
    if (cardPreviewHeading_fmt_) cardPreviewHeading_fmt_->Release();
    if (cardCode_fmt_) cardCode_fmt_->Release();
    if (cardFolder_fmt_) cardFolder_fmt_->Release();
}

void WelcomeScreen::Init(IDWriteFactory* dw) {
    if (dw_) return;
    dw_ = dw;

    // Heading: "Recent Documents"; 24pt semi-bold
    dw->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 24.0f, L"en-US", &title_fmt_);

    // Card title: filename; 14pt semi-bold
    dw->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"en-US", &cardTitle_fmt_);

    // Card preview: 12pt regular
    dw->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"en-US", &cardPreview_fmt_);

    // Card preview bold: 11pt bold
    dw->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"en-US", &cardPreviewBold_fmt_);

    // Card preview heading: 12pt semi-bold (for H1/H2 in preview)
    dw->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-US", &cardPreviewHeading_fmt_);

    // Card inline code: 11pt monospace
    dw->CreateTextFormat(L"Consolas", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"en-US", &cardCode_fmt_);

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
            // Folder: parent directory name. Guard the underflow when
            // the path starts with a separator (slash == 0).
            size_t prevSlash = (slash > 0)
                ? rf.path.find_last_of(L"\\/", slash - 1)
                : std::wstring::npos;
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
    if (!rt || !dw_) return;

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
    D2D1_COLOR_F hintColor    = D2D1::ColorF(0xA0A0A0);

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
    ID2D1SolidColorBrush* hintBrush = nullptr;
    rt->CreateSolidColorBrush(hintColor, &hintBrush);

    // Background fill
    rt->FillRectangle(D2D1::RectF(0, 0, viewW, viewH), bgBrush);

    // ── Heading ──
    if (title_fmt_) {
        std::wstring heading = L"Recent Documents";
        IDWriteTextLayout* tl = nullptr;
        dw_->CreateTextLayout(heading.c_str(),
            static_cast<UINT32>(heading.size()),
            title_fmt_, viewW, 40.0f, &tl);
        if (tl) {
            DWRITE_TEXT_METRICS tm = {};
            tl->GetMetrics(&tm);
            float hx = (viewW - tm.width) * 0.5f;
            rt->DrawTextLayout(D2D1::Point2F(hx, 28.0f), tl, titleBrush);
            tl->Release();
        }
    }

    // ── No recent files: show a friendly hint ──
    if (cards_.empty()) {
        if (cardPreview_fmt_) {
            std::wstring hint = L"Open or drag a .md file to get started.";
            IDWriteTextLayout* tl = nullptr;
            dw_->CreateTextLayout(hint.c_str(),
                static_cast<UINT32>(hint.size()),
                cardPreview_fmt_, viewW, 30.0f, &tl);
            if (tl) {
                DWRITE_TEXT_METRICS tm = {};
                tl->GetMetrics(&tm);
                float hx = (viewW - tm.width) * 0.5f;
                float hy = viewH * 0.5f - 15.0f;
                rt->DrawTextLayout(D2D1::Point2F(hx, hy), tl, hintBrush);
                tl->Release();
            }
        }
        // Cleanup and return early
        if (bgBrush) bgBrush->Release();
        if (cardBgBrush) cardBgBrush->Release();
        if (borderBrush) borderBrush->Release();
        if (hoverBorderBrush) hoverBorderBrush->Release();
        if (hoverBgBrush) hoverBgBrush->Release();
        if (titleBrush) titleBrush->Release();
        if (previewBrush) previewBrush->Release();
        if (folderBrush) folderBrush->Release();
        if (accentBrush) accentBrush->Release();
        if (hintBrush) hintBrush->Release();
        return;
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

        // ── Preview: first few lines rendered as markdown ──
        if (!c.preview.empty()) {
            std::wstring preview16;
            int lineCount = 0;
            for (size_t k = 0; k < c.preview.size() && lineCount < 6; ++k) {
                unsigned char ch = static_cast<unsigned char>(c.preview[k]);
                if (ch == '\n') {
                    preview16 += L'\n';
                    lineCount++;
                    if (lineCount >= 6) break;
                } else if (ch < 0x80) {
                    if (ch == '\r' || ch == 0) continue;
                    preview16 += static_cast<wchar_t>(ch);
                } else if (ch >= 0xC0 && ch < 0xE0 && k + 1 < c.preview.size()) {
                    // 2-byte sequence (accented Latin etc.)
                    unsigned char ch2 = static_cast<unsigned char>(c.preview[k + 1]);
                    preview16 += static_cast<wchar_t>(
                        ((ch & 0x1F) << 6) | (ch2 & 0x3F));
                    k++;
                } else if (ch >= 0xE0 && ch < 0xF0 && k + 2 < c.preview.size()) {
                    // 3-byte sequence (CJK, symbols)
                    unsigned char ch2 = static_cast<unsigned char>(c.preview[k + 1]);
                    unsigned char ch3 = static_cast<unsigned char>(c.preview[k + 2]);
                    preview16 += static_cast<wchar_t>(
                        ((ch & 0x0F) << 12) | ((ch2 & 0x3F) << 6) | (ch3 & 0x3F));
                    k += 2;
                } else if (ch >= 0xF0 && k + 3 < c.preview.size()) {
                    // 4-byte sequence (emoji/astral): UTF-16 surrogate pair.
                    unsigned char ch2 = static_cast<unsigned char>(c.preview[k + 1]);
                    unsigned char ch3 = static_cast<unsigned char>(c.preview[k + 2]);
                    unsigned char ch4 = static_cast<unsigned char>(c.preview[k + 3]);
                    uint32_t cp = ((ch & 0x07u) << 18) |
                        ((ch2 & 0x3Fu) << 12) |
                        ((ch3 & 0x3Fu) << 6) | (ch4 & 0x3Fu);
                    cp -= 0x10000;
                    preview16 += static_cast<wchar_t>(0xD800 + (cp >> 10));
                    preview16 += static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
                    k += 3;
                }
            }

            float pvY = c.y + 56.0f;
            float pvH = c.h - 56.0f - 12.0f;
            float pvW = c.w - barW - 24.0f;

            // Split into lines and render with inline formatting.
            std::vector<std::wstring> lines;
            std::wstring cur;
            for (wchar_t wc : preview16) {
                if (wc == L'\n') {
                    lines.push_back(cur);
                    cur.clear();
                } else {
                    cur += wc;
                }
            }
            if (!cur.empty()) lines.push_back(cur);

            D2D1_RECT_F clipRect = D2D1::RectF(
                textX, pvY, textX + pvW, pvY + pvH);
            // Clip to card bounds so text doesn't overflow.
            rt->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

            float curY = pvY;
            for (size_t li = 0; li < lines.size(); ++li) {
                if (curY >= pvY + pvH) break;

                const auto& line = lines[li];

                // ── Table rendering ──
                // A table row starts with '|'. A separator line looks like "|---|---|".
                // We collapse the separator and render header row in bold.
                bool isTableRow = (!line.empty() && line[0] == L'|');
                bool isTableSep = isTableRow;
                if (isTableSep) {
                    for (wchar_t c : line) {
                        if (c != L'|' && c != L'-' && c != L':' && c != L' ' && c != L'\t') {
                            isTableSep = false;
                            break;
                        }
                    }
                }

                if (isTableSep) {
                    // Skip the separator line entirely.
                    continue;
                }

                if (isTableRow) {
                    // Split the row by '|' and render as aligned columns.
                    std::vector<std::wstring> cells;
                    std::wstring cell;
                    for (size_t ci = 0; ci < line.size(); ++ci) {
                        if (line[ci] == L'|') {
                            // Trim whitespace from cell.
                            std::wstring trimmed;
                            size_t s = 0, e = cell.size();
                            while (s < e && (cell[s] == L' ' || cell[s] == L'\t')) s++;
                            while (e > s && (cell[e-1] == L' ' || cell[e-1] == L'\t')) e--;
                            trimmed = cell.substr(s, e - s);
                            if (!trimmed.empty() || ci < line.size() - 1)
                                cells.push_back(trimmed);
                            cell.clear();
                        } else {
                            cell += line[ci];
                        }
                    }

                    // The row is a header when the following preview
                    // line is a separator (dashes). Header rows render bold.
                    bool isHeader = false;
                    bool nextIsSep = false;
                    if (li + 1 < lines.size()) {
                        const auto& nl = lines[li + 1];
                        if (!nl.empty() && nl[0] == L'|') {
                            nextIsSep = true;
                            for (wchar_t c : nl) {
                                if (c != L'|' && c != L'-' && c != L':' && c != L' ' && c != L'\t') {
                                    nextIsSep = false;
                                    break;
                                }
                            }
                        }
                    }
                    isHeader = nextIsSep;

                    // Render the row: join cells with " | " separator.
                    std::wstring rowText;
                    for (size_t ci = 0; ci < cells.size(); ++ci) {
                        if (ci > 0) rowText += L"  \x2502  "; // │ box drawing char
                        rowText += cells[ci];
                    }

                    IDWriteTextFormat* fmt = isHeader ? cardPreviewBold_fmt_ : cardPreview_fmt_;
                    IDWriteTextLayout* tl = nullptr;
                    dw_->CreateTextLayout(rowText.c_str(),
                        static_cast<UINT32>(rowText.size()),
                        fmt, pvW, 20.0f, &tl);
                    if (tl) {
                        tl->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                        tl->SetTrimming(
                            &(DWRITE_TRIMMING{DWRITE_TRIMMING_GRANULARITY_CHARACTER,
                                0, 0}), nullptr);
                        DWRITE_TEXT_METRICS tm = {};
                        tl->GetMetrics(&tm);
                        rt->DrawTextLayout(D2D1::Point2F(textX, curY), tl,
                            isHeader ? titleBrush : previewBrush);
                        tl->Release();
                        curY += tm.height + 2.0f;
                    }
                    continue;
                }

                // Check for headings.
                bool isH1 = (line.size() >= 2 && line[0] == L'#' && line[1] == L' ' && line[2] != L'#');
                bool isH2 = (line.size() >= 3 && line[0] == L'#' && line[1] == L'#' && line[2] == L' ' && line[3] != L'#');
                bool isBullet = (line.size() >= 2 && line[0] == L'-' && line[1] == L' ');
                bool isNumbered = false;
                for (size_t di = 0; di < line.size() && di < 4; ++di) {
                    if (line[di] >= L'0' && line[di] <= L'9') {
                        isNumbered = (di + 1 < line.size() && line[di + 1] == L'.');
                    } else break;
                }

                // Determine text and format.
                std::wstring text = line;
                IDWriteTextFormat* fmt = cardPreview_fmt_;

                if (isH1) {
                    text = line.substr(2);
                    fmt = cardPreviewHeading_fmt_;
                } else if (isH2) {
                    text = line.substr(3);
                    fmt = cardPreviewHeading_fmt_;
                } else if (isBullet) {
                    text = L"\u2022 " + line.substr(2);
                    fmt = cardPreview_fmt_;
                } else if (isNumbered) {
                    text = line;
                    fmt = cardPreview_fmt_;
                } else if (line.empty()) {
                    curY += 6.0f;
                    continue;
                }

                // Create layout for this line.
                IDWriteTextLayout* tl = nullptr;
                dw_->CreateTextLayout(text.c_str(),
                    static_cast<UINT32>(text.size()),
                    fmt, pvW, 20.0f, &tl);
                if (tl) {
                    tl->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);

                    // Apply inline formatting: **bold**, *italic*, `code`.
                    // Scan the text and apply font weight/style/family to ranges.
                    for (size_t si = 0; si + 1 < text.size(); ) {
                        if (text[si] == L'*' && text[si + 1] == L'*') {
                            // Bold: find closing **
                            size_t end = text.find(L"**", si + 2);
                            if (end != std::wstring::npos) {
                                DWRITE_TEXT_RANGE range = {
                                    static_cast<UINT32>(si),
                                    static_cast<UINT32>(end + 2 - si)
                                };
                                tl->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
                                si = end + 2;
                            } else { si++; }
                        } else if (text[si] == L'`') {
                            // Code: find closing `
                            size_t end = text.find(L'`', si + 1);
                            if (end != std::wstring::npos) {
                                DWRITE_TEXT_RANGE range = {
                                    static_cast<UINT32>(si),
                                    static_cast<UINT32>(end + 1 - si)
                                };
                                tl->SetFontFamilyName(L"Consolas", range);
                                si = end + 1;
                            } else { si++; }
                        } else if (text[si] == L'*' || text[si] == L'_') {
                            // Italic: find closing * or _
                            wchar_t marker = text[si];
                            size_t end = text.find(marker, si + 1);
                            if (end != std::wstring::npos && end > si + 1) {
                                DWRITE_TEXT_RANGE range = {
                                    static_cast<UINT32>(si),
                                    static_cast<UINT32>(end + 1 - si)
                                };
                                tl->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, range);
                                si = end + 1;
                            } else { si++; }
                        } else {
                            si++;
                        }
                    }

                    DWRITE_TEXT_METRICS tm = {};
                    tl->GetMetrics(&tm);
                    rt->DrawTextLayout(D2D1::Point2F(textX, curY), tl,
                        isH1 || isH2 ? titleBrush : previewBrush);
                    tl->Release();
                    curY += tm.height + 2.0f;
                }
            }

            rt->PopAxisAlignedClip();
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
    if (hintBrush) hintBrush->Release();
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
