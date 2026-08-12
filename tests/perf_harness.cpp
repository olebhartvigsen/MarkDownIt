#include "gtest_lite.h"
#include "parser.h"
#include "textbuffer.h"
#include "dom.h"
#include <chrono>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

extern "C" {
#include "md4c.h"
}

static std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

template <typename Fn>
static double TimeMs(Fn fn) {
    auto start = std::chrono::high_resolution_clock::now();
    fn();
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

TEST(Perf, ReparseSmallDoc) {
    std::string doc;
    for (int i = 0; i < 50; i++) {
        doc += "## Heading " + std::to_string(i) + "\n\n";
        doc += "This is paragraph " + std::to_string(i) + " with **bold** text.\n\n";
    }
    double timeMs = TimeMs([&]() {
        Document d;
        ParseMarkdown(doc, d);
    });
    std::cout << "  Small doc reparse: " << timeMs << " ms (budget: 5 ms)" << std::endl;
    EXPECT_TRUE(timeMs < 5.0);
}

TEST(Perf, TextBufferSplice) {
    std::string doc;
    for (int i = 0; i < 100000; i++) {
        doc += "Line " + std::to_string(i) + "\n";
    }
    TextBuffer buf;
    buf.SetText(doc);
    double timeMs = TimeMs([&]() {
        buf.Splice(1000, 0, "x");
        buf.Splice(1000, 1, "");
    });
    std::cout << "  Splice (insert+delete): " << timeMs << " ms (budget: 16 ms)" << std::endl;
    EXPECT_TRUE(timeMs < 16.0);
}

TEST(Perf, LargeDocParse) {
    std::string doc = ReadFile("tests/fixtures/large-document.md");
    if (doc.empty()) {
        for (int i = 0; i < 50000; i++) {
            doc += "This is paragraph " + std::to_string(i) + ".\n\n";
        }
    }
    double timeMs = TimeMs([&]() {
        Document d;
        ParseMarkdown(doc, d);
    });
    std::cout << "  Large doc parse: " << timeMs << " ms "
              << "(doc size: " << doc.size() << " bytes, budget: 500 ms)" << std::endl;
    EXPECT_TRUE(timeMs < 500.0);
}

TEST(Perf, MeasureParseMs) {
    std::string doc;
    for (int i = 0; i < 100; i++) {
        doc += "# Heading " + std::to_string(i) + "\n\n";
        doc += "Paragraph with *italic* and **bold**.\n\n";
    }
    double ms = MeasureParseMs(doc);
    std::cout << "  MeasureParseMs: " << ms << " ms" << std::endl;
    EXPECT_TRUE(ms > 0.0);
    EXPECT_TRUE(ms < 100.0);
}
