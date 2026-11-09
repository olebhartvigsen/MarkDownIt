// Private offscreen reference pixels from the unchanged macOS SVG renderer.
#include "gtest_lite.h"
#include "svg_native_fixture.h"

#include "../src/macos/cg_target_mac.h"
#include "../src/macos/svg_mac.h"
#include "../src/macos/text_engine_mac.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace {

constexpr int kWidth = svg_native_fixture::kWidth;
constexpr int kHeight = svg_native_fixture::kHeight;
static_assert(kWidth == 240 && kHeight == 140, "Native fixture dimensions");
static_assert(std::size(svg_native_fixture::kProbes) == 18,
              "All native shape probes must be sampled");
static_assert(std::size(svg_native_fixture::kArrows) == 4,
              "All native arrow-contact corridors must be sampled");

using ColorSpacePtr = std::unique_ptr<
    std::remove_pointer_t<CGColorSpaceRef>, decltype(&CGColorSpaceRelease)>;
using ContextPtr = std::unique_ptr<
    std::remove_pointer_t<CGContextRef>, decltype(&CGContextRelease)>;

class OffscreenBitmap {
public:
    OffscreenBitmap()
        : pixels_(kWidth * kHeight * 4, 255),
          colorSpace_(CGColorSpaceCreateWithName(kCGColorSpaceSRGB),
                      &CGColorSpaceRelease),
          context_(colorSpace_ ? CGBitmapContextCreate(
                                    pixels_.data(), kWidth, kHeight, 8,
                                    kWidth * 4, colorSpace_.get(),
                                    kCGImageAlphaPremultipliedLast |
                                        kCGBitmapByteOrder32Big)
                              : nullptr,
                   &CGContextRelease) {
        if (!context_) return;
        // RGBA byte order is explicit. Paint opaque white so RGB samples do
        // not depend on premultiplication or the allocation's initial bytes.
        CGContextSetRGBFillColor(context_.get(), 1, 1, 1, 1);
        CGContextFillRect(context_.get(), CGRectMake(0, 0, kWidth, kHeight));
        // 96-DPI SVG/DIP coordinates: one user unit is one bitmap pixel.
        // No screen/backing-scale or 72/96 point conversion is involved.
        CGContextTranslateCTM(context_.get(), 0, kHeight);
        CGContextScaleCTM(context_.get(), 1, -1);
    }

    CGContextRef context() const { return context_.get(); }

    std::array<int, 3> rgb(int x, int y) const {
        const size_t offset = (static_cast<size_t>(y) * kWidth + x) * 4;
        return {pixels_[offset], pixels_[offset + 1], pixels_[offset + 2]};
    }

private:
    // The context borrows these bytes; release it before the backing storage.
    std::vector<uint8_t> pixels_;
    ColorSpacePtr colorSpace_;
    ContextPtr context_;
};

// Half-open pixel bounds [left, top, right, bottom], matching image scans on
// the Windows side. No glyph-width expectation substitutes for Core Text.
struct Bounds {
    int left = kWidth;
    int top = kHeight;
    int right = 0;
    int bottom = 0;

    bool nonempty() const { return left < right && top < bottom; }
};

Bounds DarkBounds(const OffscreenBitmap& bitmap) {
    Bounds result;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const auto rgb = bitmap.rgb(x, y);
            if (rgb[0] < 128 && rgb[1] < 128 && rgb[2] < 128) {
                result.left = std::min(result.left, x);
                result.top = std::min(result.top, y);
                result.right = std::max(result.right, x + 1);
                result.bottom = std::max(result.bottom, y + 1);
            }
        }
    }
    return result;
}

Bounds RelationBounds(const OffscreenBitmap& bitmap, int left, int top,
                      int right, int bottom, bool markerOnly = false) {
    Bounds result;
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            const auto rgb = bitmap.rgb(x, y);
            const int r = rgb[0], g = rgb[1], b = rgb[2];
            // Match Windows, retaining antialiased ink on black and white.
            const bool red = r > 32 && r > g + 32 && r > b + 32;
            const bool green = g > 32 && g > r + 32 && g > b + 32;
            if (green || (!markerOnly && red)) {
                result.left = std::min(result.left, x);
                result.top = std::min(result.top, y);
                result.right = std::max(result.right, x + 1);
                result.bottom = std::max(result.bottom, y + 1);
            }
        }
    }
    return result;
}

struct TextFixture {
    const char* id;
    const char* xmlText;
    const char* parsedText;
    int x;
    bool middle;
};

constexpr TextFixture kTextFixtures[] = {
    {"plain", "H", "H", 30, false},
    {"newline", "H&#10;HH", "H\nHH", 30, false},
    {"anchored-newline", "H&#10;HHHH", "H\nHHHH", 120, true},
};

std::string TextSvg(const TextFixture& fixture) {
    return "<svg width=\"240\" height=\"140\"><text x=\"" +
           std::to_string(fixture.x) +
           "\" y=\"130\" font-family=\"Arial\" font-size=\"24\" fill=\"#000\"" +
           (fixture.middle ? " text-anchor=\"middle\"" : "") + ">" +
           fixture.xmlText + "</text></svg>";
}

}  // namespace

TEST(SvgNativeMac, CoreGraphicsReferencePixelsAndCoreTextBounds) {
    @autoreleasepool {
        mac::CoreTextEngine engine;
        mac::FontSpec font;
        font.family = "Arial";
        font.size = 24.0f;
        // This is a borrowed cached font, not a Create/Copy result to release.
        ASSERT_TRUE(mac::CoreTextFontFor(font) != nullptr);
        EXPECT_TRUE(engine.LineHeight(font) > 0);

        OffscreenBitmap shapes;
        ASSERT_TRUE(shapes.context() != nullptr);
        mac::SvgDoc document;
        ASSERT_TRUE(document.Load(svg_native_fixture::kSvg));
        ASSERT_EQ(document.Width(), static_cast<float>(kWidth));
        ASSERT_EQ(document.Height(), static_cast<float>(kHeight));
        mac::CoreGraphicsTarget target(shapes.context());
        document.Draw(&target, &engine, 0, 0, kWidth, kHeight);
        CGContextFlush(shapes.context());

        std::vector<std::array<int, 3>> samples;
        for (const auto& probe : svg_native_fixture::kProbes) {
            ASSERT_TRUE(probe.x >= 0 && probe.x < kWidth);
            ASSERT_TRUE(probe.y >= 0 && probe.y < kHeight);
            const auto rgb = shapes.rgb(probe.x, probe.y);
            samples.push_back(rgb);
            if (std::abs(rgb[0] - probe.r) > 2 ||
                std::abs(rgb[1] - probe.g) > 2 ||
                std::abs(rgb[2] - probe.b) > 2) {
                std::cerr << "  Native Mac probe: " << probe.name << " RGB ["
                          << rgb[0] << ',' << rgb[1] << ',' << rgb[2] << "]\n";
            }
            EXPECT_NEAR(rgb[0], probe.r, 2);
            EXPECT_NEAR(rgb[1], probe.g, 2);
            EXPECT_NEAR(rgb[2], probe.b, 2);
        }
        ASSERT_EQ(samples.size(), std::size(svg_native_fixture::kProbes));

        std::array<Bounds, std::size(kTextFixtures)> textBounds;
        for (size_t i = 0; i < std::size(kTextFixtures); ++i) {
            const auto& fixture = kTextFixtures[i];
            OffscreenBitmap text;
            ASSERT_TRUE(text.context() != nullptr);
            mac::SvgDoc textDocument;
            ASSERT_TRUE(textDocument.Load(TextSvg(fixture)));
            ASSERT_EQ(textDocument.Width(), static_cast<float>(kWidth));
            ASSERT_EQ(textDocument.Height(), static_cast<float>(kHeight));
            ASSERT_EQ(textDocument.shapes().size(), size_t{1});
            const auto& shape = textDocument.shapes().front();
            ASSERT_TRUE(shape.kind == mac::SvgDoc::Kind::Text);
            // Prove the XML character reference reaches the authoritative
            // scene unchanged. Do not remove newlines or predict CTLine width.
            ASSERT_EQ(shape.text, std::string(fixture.parsedText));
            ASSERT_EQ(shape.points.size(), size_t{1});
            EXPECT_EQ(shape.points.front().y, 130.0f);
            engine.BeginLayout();
            mac::CoreGraphicsTarget textTarget(text.context());
            textDocument.Draw(&textTarget, &engine, 0, 0, kWidth, kHeight);
            CGContextFlush(text.context());
            const Bounds bounds = DarkBounds(text);
            textBounds[i] = bounds;
            std::cout << "  Native Mac text " << fixture.id << " bounds ["
                      << bounds.left << ',' << bounds.top << ',' << bounds.right
                      << ',' << bounds.bottom << "]\n";
            EXPECT_TRUE(bounds.nonempty());
            EXPECT_LE(bounds.bottom - bounds.top, 26);
            EXPECT_LE(bounds.bottom, 131);
        }

        OffscreenBitmap arrows;
        ASSERT_TRUE(arrows.context() != nullptr);
        mac::SvgDoc arrowDocument;
        ASSERT_TRUE(arrowDocument.Load(svg_native_fixture::kArrowSvg));
        ASSERT_EQ(arrowDocument.Width(), static_cast<float>(kWidth));
        ASSERT_EQ(arrowDocument.Height(), static_cast<float>(kHeight));
        mac::CoreGraphicsTarget arrowTarget(arrows.context());
        arrowDocument.Draw(&arrowTarget, &engine, 0, 0, kWidth, kHeight);
        CGContextFlush(arrows.context());

        // Measure red shaft/green marker ink, not parsed path endpoints.
        // The fixed source contacts use marker refX=10; do not snap to boxes.
        std::array<Bounds, std::size(svg_native_fixture::kArrows)> arrowBounds;
        std::array<Bounds, std::size(svg_native_fixture::kArrows)> markerBounds;
        for (size_t i = 0; i < std::size(svg_native_fixture::kArrows); ++i) {
            const auto& fixture = svg_native_fixture::kArrows[i];
            ASSERT_TRUE(fixture.left >= 0 && fixture.left < fixture.right);
            ASSERT_TRUE(fixture.top >= 0 && fixture.top < fixture.bottom);
            ASSERT_TRUE(fixture.right <= kWidth && fixture.bottom <= kHeight);
            const Bounds bounds = RelationBounds(arrows, fixture.left, fixture.top,
                                                 fixture.right, fixture.bottom);
            arrowBounds[i] = bounds;
            const Bounds marker = RelationBounds(arrows, fixture.left, fixture.top,
                                                 fixture.right, fixture.bottom, true);
            markerBounds[i] = marker;
            std::cout << "  Native Mac arrow " << fixture.id << " bounds ["
                      << bounds.left << ',' << bounds.top << ',' << bounds.right
                      << ',' << bounds.bottom << "]\n";
            EXPECT_TRUE(bounds.nonempty());
            EXPECT_NEAR(bounds.left, fixture.expectedLeft, 1);
            EXPECT_NEAR(bounds.top, fixture.expectedTop, 1);
            EXPECT_NEAR(bounds.right, fixture.expectedRight, 1);
            EXPECT_NEAR(bounds.bottom, fixture.expectedBottom, 1);
            EXPECT_TRUE(marker.nonempty());
            EXPECT_NEAR(marker.left, fixture.expectedMarkerLeft, 1);
            EXPECT_NEAR(marker.top, fixture.expectedMarkerTop, 1);
            EXPECT_NEAR(marker.right, fixture.expectedMarkerRight, 1);
            EXPECT_NEAR(marker.bottom, fixture.expectedMarkerBottom, 1);
        }

        // Export only measured numbers, never an image or a screen capture.
        // Keep emitting actual measurements on assertion failures for CI triage.
        if (const char* path = std::getenv("MDI_SVG_NATIVE_OUT")) {
            ASSERT_TRUE(*path != '\0');
            std::ofstream output(path, std::ios::out | std::ios::trunc);
            ASSERT_TRUE(output.is_open());
            output << "{\"schema\":2,\"probes\":[";
            for (size_t i = 0; i < samples.size(); ++i) {
                if (i) output << ',';
                const auto& rgb = samples[i];
                output << "{\"name\":\"" << svg_native_fixture::kProbes[i].name
                       << "\",\"rgb\":[" << rgb[0] << ',' << rgb[1] << ','
                       << rgb[2] << "]}";
            }
            output << "],\"text\":[";
            for (size_t i = 0; i < std::size(kTextFixtures); ++i) {
                if (i) output << ',';
                const auto& bounds = textBounds[i];
                output << "{\"id\":\"" << kTextFixtures[i].id
                       << "\",\"bounds\":[" << bounds.left << ',' << bounds.top
                       << ',' << bounds.right << ',' << bounds.bottom << "]}";
            }
            output << "],\"arrows\":[";
            for (size_t i = 0; i < std::size(svg_native_fixture::kArrows); ++i) {
                if (i) output << ',';
                const auto& bounds = arrowBounds[i];
                const auto& marker = markerBounds[i];
                output << "{\"id\":\"" << svg_native_fixture::kArrows[i].id
                       << "\",\"bounds\":[" << bounds.left << ',' << bounds.top
                       << ',' << bounds.right << ',' << bounds.bottom
                       << "],\"marker\":[" << marker.left << ',' << marker.top
                       << ',' << marker.right << ',' << marker.bottom << "]}";
            }
            output << "]}\n";
            output.close();
            EXPECT_TRUE(output.good());
        }
    }
}
