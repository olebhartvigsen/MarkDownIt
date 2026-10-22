// The Core Text implementation of the mac TextEngine seam.
//
// The engine measures with CTFramesetter and draws with CTLine. That is
// the same pair the Windows side uses through DirectWrite, so the mapping
// between the two is one to one: a DWRITE_LINE_METRICS line is a CTLine,
// and a DWRITE_HIT_TEST_METRICS position is a
// CTLineGetStringIndexForPosition result.
//
// Only APIs that have been in Core Text since macOS 10.5 are used, so the
// file compiles against the oldest SDK the port supports and against the
// current one alike. In particular the run dictionary is NOT read: the
// engine knows where every run starts, because it built the attributed
// string itself, and it asks Core Text for the horizontal offset of each
// run start. That keeps the file free of the private run dictionary keys,
// which have changed between SDK releases.
//
// The ownership follows the Core Foundation get rules: every object the
// engine creates is released once, and the lines it keeps are handed
// over by the typesetter, which is what makes the engine their owner.
// The attribute calls run after the text is in the string, because a
// range that reaches past the end of the string faulted inside Core
// Foundation at the first real document open. The line breaks come from
// the typesetter one line at a time, because a frame with an unbounded
// height takes a whole paragraph, which made every paragraph measure as
// a single line.

#include "text_engine_mac.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CGColor.h>
#include <CoreText/CoreText.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <vector>

namespace mac {
namespace {

// The font families the theme asks for by name, mapped to what macOS
// actually has. "Segoe UI" does not exist on macOS, so the body font
// falls back to the system UI font. The mapping lives here rather than in
// the shared theme, so the Windows names stay untouched.
std::string ResolveFamily(const std::string& family, bool monospace) {
    if (monospace) {
        // Menlo ships with every macOS and is the usual code face.
        return "Menlo";
    }
    if (family == "Segoe UI" || family == "Segoe UI Semibold" ||
        family.empty()) {
        return "Helvetica Neue";
    }
    return family;
}

// The bold and italic traits, as the numeric values Core Text expects.
// The trait dictionary takes numbers, not the named constants, so the
// values are spelled out here. 0.5 is the standard bold weight and 0.25
// the standard italic slant.
const double kBoldWeight = 0.5;
const double kItalicSlant = 0.25;

// A font, cached. CTFont creation is not cheap and the renderer asks for
// the same handful of fonts over and over.
CTFontRef GetFont(const FontSpec& spec) {
    static std::map<std::string, CTFontRef> cache;
    const std::string key = ResolveFamily(spec.family, spec.monospace) + "|" +
                            std::to_string(static_cast<int>(spec.size)) + "|" +
                            (spec.bold ? "b" : "") + (spec.italic ? "i" : "");
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;

    CFStringRef familyRef =
        CFStringCreateWithCString(kCFAllocatorDefault,
                                  ResolveFamily(spec.family, spec.monospace)
                                      .c_str(),
                                  kCFStringEncodingUTF8);
    if (!familyRef) return nullptr;

    CFMutableDictionaryRef attrs =
        CFDictionaryCreateMutable(kCFAllocatorDefault, 2,
                                  &kCFTypeDictionaryKeyCallBacks,
                                  &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(attrs, kCTFontFamilyNameAttribute, familyRef);

    if (spec.bold || spec.italic) {
        CFMutableDictionaryRef traits =
            CFDictionaryCreateMutable(kCFAllocatorDefault, 2,
                                      &kCFTypeDictionaryKeyCallBacks,
                                      &kCFTypeDictionaryValueCallBacks);
        if (spec.bold) {
            const double weight = kBoldWeight;
            CFNumberRef number =
                CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType,
                               &weight);
            if (number) {
                CFDictionarySetValue(traits, kCTFontWeightTrait, number);
                CFRelease(number);
            }
        }
        if (spec.italic) {
            const double slant = kItalicSlant;
            CFNumberRef number =
                CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType,
                               &slant);
            if (number) {
                CFDictionarySetValue(traits, kCTFontSlantTrait, number);
                CFRelease(number);
            }
        }
        CFDictionarySetValue(attrs, kCTFontTraitsAttribute, traits);
        CFRelease(traits);
    }

    CTFontDescriptorRef desc = CTFontDescriptorCreateWithAttributes(attrs);
    CFRelease(attrs);
    CFRelease(familyRef);
    if (!desc) return nullptr;

    CTFontRef font = CTFontCreateWithFontDescriptor(desc, spec.size, nullptr);
    CFRelease(desc);
    if (!font) return nullptr;

    cache[key] = font;
    return font;
}

// A CFString from UTF-8.
CFStringRef MakeCFString(const std::string& utf8) {
    return CFStringCreateWithCString(kCFAllocatorDefault, utf8.c_str(),
                                     kCFStringEncodingUTF8);
}

// One run as the engine laid it out: the text, where it starts in UTF-16
// units of the whole attributed string, and its style.
struct LaidRun {
    std::string text;
    FontSpec font;
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
    // The UTF-16 index where the run starts.
    CFIndex start = 0;
    // The length of the run in UTF-16 units.
    CFIndex u16Length = 0;
    // The UTF-8 byte offset of every UTF-16 unit of the run, so a line
    // can cut its own slice out of the run.
    std::vector<uint32_t> u16ToUtf8;
};

}  // namespace

// The font the engine takes for a spec, which the drawing side shares,
// so the measured metrics and the drawn glyphs come from one font.
CTFontRef CoreTextFontFor(const FontSpec& spec) { return GetFont(spec); }

struct CoreTextEngine::Impl {
    // The measured lines of the last MeasureWrapped call. The hit tester
    // needs the Core Text object behind a TextLine, so the lines are kept
    // here, alive, until the next measurement.
    struct Entry {
        CTLineRef line = nullptr;
        // The width of the line, which is how a TextLine is matched to its
        // entry. The engine does not carry an index through the seam, so
        // the width is the identity.
        float width = 0.0f;
        // The UTF-8 byte offset of every UTF-16 unit of the line.
        std::vector<uint32_t> u16ToUtf8;
    };
    std::vector<Entry> entries;
};

CoreTextEngine::CoreTextEngine() : impl_(new Impl()) {}

CoreTextEngine::~CoreTextEngine() {
    for (auto& entry : impl_->entries) {
        if (entry.line) CFRelease(entry.line);
    }
    delete impl_;
}

std::vector<TextLine> CoreTextEngine::MeasureWrapped(
    const std::vector<GlyphRun>& runs, float maxWidth, float lineSpacing) {
    // Release the previous measurement, so the engine holds one snapshot.
    for (auto& entry : impl_->entries) {
        if (entry.line) CFRelease(entry.line);
    }
    impl_->entries.clear();

    if (runs.empty()) return {};

    // The attributed string, and the runs as the engine knows them.
    CFMutableAttributedStringRef attributed =
        CFAttributedStringCreateMutable(kCFAllocatorDefault, 0);
    if (!attributed) return {};

    std::vector<LaidRun> laidRuns;
    // The UTF-8 byte offset of every UTF-16 unit of the whole string.
    std::vector<uint32_t> u16ToUtf8;
    CFIndex u16Length = 0;

    for (const GlyphRun& run : runs) {
        if (run.text.empty()) continue;
        CFStringRef cfText = MakeCFString(run.text);
        if (!cfText) continue;
        const CFIndex runLength = CFStringGetLength(cfText);

        LaidRun laid;
        laid.text = run.text;
        laid.font = run.font;
        laid.r = run.r;
        laid.g = run.g;
        laid.b = run.b;
        laid.a = run.a;
        laid.start = u16Length;
        laid.u16Length = runLength;

        // The character mapping: one entry per UTF-16 unit of the run, so
        // a UTF-16 index maps back to a UTF-8 offset. The run keeps its
        // own copy, so a wrapped line can find the byte range of the part
        // of the run that sits on it.
        size_t byteIndex = 0;
        for (CFIndex i = 0; i < runLength; ++i) {
            const uint32_t ch =
                static_cast<uint32_t>(static_cast<unsigned char>(
                    run.text[byteIndex < run.text.size() ? byteIndex : 0]));
            uint32_t charBytes = 1;
            uint32_t utf16Units = 1;
            if (ch >= 0xF0) {
                charBytes = 4;
                utf16Units = 2;
            } else if (ch >= 0xE0) {
                charBytes = 3;
            } else if (ch >= 0xC0) {
                charBytes = 2;
            }
            u16ToUtf8.push_back(static_cast<uint32_t>(byteIndex));
            laid.u16ToUtf8.push_back(static_cast<uint32_t>(byteIndex));
            if (utf16Units == 2) {
                // The low surrogate shares the UTF-8 offset of the high
                // one.
                u16ToUtf8.push_back(static_cast<uint32_t>(byteIndex));
                laid.u16ToUtf8.push_back(static_cast<uint32_t>(byteIndex));
            }
            byteIndex += charBytes;
            if (byteIndex > run.text.size()) byteIndex = run.text.size();
        }

        // The text of the run is appended first, so that the attribute
        // calls below always cover text that is already in the string.
        CFAttributedStringReplaceString(attributed,
                                        CFRangeMake(u16Length, 0), cfText);
        CFRelease(cfText);

        // The style of the run.
        CTFontRef font = GetFont(run.font);
        if (font) {
            CFAttributedStringSetAttribute(
                attributed, CFRangeMake(u16Length, runLength),
                kCTFontAttributeName, font);
        }
        // The colour, as a CGColor, which is the type the foreground
        // attribute takes, the same as the drawing side builds.
        CGColorRef color = CGColorCreateGenericRGB(run.r, run.g, run.b, run.a);
        if (color) {
            CFAttributedStringSetAttribute(
                attributed, CFRangeMake(u16Length, runLength),
                kCTForegroundColorAttributeName, color);
            CFRelease(color);
        }

        laidRuns.push_back(laid);

        u16Length += runLength;
    }

    if (u16Length == 0) {
        CFRelease(attributed);
        return {};
    }

    // The typesetter, which knows the line breaks. It is asked for one
    // line break at a time, which is how a paragraph wraps.
    CTTypesetterRef typesetter =
        CTTypesetterCreateWithAttributedString(attributed);
    if (!typesetter) {
        CFRelease(attributed);
        return {};
    }

    // An unbounded width means one long line per paragraph.
    const CGFloat width = maxWidth > 0.0f ? maxWidth : CGFLOAT_MAX;

    std::vector<TextLine> lines;
    CFIndex start = 0;
    while (start < u16Length) {
        // The break that fits the width.
        CFIndex count = CTTypesetterSuggestLineBreak(typesetter, start, width);
        if (count <= 0) break;
        if (start + count > u16Length) count = u16Length - start;

        // The line itself. The typesetter hands it over, so the engine
        // owns it and releases it when the next measurement replaces it.
        CTLineRef line =
            CTTypesetterCreateLine(typesetter, CFRangeMake(start, count));
        if (!line) break;

        TextLine out;
        out.width = static_cast<float>(CTLineGetTypographicBounds(
            line, nullptr, nullptr, nullptr));
        double ascent = 0.0, descent = 0.0, leading = 0.0;
        CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
        // The typographic width is the return value of the call above,
        // so the height is summed from the three parts.
        out.height = static_cast<float>(ascent + descent + leading);
        out.ascent = static_cast<float>(ascent);

        // The runs of the line, cut to the part of the run that sits on
        // this line. A wrapped run shows a slice on every line it spans,
        // and the slice starts at the horizontal offset Core Text gives
        // for its first character on the line.
        for (const LaidRun& laid : laidRuns) {
            const CFIndex runEnd = laid.start + laid.u16Length;
            if (runEnd <= start) continue;
            if (laid.start >= start + count) break;

            const CFIndex clipStart = std::max(start, laid.start);
            const CFIndex clipEnd = std::min(start + count, runEnd);
            const size_t localFrom =
                static_cast<size_t>(clipStart - laid.start);
            const size_t localTo = static_cast<size_t>(clipEnd - laid.start);
            const uint32_t byteFrom =
                localFrom < laid.u16ToUtf8.size() ? laid.u16ToUtf8[localFrom]
                                                  : 0;
            const uint32_t byteTo =
                localTo < laid.u16ToUtf8.size()
                    ? laid.u16ToUtf8[localTo]
                    : static_cast<uint32_t>(laid.text.size());

            // The horizontal offset of the run on this line.
            double runX = 0.0;
            CTLineGetOffsetForStringIndex(line, clipStart, &runX);

            GlyphRun glyph;
            glyph.text = laid.text.substr(byteFrom, byteTo - byteFrom);
            if (glyph.text.empty()) continue;
            glyph.font = laid.font;
            glyph.r = laid.r;
            glyph.g = laid.g;
            glyph.b = laid.b;
            glyph.a = laid.a;
            glyph.x = static_cast<float>(runX);
            out.runs.push_back(glyph);
        }

        Impl::Entry entry;
        entry.line = line;
        entry.width = out.width;
        entry.u16ToUtf8 = u16ToUtf8;
        impl_->entries.push_back(entry);
        lines.push_back(out);
        start += count;
    }

    CFRelease(typesetter);
    CFRelease(attributed);

    // The line spacing is applied to the line heights, so the caller does
    // not have to add it back at every use.
    if (lineSpacing != 0.0f) {
        for (TextLine& line : lines) line.height += lineSpacing;
    }
    return lines;
}

uint32_t CoreTextEngine::HitTestOffset(const TextLine& line, float x,
                                       bool* outTrailing) const {
    if (outTrailing) *outTrailing = false;

    // The line is identified by its width, which is what the seam carries.
    for (const Impl::Entry& entry : impl_->entries) {
        if (!entry.line) continue;
        if (std::fabs(entry.width - line.width) > 0.5f) continue;

        // The position in the line, in typographic coordinates. The line's
        // origin is at its left edge, so the point is the x the caller
        // asked about.
        const CGPoint position = CGPointMake(x, 0.0);
        const CFIndex index =
            CTLineGetStringIndexForPosition(entry.line, position);

        // Whether the position landed on the trailing half of a glyph is
        // decided by the distance to the character boundary, which is what
        // the Windows side does with the hit test metrics.
        double boundary = 0.0;
        CTLineGetOffsetForStringIndex(entry.line, index, &boundary);
        if (outTrailing) {
            *outTrailing = (static_cast<double>(x) > boundary);
        }

        // The index is in UTF-16 units of the line, which maps back to a
        // UTF-8 offset through the table the measurement built.
        if (index <= 0) return 0;
        const size_t u16Index = static_cast<size_t>(index);
        if (u16Index >= entry.u16ToUtf8.size()) {
            return entry.u16ToUtf8.empty() ? 0 : entry.u16ToUtf8.back();
        }
        return entry.u16ToUtf8[u16Index];
    }
    return 0;
}

Rect CoreTextEngine::OffsetToRect(const TextLine& line,
                                  uint32_t offset) const {
    Rect rect;
    rect.height = line.height;
    if (impl_->entries.empty()) return rect;

    for (const Impl::Entry& entry : impl_->entries) {
        if (!entry.line) continue;
        if (std::fabs(entry.width - line.width) > 0.5f) continue;

        // The UTF-16 index of the UTF-8 offset.
        CFIndex index = 0;
        for (size_t i = 0; i < entry.u16ToUtf8.size(); ++i) {
            if (entry.u16ToUtf8[i] >= offset) {
                index = static_cast<CFIndex>(i);
                break;
            }
            index = static_cast<CFIndex>(i + 1);
        }

        // The horizontal position of the character, which Core Text gives
        // directly.
        double x = 0.0;
        CTLineGetOffsetForStringIndex(entry.line, index, &x);
        rect.x = static_cast<float>(x);
        return rect;
    }
    return rect;
}

float CoreTextEngine::LineHeight(const FontSpec& font) const {
    CTFontRef ctFont = GetFont(font);
    if (!ctFont) return font.size * 1.2f;
    // The ascent, descent and leading of the font. These three are the
    // same numbers DirectWrite reports for a font, so the two platforms
    // agree on the line height.
    const CGFloat ascent = CTFontGetAscent(ctFont);
    const CGFloat descent = CTFontGetDescent(ctFont);
    const CGFloat leading = CTFontGetLeading(ctFont);
    const CGFloat height = ascent + descent + leading;
    return height > 0.0 ? static_cast<float>(height) : font.size * 1.2f;
}

std::string CoreTextEngine::LineTextUtf8(const TextLine& line) {
    std::string text;
    for (const GlyphRun& run : line.runs) text += run.text;
    return text;
}

}  // namespace mac
