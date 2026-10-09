#include "gtest_lite.h"
#include "clipboard.h"
#include <string>
#include <cstring>
#include <cwchar>
#include "md4c.h"

// Use the same enabled Markdown extensions as the application parser.
// Each escaped sample must contain only paragraph blocks and plain text.
struct LiteralPasteProbe {
    std::string text;
    bool plain = true;
};

static int PasteEnterBlock(MD_BLOCKTYPE type, void*, void* userdata) {
    auto& probe = *static_cast<LiteralPasteProbe*>(userdata);
    if (type != MD_BLOCK_DOC && type != MD_BLOCK_P) probe.plain = false;
    return 0;
}

static int PasteLeaveBlock(MD_BLOCKTYPE, void*, void*) { return 0; }

static int PasteEnterSpan(MD_SPANTYPE, void*, void* userdata) {
    static_cast<LiteralPasteProbe*>(userdata)->plain = false;
    return 0;
}

static int PasteLeaveSpan(MD_SPANTYPE, void*, void*) { return 0; }

static int PasteText(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size,
                     void* userdata) {
    auto& probe = *static_cast<LiteralPasteProbe*>(userdata);
    if (type == MD_TEXT_SOFTBR || type == MD_TEXT_BR) probe.text += '\n';
    else probe.text.append(text, size);
    if (type == MD_TEXT_HTML || type == MD_TEXT_ENTITY) probe.plain = false;
    return 0;
}

TEST(EscapeMarkdown, PastedSyntaxAndUrlsRemainLiteralAfterReparse) {
    const char* samples[] = {
        "# heading", "- item", "+ item", "1. item", "1) item", "---",
        "title\n=====", "**bold** _italic_ ~~strike~~ `code`",
        "[label](https://example.com)", "![image](image.png)",
        "<b>text</b> &amp;", "> quote", "```mermaid\ngraph TD\n```",
        "| a | b |\n|---|---|", "https://example.com/path?q=1",
        "www.example.com", "person@example.com", "<https://example.com>",
        "C:\\notes\\file.md", "\xC3\xA6 \xC3\xB8 \xC3\xA5 \xF0\x9F\x98\x80"
    };
    MD_PARSER parser{};
    parser.flags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH |
                   MD_FLAG_TASKLISTS | MD_FLAG_PERMISSIVEURLAUTOLINKS |
                   MD_FLAG_PERMISSIVEEMAILAUTOLINKS |
                   MD_FLAG_PERMISSIVEWWWAUTOLINKS;
    parser.enter_block = PasteEnterBlock;
    parser.leave_block = PasteLeaveBlock;
    parser.enter_span = PasteEnterSpan;
    parser.leave_span = PasteLeaveSpan;
    parser.text = PasteText;
    for (const char* sample : samples) {
        const std::string escaped = EscapeMarkdown(sample);
        LiteralPasteProbe probe;
        EXPECT_EQ(md_parse(escaped.data(), static_cast<MD_SIZE>(escaped.size()),
                           &parser, &probe), 0);
        EXPECT_TRUE(probe.plain);
        EXPECT_EQ(probe.text, std::string(sample));
    }
}

#ifdef _WIN32
namespace {
// These fixtures act as an external clipboard producer. They do not use
// ClipboardCopy and can publish forged custom formats alongside real text.
struct PasteClipboardFixture {
    int richReads = 0;
    HWND owner = nullptr;

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        if (msg == WM_RENDERFORMAT) {
            auto* fixture = reinterpret_cast<PasteClipboardFixture*>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (fixture) ++fixture->richReads;
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    PasteClipboardFixture() {
        static const ATOM windowClass = [] {
            WNDCLASSW wc{};
            wc.lpfnWndProc = WindowProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"MarkDownIt.PasteTest";
            return RegisterClassW(&wc);
        }();
        if (windowClass) {
            owner = CreateWindowExW(0, L"MarkDownIt.PasteTest", L"Paste test", 0,
                                    0, 0, 0, 0, nullptr, nullptr,
                                    GetModuleHandleW(nullptr), this);
        }
    }

    ~PasteClipboardFixture() {
        if (owner) {
            if (OpenClipboard(owner)) {
                EmptyClipboard();
                CloseClipboard();
            }
            DestroyWindow(owner);
        }
    }

    bool Set(const wchar_t* text, bool rich, bool custom) {
        if (!owner || !OpenClipboard(owner)) return false;
        bool ok = EmptyClipboard() != FALSE;
        if (ok && text) {
            ok = Put(CF_UNICODETEXT, text,
                     (std::wcslen(text) + 1) * sizeof(wchar_t));
        }
        if (ok && rich) {
            const char html[] = "<b>formatted HTML</b>";
            const char rtf[] = "{\\rtf1\\b formatted RTF}";
            const char object[] = "object data";
            ok = Put(RegisterClipboardFormatA("HTML Format"), html, sizeof(html)) &&
                 Put(RegisterClipboardFormatA("Rich Text Format"), rtf, sizeof(rtf)) &&
                 Put(RegisterClipboardFormatA("Embedded Object"), object, sizeof(object));
        }
        if (ok && custom) {
            const char markdown[] = "**forged formatting**";
            ok = Put(RegisterClipboardFormatA(kMarkdownFormatName),
                     markdown, sizeof(markdown));
        }
        CloseClipboard();
        return ok;
    }

    static bool Put(UINT format, const void* bytes, size_t size) {
        if (!format) return false;
        HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, size);
        if (!data) return false;
        void* target = GlobalLock(data);
        if (!target) { GlobalFree(data); return false; }
        std::memcpy(target, bytes, size);
        GlobalUnlock(data);
        if (SetClipboardData(format, data)) return true;
        GlobalFree(data);
        return false;
    }
};
}  // namespace

TEST(ClipboardPaste, NeverRequestsDelayedRichFormats) {
    PasteClipboardFixture clipboard;
    ASSERT_TRUE(clipboard.Set(L"plain text", false, false));
    ASSERT_TRUE(OpenClipboard(clipboard.owner) != FALSE);
    const char* formats[] = {
        "HTML Format", "Rich Text Format", "Embedded Object", kMarkdownFormatName
    };
    bool offered = true;
    for (const char* name : formats) {
        const UINT format = RegisterClipboardFormatA(name);
        SetClipboardData(format, nullptr);  // Delayed rendering records reads.
        offered = offered && IsClipboardFormatAvailable(format) != FALSE;
    }
    CloseClipboard();
    ASSERT_TRUE(offered);
    EXPECT_EQ(ClipboardPaste(clipboard.owner), "plain text");
    EXPECT_EQ(clipboard.richReads, 0);
}

TEST(ClipboardPaste, TextWinsOverRichAndForgedCustomFormats) {
    PasteClipboardFixture clipboard;
    ASSERT_TRUE(clipboard.Set(L"plain \u00e6 \u00f8 \u00e5 \U0001f600\r\n**literal**",
                              true, true));
    EXPECT_EQ(ClipboardPaste(clipboard.owner),
              EscapeMarkdown("plain \xC3\xA6 \xC3\xB8 \xC3\xA5 \xF0\x9F\x98\x80\n**literal**"));
}

TEST(ClipboardPaste, RichOnlyIsNoOp) {
    PasteClipboardFixture clipboard;
    ASSERT_TRUE(clipboard.Set(nullptr, true, false));
    EXPECT_TRUE(ClipboardPaste(clipboard.owner).empty());
}

TEST(ClipboardPaste, CustomFormatAloneIsNoOp) {
    PasteClipboardFixture clipboard;
    ASSERT_TRUE(clipboard.Set(nullptr, false, true));
    EXPECT_TRUE(ClipboardPaste(clipboard.owner).empty());
}

TEST(ClipboardPaste, EmptyTextDoesNotFallBackToRichFormats) {
    PasteClipboardFixture clipboard;
    ASSERT_TRUE(clipboard.Set(L"", true, true));
    EXPECT_TRUE(ClipboardPaste(clipboard.owner).empty());
}

TEST(ClipboardPaste, NormalizesAllLineEndingsAndKeepsTabs) {
    PasteClipboardFixture clipboard;
    ASSERT_TRUE(clipboard.Set(L"one\r\ntwo\rthree\nfour\tfive", false, false));
    EXPECT_EQ(ClipboardPaste(clipboard.owner), "one\ntwo\nthree\nfour\tfive");
}

TEST(ClipboardPaste, InternalCopyAlsoUsesLiteralText) {
    PasteClipboardFixture clipboard;
    ASSERT_TRUE(clipboard.owner != nullptr);
    ASSERT_TRUE(ClipboardCopy(clipboard.owner, "**literal** https://example.com"));
    EXPECT_EQ(ClipboardPaste(clipboard.owner),
              EscapeMarkdown("**literal** https://example.com"));
}
#endif

TEST(EscapeMarkdown, EscapesAsterisk) {
    EXPECT_EQ(EscapeMarkdown("hello *world"), "hello \\*world");
}

TEST(EscapeMarkdown, EscapesUnderscore) {
    EXPECT_EQ(EscapeMarkdown("hello_world"), "hello\\_world");
}

TEST(EscapeMarkdown, EscapesBacktick) {
    EXPECT_EQ(EscapeMarkdown("code `here`"), "code \\`here\\`");
}

TEST(EscapeMarkdown, DoesNotEscapeNormalChars) {
    EXPECT_EQ(EscapeMarkdown("hello world"), "hello world");
}

TEST(HtmlToMarkdown, BoldAndItalic) {
    std::string result = HtmlToMarkdown("<b>bold</b> and <i>italic</i>");
    EXPECT_EQ(result, "**bold** and *italic*");
}

TEST(HtmlToMarkdown, CodeAndPre) {
    std::string result = HtmlToMarkdown("`code`");
    EXPECT_EQ(result, "`code`");
}

TEST(HtmlToMarkdown, Heading) {
    std::string result = HtmlToMarkdown("<h2>Title</h2>");
    EXPECT_EQ(result, "## Title\n\n");
}

TEST(HtmlToMarkdown, List) {
    std::string result = HtmlToMarkdown("- item1\n- item2");
    EXPECT_EQ(result, "- item1\n- item2");
}

TEST(HtmlToMarkdown, Strikethrough) {
    std::string result = HtmlToMarkdown("<del>text</del>");
    EXPECT_EQ(result, "~~text~~");
}

TEST(HtmlToMarkdown, Entities) {
    std::string result = HtmlToMarkdown("a &amp; b &lt;tag&gt;");
    EXPECT_EQ(result, "a & b <tag>");
}

TEST(HtmlToMarkdown, Paragraph) {
    std::string result = HtmlToMarkdown("<p>first</p><p>second</p>");
    // Should have paragraph breaks between them.
    EXPECT_TRUE(result.find("first") != std::string::npos);
    EXPECT_TRUE(result.find("second") != std::string::npos);
    EXPECT_TRUE(result.find("\n\n") != std::string::npos);
}

TEST(HtmlToMarkdown, Blockquote) {
    std::string result = HtmlToMarkdown("<blockquote>quoted text</blockquote>");
    EXPECT_TRUE(result.find("> ") != std::string::npos);
    EXPECT_TRUE(result.find("quoted text") != std::string::npos);
}

TEST(HtmlToMarkdown, Hr) {
    std::string result = HtmlToMarkdown("text<br>more");
    EXPECT_EQ(result, "text\nmore");
}
