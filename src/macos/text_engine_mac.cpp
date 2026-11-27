// The Core Text implementation of the mac TextEngine seam.
//
// The engine measures with CTTypesetter and draws with CTLine. That is
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
#include <CoreGraphics/CGAffineTransform.h>
#include <CoreGraphics/CGColor.h>
#include <CoreText/CoreText.h>

#include "crash_trace_mac.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace platform {
namespace {

// The family name that stands for "the platform's own UI face". Core
// Text is asked for the system font itself (see CreateSystemFont), so
// the OS provides whichever family is current.
const char* const kSystemFamily = "System UI";
// The fixed-pitch sibling: the code face resolves to the
// platform's fixed-pitch UI font through the same API, so the
// OS decides which mono face is current.
const char* const kSystemMonoFamily = "System Mono";

// The font families the theme asks for by name, mapped to the platform
// faces. The theme keeps the Windows names; the mac build draws with
// the system stack (body: the system UI font; code: Menlo), as the port
// plan's font note asks. A family that is not one of the mapped names
// passes through untouched.
std::string ResolveFamily(const std::string& family, bool monospace) {
    if (monospace) {
        // The code face: the platform's fixed-pitch UI font through
        // the system API, the same way the text face resolves, so the
        // OS picks the current mono face.
        return kSystemMonoFamily;
    }
    if (family == "Segoe UI Semibold" || family == "Segoe UI" ||
        family.empty()) {
        return kSystemFamily;
    }
    return family;
}

// True for the runs the theme marks semibold. The Windows build spells
// that as a family name of its own, "Segoe UI Semibold".
bool WantsSemibold(const std::string& family) {
    return family == "Segoe UI Semibold";
}

// The last resort faces, for a system that fails to answer with the
// family that was asked for.
const char* const kFallbackSans = "Helvetica Neue";
const char* const kFallbackMono = "Menlo";

// The bold and italic traits, as the numeric values Core Text expects.
// The trait dictionary takes numbers, not the named constants, so the
// values are spelled out here. They are the weights Core Text matches
// faces against: regular is 0.0, semibold 0.3 and bold 0.4, and 0.25 is
// the slant of the system italic.
const double kBoldWeight = 0.4;
const double kSemiboldWeight = 0.3;
const double kItalicSlant = 0.25;

// A font, cached. CTFont creation is not cheap and the renderer asks for
// the same handful of fonts over and over.
// One CTFont for a resolved family and a style: bold, semibold and
// italic are Core Text traits the descriptor carries.
CTFontRef CreateFont(const std::string& family, const FontSpec& spec,
                     bool semibold) {
    CFStringRef familyRef =
        CFStringCreateWithCString(kCFAllocatorDefault, family.c_str(),
                                  kCFStringEncodingUTF8);
    if (!familyRef) return nullptr;

    CFMutableDictionaryRef attrs =
        CFDictionaryCreateMutable(kCFAllocatorDefault, 2,
                                  &kCFTypeDictionaryKeyCallBacks,
                                  &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(attrs, kCTFontFamilyNameAttribute, familyRef);

    if (spec.bold || semibold || spec.italic) {
        CFMutableDictionaryRef traits =
            CFDictionaryCreateMutable(kCFAllocatorDefault, 2,
                                      &kCFTypeDictionaryKeyCallBacks,
                                      &kCFTypeDictionaryValueCallBacks);
        if (spec.bold || semibold) {
            const double weight = spec.bold ? kBoldWeight : kSemiboldWeight;
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
    return font;
}

// True when the font's family is the one that was asked for. The engine
// reads this to notice that a named face did not answer, in which case
// Core Text returned a substitute.
bool FamilyMatches(CTFontRef font, const std::string& family) {
    if (!font) return false;
    CFStringRef want = CFStringCreateWithCString(
        kCFAllocatorDefault, family.c_str(), kCFStringEncodingUTF8);
    if (!want) return false;
    CFStringRef have = CTFontCopyFamilyName(font);
    const bool match =
        have && CFStringCompare(have, want, 0) == kCFCompareEqualTo;
    if (have) CFRelease(have);
    CFRelease(want);
    return match;
}

// The platform's own UI face at the wanted style: Core Text resolves
// the system font, so the OS provides whichever family is current, and
// the weight and slant traits pick semibold, bold and italic from it.
// The fixed-pitch flag asks for the system's mono face through the
// same API, for code.
CTFontRef CreateSystemFont(const FontSpec& spec, bool semibold,
                           bool monospace) {
    CTFontRef base = CTFontCreateUIFontForLanguage(
        monospace ? kCTFontUIFontUserFixedPitch : kCTFontUIFontSystem,
        spec.size, nullptr);
    if (!base) return nullptr;

    // The weight first: bold and semibold are weight traits Core Text
    // selects a face with.
    if (spec.bold || semibold) {
        const double weight = spec.bold ? kBoldWeight : kSemiboldWeight;
        CFNumberRef number =
            CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &weight);
        CFMutableDictionaryRef traits = CFDictionaryCreateMutable(
            kCFAllocatorDefault, 1, &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);
        if (number) {
            CFDictionarySetValue(traits, kCTFontWeightTrait, number);
            CFRelease(number);
        }
        CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(
            kCFAllocatorDefault, 1, &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);
        CFDictionarySetValue(attrs, kCTFontTraitsAttribute, traits);
        CTFontDescriptorRef desc = CTFontDescriptorCreateWithAttributes(attrs);
        CTFontRef weighted =
            desc ? CTFontCreateCopyWithAttributes(base, spec.size, nullptr,
                                                  desc)
                 : nullptr;
        if (desc) CFRelease(desc);
        CFRelease(attrs);
        CFRelease(traits);
        if (weighted) {
            CFRelease(base);
            base = weighted;
        }
    }

    if (spec.italic) {
        // The real italic face first: the symbolic trait selects one
        // where the family carries it. A face that comes back as the
        // upright one (a system without italics, or a family the
        // trait cannot reach) gets the shear instead, tan(12 degrees),
        // so an italic run is never drawn upright.
        CTFontRef slanted = CTFontCreateCopyWithSymbolicTraits(
            base, spec.size, nullptr, kCTFontItalicTrait,
            kCTFontItalicTrait);
        bool different = false;
        if (slanted) {
            CFStringRef upright = CTFontCopyPostScriptName(base);
            CFStringRef asked = CTFontCopyPostScriptName(slanted);
            different = upright && asked &&
                        CFStringCompare(upright, asked, 0) !=
                            kCFCompareEqualTo;
            if (upright) CFRelease(upright);
            if (asked) CFRelease(asked);
        }
        if (different) {
            CFRelease(base);
            base = slanted;
        } else {
            if (slanted) CFRelease(slanted);
            CGAffineTransform matrix =
                CGAffineTransformMake(1.0, 0.0, 0.2126, 1.0, 0.0, 0.0);
            CFDataRef data = CFDataCreate(
                kCFAllocatorDefault, reinterpret_cast<const UInt8*>(&matrix),
                sizeof(matrix));
            CFMutableDictionaryRef shearAttrs = CFDictionaryCreateMutable(
                kCFAllocatorDefault, 1, &kCFTypeDictionaryKeyCallBacks,
                &kCFTypeDictionaryValueCallBacks);
            if (data) {
                CFDictionarySetValue(shearAttrs, kCTFontMatrixAttribute, data);
                CFRelease(data);
            }
            CTFontDescriptorRef shearDesc =
                CTFontDescriptorCreateWithAttributes(shearAttrs);
            CTFontRef sheared =
                shearDesc ? CTFontCreateCopyWithAttributes(base, spec.size,
                                                           nullptr, shearDesc)
                          : nullptr;
            if (shearDesc) CFRelease(shearDesc);
            CFRelease(shearAttrs);
            if (sheared) {
                CFRelease(base);
                base = sheared;
            }
        }
    }
    return base;
}

CTFontRef GetFont(const FontSpec& spec) {
    static std::map<std::string, CTFontRef> cache;
    const bool semibold = WantsSemibold(spec.family);
    const std::string resolved = ResolveFamily(spec.family, spec.monospace);
    const std::string key = resolved + "|" +
                            std::to_string(static_cast<int>(spec.size)) + "|" +
                            (spec.bold ? "b" : "") + (spec.italic ? "i" : "") +
                            (semibold ? "s" : "");
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;

    const bool systemFamily = (resolved == kSystemFamily);
    const bool systemMono = (resolved == kSystemMonoFamily);
    CTFontRef font = (systemFamily || systemMono)
                         ? CreateSystemFont(spec, semibold, systemMono)
                         : CreateFont(resolved, spec, semibold);
    if (!systemFamily && !systemMono && font &&
        !FamilyMatches(font, resolved)) {
        // A named face did not answer (unusual on macOS). The fallback
        // faces keep text drawing.
        CFRelease(font);
        font = CreateFont(spec.monospace ? kFallbackMono : kFallbackSans,
                          spec, false);
        static bool logged = false;
        if (!logged) {
            TraceMac("a text face did not resolve; "
                     "the fallback faces take over");
            logged = true;
        }
    }
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
    Color color;
    // The run is a link and carries an underline, or is struck
    // through; the flags travel through the measured lines so the
    // renderer can draw the decorations.
    bool underline = false;
    bool strike = false;
    // The UTF-16 index where the run starts.
    CFIndex start = 0;
    // The length of the run in UTF-16 units.
    CFIndex u16Length = 0;
    // The UTF-8 byte offset of every UTF-16 unit of the run, so a line
    // can cut its own slice out of the run.
    std::vector<uint32_t> u16ToUtf8;
};

// The cap on the line entries the engine keeps for hit testing. The cap
// bounds the memory of an engine whose measurements are never bracketed
// by a layout pass (the diagram engine). A document stays far below it,
// so the hit tester keeps every line of the current layout.
constexpr size_t kMaxLineEntries = 20000;

}  // namespace

// The font the engine takes for a spec, which the drawing side shares,
// so the measured metrics and the drawn glyphs come from one font.
CTFontRef CoreTextFontFor(const FontSpec& spec) { return GetFont(spec); }

// The family the engine maps a theme family name onto. The tests read
// the mapping through here.
std::string ResolvedFamily(const std::string& family, bool monospace) {
    return ResolveFamily(family, monospace);
}

struct CoreTextEngine::Impl {
    // The measured lines since the last BeginLayout. The hit tester
    // needs the Core Text object behind a TextLine, so the lines are kept
    // here, alive, until the next layout pass starts.
    struct Entry {
        CTLineRef line = nullptr;
        // The key the measured TextLine carries, which is the identity a
        // reader asks with. The width and the text stay for the lines a
        // renderer builds by hand, which never passed through a measure.
        uint64_t key = 0;
        float width = 0.0f;
        std::string text;
        // Core Text answers a position and a caret x in the paragraph's
        // own index space, not the line's: a line-relative index falls
        // outside the line's string range, and Core Text clamps it to the
        // line's first byte. The entry keeps the range the line has there
        // and the paragraph's byte map, so both readers speak the same
        // units Core Text does.
        CFIndex u16Start = 0;
        uint32_t byteBase = 0;
        // The UTF-8 byte offset of every UTF-16 unit of the line.
        std::vector<uint32_t> u16ToUtf8;
    };
    std::vector<Entry> entries;
    // The key the next measured line gets. It only grows, so a line from
    // a layout that has since been replaced finds no entry and the check
    // falls back to the width and the text.
    uint64_t nextKey = 0;
};

CoreTextEngine::CoreTextEngine() : impl_(std::make_unique<Impl>()) {}

CoreTextEngine::~CoreTextEngine() {
    for (auto& entry : impl_->entries) {
        if (entry.line) CFRelease(entry.line);
    }
}

void CoreTextEngine::BeginLayout() {
    // A new layout pass starts: the lines of the pass before it are
    // dropped, and the hit tester works against the new layout.
    for (auto& entry : impl_->entries) {
        if (entry.line) CFRelease(entry.line);
    }
    impl_->entries.clear();
}

std::vector<TextLine> CoreTextEngine::MeasureWrapped(
    const std::vector<GlyphRun>& runs, float maxWidth, float lineSpacing) {
    // The entries stay until the next BeginLayout, so the hit tester can
    // reach every line of the layout the renderer made. An engine whose
    // measurements are never bracketed that way (the diagram engine)
    // stops growing here instead.
    if (impl_->entries.size() > kMaxLineEntries) {
        for (auto& entry : impl_->entries) {
            if (entry.line) CFRelease(entry.line);
        }
        impl_->entries.clear();
    }

    if (runs.empty()) return {};

    // The attributed string, and the runs as the engine knows them.
    CFMutableAttributedStringRef attributed =
        CFAttributedStringCreateMutable(kCFAllocatorDefault, 0);
    if (!attributed) return {};

    std::vector<LaidRun> laidRuns;
    // The UTF-8 byte offset of every UTF-16 unit of the whole string.
    std::vector<uint32_t> u16ToUtf8;
    CFIndex u16Length = 0;
    // The map runs over the whole paragraph, so every run adds its own
    // bytes to the base. A line's entry takes its slice of this map and
    // rebases it to the line, which is the offset its readers use.
    uint32_t lineByteBase = 0;

    for (const GlyphRun& run : runs) {
        if (run.text.empty()) continue;
        CFStringRef cfText = MakeCFString(run.text);
        if (!cfText) continue;
        const CFIndex runLength = CFStringGetLength(cfText);

        LaidRun laid;
        laid.text = run.text;
        laid.font = run.font;
        laid.color = run.color;
        laid.underline = run.underline;
        laid.strike = run.strike;
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
            u16ToUtf8.push_back(lineByteBase +
                               static_cast<uint32_t>(byteIndex));
            laid.u16ToUtf8.push_back(static_cast<uint32_t>(byteIndex));
            if (utf16Units == 2) {
                // The low surrogate shares the UTF-8 offset of the high
                // one.
                u16ToUtf8.push_back(lineByteBase +
                                   static_cast<uint32_t>(byteIndex));
                laid.u16ToUtf8.push_back(static_cast<uint32_t>(byteIndex));
            }
            byteIndex += charBytes;
            if (byteIndex > run.text.size()) byteIndex = run.text.size();
        }

        // The paragraph's map is over the whole string, so each run's
        // bytes start where the run before it ended. One step per run:
        // a second one skips a run's worth of bytes on every line that
        // has more than one run, and the caret after a styled span then
        // answers the span's last byte for every offset past it.
        lineByteBase += static_cast<uint32_t>(run.text.size());

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
        // The colour, as a CGColor, and only for a run that names one. A
        // run with no ink of its own carries no foreground attribute, so
        // Core Text draws it in the context's fill colour: the ink the
        // target passes to DrawLayout. That is the same fallback the
        // portable path applies to a run with no colour.
        if (run.color.a > 0.0f) {
            CGColorRef color = CGColorCreateGenericRGB(run.color.r, run.color.g,
                                                      run.color.b, run.color.a);
            if (color) {
                CFAttributedStringSetAttribute(
                    attributed, CFRangeMake(u16Length, runLength),
                    kCTForegroundColorAttributeName, color);
                CFRelease(color);
            }
        } else {
            // The run has no ink of its own, so it takes the context's
            // fill colour: the target's DrawLayout sets that fill to the
            // block's colour. Without this attribute Core Text would use
            // black whatever the fill is.
            CFAttributedStringSetAttribute(
                attributed, CFRangeMake(u16Length, runLength),
                kCTForegroundColorFromContextAttributeName, kCFBooleanTrue);
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
            glyph.color = laid.color;
            glyph.underline = laid.underline;
            glyph.strike = laid.strike;
            glyph.x = static_cast<float>(runX);
            out.runs.push_back(glyph);
        }

        Impl::Entry entry;
        entry.line = line;
        out.measuredKey = ++impl_->nextKey;
        entry.key = out.measuredKey;
        entry.width = out.width;
        entry.text = CoreTextEngine::LineTextUtf8(out);
        // The map stays in the paragraph's byte space, and the entry keeps
        // where the line starts in it. Rebasing to the line made the byte
        // table the callers' unit but left Core Text reading absolute
        // indices, so every position on a wrapped line answered the
        // line's first byte.
        entry.u16Start = start;
        entry.byteBase = static_cast<size_t>(start) < u16ToUtf8.size()
                             ? u16ToUtf8[static_cast<size_t>(start)]
                             : 0;
        entry.u16ToUtf8.reserve(static_cast<size_t>(count));
        for (CFIndex i = 0; i < count; ++i) {
            const size_t at = static_cast<size_t>(start + i);
            entry.u16ToUtf8.push_back(
                at < u16ToUtf8.size() ? u16ToUtf8[at]
                                      : entry.byteBase);
        }
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

    const std::size_t at = EntryIndexFor(line);
    if (at >= impl_->entries.size()) return 0;
    const Impl::Entry& entry = impl_->entries[at];

    // A hit at or past the trailing edge of the line answers the last
    // position, the line's full byte length, the same rule as the Windows
    // and fake engines.
    if (x >= entry.width - 0.5f) {
        return static_cast<uint32_t>(entry.text.size());
    }

    // The position in the line, in typographic coordinates. The line's
    // origin is at its left edge, so the point is the x the caller asked
    // about.
    const CGPoint position = CGPointMake(x, 0.0);
    const CFIndex index =
        CTLineGetStringIndexForPosition(entry.line, position);

    // Whether the position landed on the trailing half of a glyph is
    // decided by the distance to the character boundary, which is what the
    // Windows side does with the hit test metrics.
    double boundary = 0.0;
    CTLineGetOffsetForStringIndex(entry.line, index, &boundary);
    if (outTrailing) {
        *outTrailing = (static_cast<double>(x) > boundary);
    }

    // Core Text answered in the paragraph's units, so the index is turned
    // back into a byte offset inside this line before it leaves the seam.
    const CFIndex local = index - entry.u16Start;
    if (local <= 0) return 0;
    if (static_cast<std::size_t>(local) >= entry.u16ToUtf8.size()) {
        return entry.u16ToUtf8.empty()
                   ? 0
                   : entry.u16ToUtf8.back() - entry.byteBase;
    }
    return entry.u16ToUtf8[static_cast<std::size_t>(local)] - entry.byteBase;
}

Rect CoreTextEngine::OffsetToRect(const TextLine& line,
                                  uint32_t offset) const {
    Rect rect;
    rect.height = line.height;
    if (impl_->entries.empty()) return rect;

    // A line the engine no longer holds answers its own start: the caller
    // draws a caret at the beginning of the line rather than nothing.
    const std::size_t at = EntryIndexFor(line);
    if (at >= impl_->entries.size()) return rect;
    const Impl::Entry& entry = impl_->entries[at];

    // The byte the caller names, in the paragraph's space, and the UTF-16
    // index that holds it. An offset past the line's last byte answers the
    // line's end, which is where Core Text clamps an index out of range.
    const uint32_t target = entry.byteBase + offset;
    CFIndex index = entry.u16Start +
                    static_cast<CFIndex>(entry.u16ToUtf8.size());
    for (std::size_t i = 0; i < entry.u16ToUtf8.size(); ++i) {
        if (entry.u16ToUtf8[i] >= target) {
            index = entry.u16Start + static_cast<CFIndex>(i);
            break;
        }
    }

    double x = 0.0;
    CTLineGetOffsetForStringIndex(entry.line, index, &x);
    rect.x = static_cast<float>(x);
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

bool CoreTextEngine::FontBox(const FontSpec& font, float* natural,
                             float* ascent) const {
    CTFontRef ctFont = GetFont(font);
    if (!ctFont) return false;
    const CGFloat up = CTFontGetAscent(ctFont);
    const CGFloat down = CTFontGetDescent(ctFont);
    const CGFloat leading = CTFontGetLeading(ctFont);
    const CGFloat height = up + down + leading;
    if (!(height > 0.0)) return false;
    if (natural) *natural = static_cast<float>(height);
    if (ascent) *ascent = static_cast<float>(up);
    return true;
}

std::vector<std::string> CoreTextEngine::FontFamilies() const {
    std::vector<std::string> names;
    CFArrayRef families = CTFontManagerCopyAvailableFontFamilyNames();
    if (!families) return names;
    const CFIndex count = CFArrayGetCount(families);
    names.reserve(static_cast<size_t>(count > 0 ? count : 0));
    for (CFIndex i = 0; i < count; ++i) {
        CFStringRef name = static_cast<CFStringRef>(
            CFArrayGetValueAtIndex(families, i));
        if (!name) continue;
        char buffer[512];
        if (CFStringGetCString(name, buffer, sizeof(buffer),
                               kCFStringEncodingUTF8)) {
            names.push_back(buffer);
        }
    }
    CFRelease(families);
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

CgTextLayout::CgTextLayout(std::vector<Line> lines, float width,
                           float height)
    : lines_(std::move(lines)), width_(width), height_(height) {}

CgTextLayout::~CgTextLayout() {
    for (Line& line : lines_) {
        if (line.line) CFRelease(line.line);
    }
}

// The caret's questions, answered by the CTLine the block kept for each
// line. Core Text knows a line's string range directly, so unlike
// DirectWrite there is no need to probe the line's edges.

uint32_t CgTextLayout::LineCount() const {
    return static_cast<uint32_t>(lines_.size());
}

float CgTextLayout::LineHeightAt(uint32_t lineIndex) const {
    return lineIndex < lines_.size() ? lines_[lineIndex].height : 0.0f;
}

bool CgTextLayout::LineTextRange(uint32_t lineIndex, float layoutWidth,
                                 uint32_t* outStart, uint32_t* outEnd) const {
    (void)layoutWidth;  // the line knows its own range
    if (lineIndex >= lines_.size() || !lines_[lineIndex].line) return false;
    const CFRange range = CTLineGetStringRange(lines_[lineIndex].line);
    if (range.location == kCFNotFound || range.location < 0) return false;
    if (outStart) *outStart = static_cast<uint32_t>(range.location);
    if (outEnd) {
        *outEnd = static_cast<uint32_t>(range.location + range.length);
    }
    return true;
}

TextHit CgTextLayout::HitTestPoint(float x, float y) const {
    TextHit hit;
    if (lines_.empty()) return hit;

    uint32_t index = 0;
    for (uint32_t i = 0; i < lines_.size(); ++i) {
        if (y < lines_[i].top + lines_[i].height ||
            i + 1 == lines_.size()) {
            index = i;
            break;
        }
    }
    const Line& line = lines_[index];
    if (!line.line) return hit;

    const CGPoint point = CGPointMake(x - line.x, 0.0);
    CFIndex at = CTLineGetStringIndexForPosition(line.line, point);
    const CFRange range = CTLineGetStringRange(line.line);
    const CFIndex lineEnd = range.location + range.length;
    if (at == kCFNotFound || at < range.location) at = range.location;
    if (at > lineEnd) at = lineEnd;

    // The trailing half of the glyph decides the side of the caret the
    // point belongs to, the rule the engine's own hit tester uses.
    const CGFloat here = CTLineGetOffsetForStringIndex(line.line, at, nullptr);
    const CGFloat next =
        at < lineEnd ? CTLineGetOffsetForStringIndex(line.line, at + 1, nullptr)
                     : here;
    hit.trailing = (x - line.x - here) > (next - here) * 0.5;
    hit.textPosition = static_cast<uint32_t>(at);
    hit.height = line.height;
    hit.inside = y >= line.top && y < line.top + line.height &&
                 x >= 0.0f && x <= width_;
    return hit;
}

TextHit CgTextLayout::CaretAtOffset(uint32_t textPosition) const {
    TextHit hit;
    hit.textPosition = textPosition;
    if (lines_.empty()) return hit;

    uint32_t index = 0;
    for (uint32_t i = 0; i < lines_.size(); ++i) {
        index = i;
        if (!lines_[i].line) break;
        const CFRange range = CTLineGetStringRange(lines_[i].line);
        const CFIndex lineEnd = range.location + range.length;
        if (textPosition < static_cast<uint32_t>(lineEnd) ||
            i + 1 == lines_.size()) {
            break;
        }
    }
    const Line& line = lines_[index];
    if (!line.line) return hit;

    const CGFloat offset = CTLineGetOffsetForStringIndex(
        line.line, static_cast<CFIndex>(textPosition), nullptr);
    hit.x = line.x + static_cast<float>(offset);
    hit.y = line.top;
    hit.height = line.height;
    hit.inside = true;
    return hit;
}

TextMeasure CoreTextEngine::MeasureText(const std::string& text,
                                        const FontSpec& font, float maxWidth,
                                        const LineBox& box) {
    TextMeasure out;
    if (text.empty()) return out;
    GlyphRun run;
    run.text = text;
    run.font = font;
    // One run, the label's own newlines and the width the diagram wraps
    // at: the same measure the mac painter's diagram path always made.
    const std::vector<TextLine> lines =
        MeasureWrapped({run}, maxWidth > 0.0f ? maxWidth : 1.0e6f, 0.0f);
    for (const TextLine& line : lines) {
        if (line.width > out.width) out.width = line.width;
        out.height += box.heightEm > 0.0f ? box.heightEm * font.size
                                          : line.height;
    }
    return out;
}

TextLayoutHandle CoreTextEngine::BuildBlock(
    const std::u16string& text, const std::vector<TextRangeStyle>& styles,
    const BlockStyle& style) {
    if (text.empty() || !(style.maxWidth > 0.0f)) return nullptr;
    const std::vector<GlyphRun> runs = detail::DressedRuns(text, styles,
                                                           style.font);
    if (runs.empty()) return nullptr;
    std::vector<TextLine> lines = MeasureWrapped(runs, style.maxWidth, 0.0f);
    if (lines.empty()) return nullptr;
    if (style.lineHeightEm > 0.0f) {
        float natural = 0.0f;
        float ascent = 0.0f;
        if (FontBox(style.font, &natural, &ascent) && natural > 0.0f) {
            const float height = style.lineHeightEm * style.font.size;
            const float baseline = (height - natural) * 0.5f + ascent;
            for (TextLine& line : lines) {
                line.height = height;
                line.ascent = baseline;
            }
        }
    }
    return MakeLineLayout(std::move(lines), style.maxWidth, style.align);
}

TextLayoutHandle CoreTextEngine::BuildLayout(
    const std::vector<GlyphRun>& runs, float maxWidth, TextAlign align) {
    // A block the engine cannot cut still has the seam's portable answer,
    // built from the same measured lines.
    const auto fallback = [&]() {
        return TextEngine::BuildLayout(runs, maxWidth, align);
    };
    if (runs.empty() || !(maxWidth > 0.0f)) return fallback();

    // The measure also keeps the CTLine of every line for the hit tester.
    // The block takes its own reference to those lines, so the lines that
    // draw are the lines that were measured, not a second layout built
    // from the run texts.
    const std::vector<TextLine> measured =
        MeasureWrapped(runs, maxWidth, 0.0f);
    if (measured.empty()) return fallback();

    std::vector<CgTextLayout::Line> lines;
    lines.reserve(measured.size());
    float top = 0.0f;
    bool complete = true;
    for (const TextLine& source : measured) {
        const std::size_t at = EntryIndexFor(source);
        if (at >= impl_->entries.size() || !impl_->entries[at].line) {
            complete = false;
            break;
        }
        CgTextLayout::Line out;
        out.line = static_cast<CTLineRef>(
            CFRetain(impl_->entries[at].line));
        // The alignment moves the line inside the column; Core Text draws
        // from the line's own left edge, unlike DirectWrite, which aligns
        // the paragraph itself.
        if (align == TextAlign::Center)
            out.x = (maxWidth - source.width) * 0.5f;
        else if (align == TextAlign::Right)
            out.x = maxWidth - source.width;
        out.top = top;
        out.ascent = source.ascent;
        out.height = source.height;
        top += source.height;
        lines.push_back(out);
    }
    if (!complete) {
        for (CgTextLayout::Line& line : lines) {
            if (line.line) CFRelease(line.line);
        }
        return fallback();
    }
    return std::make_shared<CgTextLayout>(std::move(lines), maxWidth, top);
}

std::size_t CoreTextEngine::EntryIndexFor(const TextLine& line) const {
    // A measured line carries the engine's key, which is exact: two lines
    // can share a width and a text, and an entry from a replaced layout
    // can share the width alone.
    if (line.measuredKey != 0) {
        for (std::size_t i = 0; i < impl_->entries.size(); ++i) {
            const Impl::Entry& entry = impl_->entries[i];
            if (entry.line && entry.key == line.measuredKey) return i;
        }
    }
    const std::string wanted = CoreTextEngine::LineTextUtf8(line);
    for (std::size_t i = 0; i < impl_->entries.size(); ++i) {
        const Impl::Entry& entry = impl_->entries[i];
        if (!entry.line) continue;
        if (std::fabs(entry.width - line.width) > 0.5f) continue;
        if (!entry.text.empty() && entry.text != wanted) continue;
        return i;
    }
    return impl_->entries.size();
}

std::string CoreTextEngine::LineTextUtf8(const TextLine& line) {
    std::string text;
    for (const GlyphRun& run : line.runs) text += run.text;
    return text;
}

}  // namespace platform
