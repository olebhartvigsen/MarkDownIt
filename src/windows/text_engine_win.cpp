// The DirectWrite engine behind the text seam, plan task 2.2. The runs
// of a paragraph go into one DirectWrite layout with word wrapping,
// the same shape the Core Text twin gives its typesetter, and every
// line answers the runs it holds a slice of. Offsets are answered in
// UTF-8 bytes, the seam's contract unit; the UTF-16 mapping is
// DirectWrite's native position grid.

#include "windows/text_engine_win.h"

#include <dwrite.h>
#include <windows.h>

#include <algorithm>
#include <utility>

namespace platform {
namespace {

// UTF-8 to UTF-16, self contained so no locale can move the answer.
std::wstring ToUtf16(const std::string& text) {
    std::wstring out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = (unsigned char)text[i];
        unsigned int cp = 0;
        size_t extra = 0;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1Fu;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0Fu;
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07u;
            extra = 3;
        } else {
            cp = 0xFFFDu;  // a stray byte is one replacement character
            extra = 0;
        }
        for (size_t k = 0; k < extra && i + 1 + k < text.size(); ++k) {
            unsigned char cc = (unsigned char)text[i + 1 + k];
            if ((cc & 0xC0) != 0x80) {
                extra = k;
                break;
            }
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        i += 1 + extra;
        if (cp > 0x10FFFFu) cp = 0xFFFDu;
        if (cp < 0x10000u) {
            out.push_back((wchar_t)cp);
        } else {
            cp -= 0x10000u;
            out.push_back((wchar_t)(0xD800u + (cp >> 10)));
            out.push_back((wchar_t)(0xDC00u + (cp & 0x3FFu)));
        }
    }
    return out;
}

// The theme names fonts by role; Windows needs a real family.
std::wstring FamilyFor(const std::string& family) {
    std::string f = family;
    std::transform(f.begin(), f.end(), f.begin(),
                   [](unsigned char c) { return (char)tolower(c); });
    if (f == "code" || f == "mono" || f == "consolas") return L"Consolas";
    if (f == "serif" || f == "georgia") return L"Georgia";
    return L"Segoe UI";  // body, headings, ui and anything unknown
}

float WidestLineHeight(const std::vector<GlyphRun>& runs) {
    float h = 0.0f;
    for (const GlyphRun& run : runs) {
        float rh = run.font.size > 0.0f ? run.font.size * 1.5f : 12.0f;
        h = (std::max)(h, rh);
    }
    return h > 0.0f ? h : 12.0f;  // an empty line still needs a height
}


// Maps every UTF-16 code unit of the run's UTF-8 bytes onto the byte
// offset of the character it belongs to, and one past the end onto the
// total length: the unit contract of the seam's hit test, answered in
// the bytes of run.text. A low surrogate names its pair's lead byte.
std::vector<uint32_t> U16ToByte(const std::string& text) {
    std::vector<uint32_t> map;
    map.reserve(text.size() + 1);
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = (unsigned char)text[i];
        size_t len = 1;
        if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        const uint32_t lead = (uint32_t)i;
        for (size_t k = 0; k < len; ++k) map.push_back(lead);
        i += len;
    }
    map.push_back((uint32_t)text.size());
    return map;
}

}  // namespace

DWriteEngine::DWriteEngine() {
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                   __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&factory_)))) {
        factory_ = nullptr;  // every later call degrades to empty text
    }
}

DWriteEngine::~DWriteEngine() {
    for (auto& entry : formats_) {
        if (entry.second) entry.second->Release();
    }
    if (factory_) factory_->Release();
}

void DWriteEngine::BeginLayout() {
    // Formats stay cached; DirectWrite layouts are created fresh per
    // measure, so there is no state to reset.
}

float DWriteEngine::LineHeight(const FontSpec& font) const {
    return font.size > 0.0f ? font.size * 1.5f : 12.0f;
}

IDWriteTextFormat* DWriteEngine::FormatFor(const FontSpec& font) const {
    if (!factory_) return nullptr;
    const std::wstring family = FamilyFor(font.family);
    const float size = font.size > 0.0f ? font.size : 14.0f;
    const auto key = std::make_pair(family, size);
    auto it = formats_.find(key);
    if (it != formats_.end()) return it->second;
    IDWriteTextFormat* format = nullptr;
    if (FAILED(factory_->CreateTextFormat(family.c_str(), nullptr,
                                          DWRITE_FONT_WEIGHT_NORMAL,
                                          DWRITE_FONT_STYLE_NORMAL,
                                          DWRITE_FONT_STRETCH_NORMAL,
                                          size, L"",
                                          &format))) {
        format = nullptr;
    }
    if (format) {
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }
    formats_[key] = format;
    return format;
}

float DWriteEngine::MeasureRun(const GlyphRun& run,
                               float maxWidth) const {
    IDWriteTextFormat* format = FormatFor(run.font);
    if (!format || run.text.empty()) return 0.0f;
    const std::wstring text = ToUtf16(run.text);
    IDWriteTextLayout* layout = nullptr;
    if (FAILED(factory_->CreateTextLayout(
            text.c_str(), (UINT32)text.size(), format,
            maxWidth > 0.0f ? maxWidth : 100000.0f, 1000.0f, &layout))) {
        return 0.0f;
    }
    DWRITE_TEXT_METRICS metrics = {};
    float width = 0.0f;
    if (SUCCEEDED(layout->GetMetrics(&metrics))) {
        width = metrics.width;
    }
    layout->Release();
    return width;
}

float DWriteEngine::PositionInRun(const GlyphRun& run, float maxWidth,
                                  uint32_t utf16Offset) const {
    IDWriteTextFormat* format = FormatFor(run.font);
    if (!format || run.text.empty()) return 0.0f;
    const std::wstring text = ToUtf16(run.text);
    if (utf16Offset > text.size()) utf16Offset = (uint32_t)text.size();
    IDWriteTextLayout* layout = nullptr;
    if (FAILED(factory_->CreateTextLayout(
            text.c_str(), (UINT32)text.size(), format,
            maxWidth > 0.0f ? maxWidth : 100000.0f, 1000.0f, &layout))) {
        return 0.0f;
    }
    float x = 0.0f;
    float y = 0.0f;
    DWRITE_HIT_TEST_METRICS hit = {};
    if (FAILED(layout->HitTestTextPosition(utf16Offset, false,
                                           &x, &y, &hit))) {
        x = 0.0f;
    }
    layout->Release();
    return x;
}

std::vector<TextLine> DWriteEngine::MeasureWrapped(
        const std::vector<GlyphRun>& runs,
        float maxWidth, float lineSpacing) {
    (void)lineSpacing;  // spacing rides LayoutMetrics, not the engine
    // One layout over the whole run list, the shape the Core Text
    // twin gives its typesetter: the words wrap at the width inside
    // the engine, a run shows a slice on every line it lands on, and
    // the break bytes stay with the line they end so the byte account
    // survives the wrap. If the account ever fails, the answer falls
    // back to the old run-glide composition, which never loses a
    // byte, and the mismatch shows up as drift to referee.
    const auto glideCompose = [&]() {
        std::vector<TextLine> out;
        TextLine line;
        line.height = WidestLineHeight(runs);
        float x = 0.0f;
        for (const GlyphRun& run : runs) {
            const float w = MeasureRun(run, maxWidth);
            // A run never breaks inside itself; a full run moves to
            // the next line when it no longer fits, and a first run
            // stays on the line even when too wide, the shared wrap
            // rule.
            if (maxWidth > 0.0f && x > 0.0f && x + w > maxWidth) {
                line.width = x;
                out.push_back(std::move(line));
                line = TextLine();
                line.height = WidestLineHeight(runs);
                x = 0.0f;
            }
            GlyphRun onLine = run;
            onLine.x = x;
            x += w;
            line.runs.push_back(std::move(onLine));
        }
        line.width = x;
        if (line.runs.empty() || x > 0.0f) out.push_back(std::move(line));
        if (out.empty()) {
            line = TextLine();
            line.height = WidestLineHeight(runs);
            out.push_back(std::move(line));
        }
        return out;
    };

    // The spans: one per run, in order, with the run's own UTF-16 to
    // byte map so a slice can name its source bytes.
    struct Span {
        size_t runIndex;
        UINT32 u16Start;
        UINT32 u16Length;
        std::vector<uint32_t> u16ToByte;
    };
    std::vector<Span> spans;
    std::wstring text;
    size_t byteTotal = 0;
    for (size_t r = 0; r < runs.size(); ++r) {
        Span span;
        span.runIndex = r;
        span.u16Start = (UINT32)text.size();
        span.u16ToByte = U16ToByte(runs[r].text);
        const std::wstring part = ToUtf16(runs[r].text);
        span.u16Length = (UINT32)part.size();
        text += part;
        byteTotal += runs[r].text.size();
        spans.push_back(std::move(span));
    }
    if (!factory_ || spans.empty() || text.empty() || byteTotal == 0) {
        TextLine line;
        line.height = WidestLineHeight(runs);
        std::vector<TextLine> out;
        out.push_back(std::move(line));
        return out;
    }

    // The first run with text owns the base format; every run dresses
    // its own span with its family, size, weight and decorations.
    size_t base = 0;
    while (base < runs.size() && runs[base].text.empty()) ++base;
    IDWriteTextFormat* format = base < runs.size()
        ? FormatFor(runs[base].font) : nullptr;
    IDWriteTextLayout* layout = nullptr;
    const bool wrap = maxWidth > 0.0f;
    if (!format || FAILED(factory_->CreateTextLayout(
            text.c_str(), (UINT32)text.size(), format,
            wrap ? maxWidth : 100000.0f, 100000.0f, &layout)) || !layout) {
        return glideCompose();
    }
    layout->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP
                                 : DWRITE_WORD_WRAPPING_NO_WRAP);
    for (size_t r = 0; r < runs.size(); ++r) {
        if (runs[r].text.empty()) continue;
        const DWRITE_TEXT_RANGE range = {spans[r].u16Start,
                                         spans[r].u16Length};
        const std::wstring family = FamilyFor(runs[r].font.family);
        layout->SetFontFamilyName(family.c_str(), range);
        layout->SetFontSize(
            runs[r].font.size > 0.0f ? runs[r].font.size : 14.0f, range);
        if (runs[r].font.bold)
            layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
        if (runs[r].font.italic)
            layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, range);
        if (runs[r].underline) layout->SetUnderline(TRUE, range);
        if (runs[r].strike) layout->SetStrikethrough(TRUE, range);
    }

    // The lines DirectWrite broke, sliced back onto the runs.
    UINT32 lineCount = 0;
    if (FAILED(layout->GetLineMetrics(nullptr, 0, &lineCount)) ||
        lineCount == 0) {
        layout->Release();
        return glideCompose();
    }
    std::vector<DWRITE_LINE_METRICS> lineMetrics(lineCount);
    UINT32 got = 0;
    if (FAILED(layout->GetLineMetrics(lineMetrics.data(), lineCount, &got)) ||
        got == 0) {
        layout->Release();
        return glideCompose();
    }
    lineMetrics.resize(got);

    std::vector<TextLine> lines;
    UINT32 cursor = 0;  // the UTF-16 index the current line starts at
    size_t slicedBytes = 0;
    for (const DWRITE_LINE_METRICS& m : lineMetrics) {
        // DirectWrite's length stops before the line ending characters
        // (they ride the newlineLength count), and the account must
        // hold every byte, so the break characters belong to this
        // line's last slice, the same piece the fake keeps them with.
        const UINT32 start = cursor;
        const UINT32 end = start + m.length + m.newlineLength;
        cursor = end;
        TextLine out;
        out.height = m.height > 0.0f ? m.height : WidestLineHeight(runs);
        out.ascent = m.baseline;
        float lastX = 0.0f;
        for (const Span& span : spans) {
            const UINT32 rs = span.u16Start;
            const UINT32 re = rs + span.u16Length;
            if (re <= start) continue;
            if (rs >= end) break;
            const UINT32 a = rs > start ? rs : start;
            const UINT32 b = re < end ? re : end;
            if (b <= a) continue;
            GlyphRun slice = runs[span.runIndex];
            const UINT32 byteA = span.u16ToByte[a - rs];
            const UINT32 byteB = span.u16ToByte[b - rs];
            slice.text = slice.text.substr(byteA, byteB - byteA);
            float hx = 0.0f;
            float hy = 0.0f;
            DWRITE_HIT_TEST_METRICS hit = {};
            if (FAILED(layout->HitTestTextPosition(a, false, &hx, &hy,
                                                   &hit))) {
                hx = lastX;
            }
            slice.x = hx;
            out.runs.push_back(std::move(slice));
            lastX = hx;
            slicedBytes += byteB - byteA;
        }
        if (!out.runs.empty()) {
            const GlyphRun& last = out.runs.back();
            out.width = last.x + MeasureRun(last, 100000.0f);
        }
        lines.push_back(std::move(out));
    }
    layout->Release();
    if (slicedBytes != byteTotal) return glideCompose();
    return lines;
}

uint32_t DWriteEngine::HitTestOffset(const TextLine& line, float x,
                                     bool* outTrailing) const {
    if (outTrailing) *outTrailing = false;
    // Walk the runs with DirectWrite's own positions: every run
    // starts at run.x, and the end of a run is the start plus the
    // advance width DirectWrite measured for its text. Offsets are
    // answered in UTF-8 bytes, the seam's contract unit.
    uint32_t seenBytes = 0;
    for (const GlyphRun& run : line.runs) {
        const std::wstring text = ToUtf16(run.text);
        if (text.empty()) continue;
        const float runStart = run.x;
        const float runWidth = MeasureRun(run, 100000.0f);
        if (x < runStart + runWidth) {
            // Find the code unit whose position first reaches x, then
            // name it in bytes through the mapping above.
            const std::vector<uint32_t> u16ToByte =
                U16ToByte(run.text);
            float prev = runStart;
            for (uint32_t u = 1; u <= text.size(); ++u) {
                const float pos = runStart + PositionInRun(run,
                                                            100000.0f, u);
                if (pos >= x) {
                    if (outTrailing)
                        *outTrailing = (x - prev) > ((pos - prev) * 0.5f);
                    return seenBytes + u16ToByte[u];
                }
                prev = pos;
            }
            return seenBytes + (uint32_t)run.text.size();
        }
        seenBytes += (uint32_t)run.text.size();
    }
    return seenBytes;
}

Rect DWriteEngine::OffsetToRect(const TextLine& line,
                                uint32_t offset) const {
    Rect rect;
    rect.height = line.height;
    // The offset names a byte in run.text. Find the run, then the
    // code unit whose byte the offset is, and answer its position.
    uint32_t seenBytes = 0;
    for (const GlyphRun& run : line.runs) {
        if (offset < seenBytes + run.text.size()) {
            const std::vector<uint32_t> u16ToByte =
                U16ToByte(run.text);
            for (uint32_t u = 0; u + 1 < u16ToByte.size(); ++u) {
                if (u16ToByte[u] > offset - seenBytes) break;
                if (u + 1 == u16ToByte.size() - 1 ||
                    u16ToByte[u + 1] > offset - seenBytes) {
                    const float inside = PositionInRun(run, 100000.0f, u);
                    rect.x = run.x + inside;
                    rect.width = 1.0f;  // a caret's sliver
                    return rect;
                }
            }
        }
        seenBytes += (uint32_t)run.text.size();
    }
    // At or past the end: the trailing edge of the last run.
    float endX = 0.0f;
    for (const GlyphRun& run : line.runs) {
        endX = run.x + MeasureRun(run, 100000.0f);
    }
    rect.x = endX;
    rect.width = 1.0f;
    return rect;
}

}  // namespace platform
