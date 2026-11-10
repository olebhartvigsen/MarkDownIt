// The transcript tests of the inline formatting family, extraction
// batch 4 family 3 (references/batch4-controller-extraction.md).
//
// Each transcript under tests/transcripts/inline_formatting is a
// start text, a command list written in the document controller's
// command names, and the expected text, selection and undo depth the
// commands must leave behind. The runner replays the commands
// straight through DocumentController, the same bodies both ports
// run, so the transcripts pin the port-agnostic behavior: the
// pending typing format rules, the shared per run toggle semantics
// (the resolution of the map's divergence 1, pinned by the
// bold-multi-run transcript) and the clear format strip-all engine.
//
// The undo depth is the number of entries left in the undo stack
// after the command list, measured by undoing until the stack is
// empty. Undo entries never re-coalesce (coalescing happens on
// push), so the number is deterministic on both platforms.
//
// Parity pipeline contract (tools/check_transcript_parity.py): when
// the environment names an output file in MDI_TRANSCRIPT_OUT, the run
// writes one digest object there at exit, one entry per transcript
// case: name, passed, undo_depth and a sha256 of the case's final
// buffer text. Both platform jobs set the variable and the pipeline
// compares the digest files; local runs stay quiet.

#include "documentcontroller.h"
#include "gtest_lite.h"
#include "parser.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// A small honest JSON reader, sufficient for the transcript fixtures:
// objects, arrays, strings with the standard escapes, numbers, booleans
// and null. The transcript files stay this simple on purpose.
// ---------------------------------------------------------------------------

struct Json {
    enum Kind { Null, Bool, Num, Str, Arr, Obj } kind = Null;
    bool boolean = false;
    double number = 0;
    std::string str;
    std::vector<Json> items;
    std::map<std::string, Json> fields;
};

class JsonReader {
public:
    explicit JsonReader(const std::string& text) : text_(text) {}

    bool Failed() const { return failed_; }

    Json Read() {
        SkipSpace();
        return ReadValue();
    }

private:
    const std::string& text_;
    size_t pos_ = 0;
    bool failed_ = false;

    void SkipSpace() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
            else break;
        }
    }

    bool Eat(char c) {
        SkipSpace();
        if (pos_ < text_.size() && text_[pos_] == c) { ++pos_; return true; }
        return false;
    }

    void Fail() { failed_ = true; }

    Json ReadValue() {
        SkipSpace();
        Json j;
        if (pos_ >= text_.size()) { Fail(); return j; }
        const char c = text_[pos_];
        if (c == '{') return ReadObject();
        if (c == '[') return ReadArray();
        if (c == '"') { j.kind = Json::Str; j.str = ReadString(); return j; }
        if (c == 't' || c == 'f') { j.kind = Json::Bool; j.boolean = ReadBool(); return j; }
        if (c == 'n') { ReadLiteral("null"); return j; }
        j.kind = Json::Num;
        j.number = ReadNumber();
        return j;
    }

    Json ReadObject() {
        Json j;
        j.kind = Json::Obj;
        if (!Eat('{')) { Fail(); return j; }
        if (Eat('}')) return j;
        while (!failed_) {
            SkipSpace();
            if (pos_ >= text_.size() || text_[pos_] != '"') { Fail(); break; }
            const std::string key = ReadString();
            if (!Eat(':')) { Fail(); break; }
            j.fields[key] = ReadValue();
            if (Eat(',')) continue;
            if (Eat('}')) break;
            Fail();
            break;
        }
        return j;
    }

    Json ReadArray() {
        Json j;
        j.kind = Json::Arr;
        if (!Eat('[')) { Fail(); return j; }
        if (Eat(']')) return j;
        while (!failed_) {
            j.items.push_back(ReadValue());
            if (Eat(',')) continue;
            if (Eat(']')) break;
            Fail();
            break;
        }
        return j;
    }

    std::string ReadString() {
        std::string out;
        if (pos_ >= text_.size() || text_[pos_] != '"') { Fail(); return out; }
        ++pos_;
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') return out;
            if (c != '\\') { out += c; continue; }
            if (pos_ >= text_.size()) break;
            const char e = text_[pos_++];
            switch (e) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u': {
                    if (pos_ + 4 > text_.size()) { Fail(); return out; }
                    unsigned code = 0;
                    for (int k = 0; k < 4; ++k) {
                        const char h = text_[pos_++];
                        code <<= 4;
                        if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
                        else { Fail(); return out; }
                    }
                    // Encode the code point as UTF 8. The transcripts
                    // only carry short escapes, never surrogates.
                    if (code < 0x80) {
                        out += static_cast<char>(code);
                    } else if (code < 0x800) {
                        out += static_cast<char>(0xC0 | (code >> 6));
                        out += static_cast<char>(0x80 | (code & 0x3F));
                    } else {
                        out += static_cast<char>(0xE0 | (code >> 12));
                        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (code & 0x3F));
                    }
                    break;
                }
                default: Fail(); return out;
            }
        }
        Fail();
        return out;
    }

    bool ReadBool() {
        if (text_.compare(pos_, 4, "true") == 0) { pos_ += 4; return true; }
        if (text_.compare(pos_, 5, "false") == 0) { pos_ += 5; return false; }
        Fail();
        return false;
    }

    void ReadLiteral(const char* lit) {
        const size_t n = std::string(lit).size();
        if (text_.compare(pos_, n, lit) == 0) pos_ += n;
        else Fail();
    }

    double ReadNumber() {
        size_t begin = pos_;
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' ||
                c == 'e' || c == 'E') {
                ++pos_;
            } else {
                break;
            }
        }
        if (begin == pos_) { Fail(); return 0; }
        return std::atof(text_.substr(begin, pos_ - begin).c_str());
    }
};

const Json* Find(const Json& obj, const char* key) {
    const auto it = obj.fields.find(key);
    return it == obj.fields.end() ? nullptr : &it->second;
}

std::string StrOf(const Json& obj, const char* key,
                  const std::string& fallback = std::string()) {
    const Json* j = Find(obj, key);
    return (j && j->kind == Json::Str) ? j->str : fallback;
}

int IntOf(const Json& obj, const char* key, int fallback) {
    const Json* j = Find(obj, key);
    return (j && j->kind == Json::Num) ? static_cast<int>(j->number) : fallback;
}

// ---------------------------------------------------------------------------
// SHA 256 (FIPS 180-4), over the final buffer text of a case. The
// parity pipeline compares these digests, so the hash must be a real
// one, computed the same way on every platform.
// ---------------------------------------------------------------------------

class Sha256 {
public:
    explicit Sha256(const std::string& message) {
        state_[0] = 0x6a09e667; state_[1] = 0xbb67ae85;
        state_[2] = 0x3c6ef372; state_[3] = 0xa54ff53a;
        state_[4] = 0x510e527f; state_[5] = 0x9b05688c;
        state_[6] = 0x1f83d9ab; state_[7] = 0x5be0cd19;
        uint8_t block[64];
        size_t pos = 0;
        const size_t len = message.size();
        while (pos + 64 <= len) {
            for (int i = 0; i < 64; ++i) block[i] = static_cast<uint8_t>(message[pos + i]);
            Compress(block);
            pos += 64;
        }
        for (int i = 0; i < 64; ++i) block[i] = 0;
        size_t rest = len - pos;
        for (size_t i = 0; i < rest; ++i)
            block[i] = static_cast<uint8_t>(message[pos + i]);
        block[rest] = 0x80;
        if (rest >= 56) {
            Compress(block);
            for (int i = 0; i < 64; ++i) block[i] = 0;
        }
        const uint64_t bits = static_cast<uint64_t>(len) * 8;
        for (int i = 0; i < 8; ++i)
            block[63 - i] = static_cast<uint8_t>((bits >> (8 * i)) & 0xFF);
        Compress(block);
    }

    std::string Hex() const {
        static const char* digits = "0123456789abcdef";
        std::string out;
        for (int i = 0; i < 8; ++i) {
            for (int shift = 28; shift >= 0; shift -= 4)
                out += digits[(state_[i] >> shift) & 0xF];
        }
        return out;
    }

private:
    uint32_t state_[8];

    static uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void Compress(const uint8_t block[64]) {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
            0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
            0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
            0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
            0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
            0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
            0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
            0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int t = 0; t < 16; ++t) {
            w[t] = (static_cast<uint32_t>(block[t * 4]) << 24) |
                   (static_cast<uint32_t>(block[t * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(block[t * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(block[t * 4 + 3]));
        }
        for (int t = 16; t < 64; ++t) {
            const uint32_t s0 = Rotr(w[t - 15], 7) ^ Rotr(w[t - 15], 18) ^
                                (w[t - 15] >> 3);
            const uint32_t s1 = Rotr(w[t - 2], 17) ^ Rotr(w[t - 2], 19) ^
                                (w[t - 2] >> 10);
            w[t] = w[t - 16] + s0 + w[t - 7] + s1;
        }
        uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (int t = 0; t < 64; ++t) {
            const uint32_t S1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
            const uint32_t choice = (e & f) ^ (~e & g);
            const uint32_t temp1 = h + S1 + choice + k[t] + w[t];
            const uint32_t S0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = S0 + majority;
            h = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }
        state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
        state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
    }
};

// ---------------------------------------------------------------------------
// The transcript case registry and the digest file the parity pipeline
// reads. The emitter runs at process exit (after RUN_ALL_TESTS), and
// only when the environment names an output file.
// ---------------------------------------------------------------------------

struct CaseDigest {
    std::string name;
    bool passed = false;
    int undo_depth = 0;
    std::string text_hash;
};

// The registry is deliberately never destroyed: it is filled during
// the run and read at process exit (after main returns) by the digest
// emitter, and a function-local static constructed on first use would
// die before the emitter runs. The leak is one vector of small
// strings, freed by the OS at exit.
std::vector<CaseDigest>& CaseRegistry() {
    static std::vector<CaseDigest>* registry = new std::vector<CaseDigest>();
    return *registry;
}

struct DigestEmitter {
    ~DigestEmitter() {
        const char* out = std::getenv("MDI_TRANSCRIPT_OUT");
        if (out == nullptr || *out == '\0') return;
        std::ofstream file(out);
        if (!file) return;
        file << "{\"cases\":[";
        bool first = true;
        for (const CaseDigest& digest : CaseRegistry()) {
            if (!first) file << ",";
            first = false;
            file << "{\"name\":\"" << digest.name << "\""
                  << ",\"passed\":" << (digest.passed ? "true" : "false")
                  << ",\"undo_depth\":" << digest.undo_depth
                  << ",\"text_hash\":\"" << digest.text_hash << "\"}";
        }
        file << "]}";
    }
};

DigestEmitter g_digest_emitter;

// ---------------------------------------------------------------------------
// The transcript runner.
// ---------------------------------------------------------------------------

std::string LoadFixture(const std::string& name, std::string* error) {
    const std::string path =
        "tests/transcripts/inline_formatting/" + name + ".json";
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        *error = "cannot open " + path +
                 " (the test binary runs from the repo root)";
        return std::string();
    }
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

struct CaseResult {
    bool ok = false;
    std::string name;
    std::string text;
    std::string observed;  // what actually happened, when a case fails
    int undo_depth = 0;
};

// C escapes for the observed print, so a failing transcript shows its
// actual buffer text on one line.
std::string CEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            default:   out += c; break;
        }
    }
    return out;
}

// Replays one transcript against a fresh controller. The parse service
// mirrors the ports: every document change notification re-parses the
// buffer into the controller's document, because the format state
// query and the clear format engine read the parse.
CaseResult RunTranscript(const std::string& name) {
    CaseResult result;
    result.ok = false;
    result.name = name;

    std::string error;
    const std::string raw = LoadFixture(name, &error);
    if (!error.empty()) {
        result.observed = error;
        return result;
    }
    JsonReader reader(raw);
    const Json fixture = reader.Read();
    if (reader.Failed() || fixture.kind != Json::Obj) {
        result.observed = "fixture is not a JSON object";
        return result;
    }
    if (StrOf(fixture, "name") != name) {
        result.observed = "fixture name does not match its file name";
        return result;
    }

    DocumentController controller;
    controller.SetEditing(true);
    controller.buffer().SetText(StrOf(fixture, "start_text"));
    controller.SetDocumentChangedHandler([&controller] {
        ParseMarkdown(controller.buffer().Text(), controller.document());
    });
    // The start text is what a reparse answers for before any command.
    ParseMarkdown(controller.buffer().Text(), controller.document());

    const Json* commands = Find(fixture, "commands");
    if (commands == nullptr || commands->kind != Json::Arr) {
        result.observed = "fixture has no command list";
        return result;
    }

    for (const Json& command : commands->items) {
        if (command.kind != Json::Obj) {
            result.observed = "a command is not an object";
            return result;
        }
        const std::string cmd = StrOf(command, "cmd");
        if (cmd == "select") {
            const int start = IntOf(command, "start", 0);
            const int end = IntOf(command, "end", 0);
            controller.selection().anchor = CaretPos{static_cast<uint32_t>(start)};
            controller.selection().active = CaretPos{static_cast<uint32_t>(end)};
            controller.ClearPendingFormat();
            controller.editor().BreakUndoCoalesce();
        } else if (cmd == "set_caret") {
            const int offset = IntOf(command, "offset", 0);
            controller.selection().anchor = CaretPos{static_cast<uint32_t>(offset)};
            controller.selection().active = CaretPos{static_cast<uint32_t>(offset)};
            controller.ClearPendingFormat();
            controller.editor().BreakUndoCoalesce();
        } else if (cmd == "type") {
            controller.ApplyPendingTypingFormat(StrOf(command, "text"));
        } else if (cmd == "toggle_bold") {
            controller.ToggleBold();
        } else if (cmd == "toggle_italic") {
            controller.ToggleItalic();
        } else if (cmd == "toggle_strike") {
            controller.ToggleStrike();
        } else if (cmd == "toggle_code") {
            controller.ToggleCode();
        } else if (cmd == "toggle_inline_link") {
            controller.ToggleInlineLink(StrOf(command, "url"));
        } else if (cmd == "clear_format") {
            controller.ClearFormat();
        } else if (cmd == "undo") {
            UndoEntry entry{};
            controller.editor().Undo(&entry);
            controller.RestorePendingFormat(entry, false);
        } else if (cmd == "redo") {
            UndoEntry entry{};
            controller.editor().Redo(&entry);
            controller.RestorePendingFormat(entry, true);
        } else if (cmd == "set_heading") {
            controller.SetHeading(IntOf(command, "level", 0));
        } else if (cmd == "toggle_bullets") {
            controller.ToggleBullets();
        } else if (cmd == "toggle_numbering") {
            controller.ToggleNumbering();
        } else if (cmd == "toggle_quote") {
            controller.ToggleQuote();
        } else if (cmd == "indent") {
            controller.Indent();
        } else if (cmd == "outdent") {
            controller.Outdent();
        } else if (cmd == "replace_match") {
            controller.ReplaceMatch(
                static_cast<uint32_t>(IntOf(command, "offset", 0)),
                static_cast<uint32_t>(IntOf(command, "length", 0)),
                StrOf(command, "text"));
        } else if (cmd == "replace_all_text") {
            controller.ReplaceAllText(StrOf(command, "text"));
        } else if (cmd == "add_row") {
            controller.AddTableRow(IntOf(command, "below", 1) != 0);
        } else if (cmd == "remove_row") {
            controller.RemoveTableRow();
        } else if (cmd == "add_column") {
            controller.AddTableColumn(IntOf(command, "right", 1) != 0);
        } else if (cmd == "remove_column") {
            controller.RemoveTableColumn();
        } else if (cmd == "align_column") {
            const int mark = IntOf(command, "mark", 0);
            controller.SetTableColumnAlign(
                mark == 1 ? TableAlignMark::Left
                : mark == 2 ? TableAlignMark::Center
                : mark == 3 ? TableAlignMark::Right : TableAlignMark::None);
        } else if (cmd == "split_cell") {
            controller.SplitTableCell();
        } else if (cmd == "remove_table") {
            controller.RemoveTable();
        } else if (cmd == "insert_table") {
            controller.InsertTable(IntOf(command, "cols", 2),
                                   IntOf(command, "rows", 1));
        } else if (cmd == "append_row") {
            controller.AppendRowToTable();
        } else {
            result.observed = "unknown command " + cmd;
            return result;
        }
    }

    result.text = controller.buffer().Text();
    // The selection is read before the undo depth count: every undo
    // step restores its own selBefore, which would otherwise leave the
    // comparison looking at the oldest entry's selection.
    const Selection finalSel = controller.selection();

    // The undo depth after the command list: undo until the stack is
    // empty and count the steps. The count is deterministic: entries
    // merge only when pushed, never while being undone.
    while (controller.undo().CanUndo()) {
        UndoEntry entry{};
        controller.editor().Undo(&entry);
        controller.editor().BreakUndoCoalesce();
        ++result.undo_depth;
    }

    // Expected text.
    const Json* expectedText = Find(fixture, "expected_text");
    if (expectedText == nullptr || expectedText->kind != Json::Str) {
        result.observed = "fixture has no expected_text";
        return result;
    }
    if (result.text != expectedText->str) {
        std::ostringstream note;
        note << "text is \"" << CEscape(result.text) << "\"";
        result.observed = note.str();
        return result;
    }

    // Expected selection.
    const Json* expectedSel = Find(fixture, "expected_selection");
    if (expectedSel == nullptr || expectedSel->kind != Json::Obj) {
        result.observed = "fixture has no expected_selection";
        return result;
    }
    const Selection& sel = finalSel;
    const int anchor = IntOf(*expectedSel, "anchor", -1);
    const int active = IntOf(*expectedSel, "active", -1);
    if (static_cast<int>(sel.anchor.offset) != anchor ||
        static_cast<int>(sel.active.offset) != active) {
        std::ostringstream note;
        note << "selection is anchor=" << sel.anchor.offset
             << " active=" << sel.active.offset;
        result.observed = note.str();
        return result;
    }

    // Expected undo depth.
    if (IntOf(fixture, "expected_undo_depth", -1) != result.undo_depth) {
        std::ostringstream note;
        note << "undo_depth is " << result.undo_depth;
        result.observed = note.str();
        return result;
    }

    result.ok = true;
    return result;
}

// One registered test per transcript file: the replay runs once, the
// digest the parity pipeline reads is taken from that same run, and a
// failure reports what the replay actually produced.
void RunCase(const char* name) {
    const CaseResult result = RunTranscript(name);

    CaseDigest digest;
    digest.name = name;
    digest.passed = result.ok;
    digest.undo_depth = result.undo_depth;
    digest.text_hash = Sha256(result.text).Hex();
    CaseRegistry().push_back(digest);

    EXPECT_TRUE(result.ok);
    if (!result.ok) {
        std::cerr << "  OBSERVED transcript " << name << ": "
                  << result.observed << "\n";
    }
}

// SHA-256 itself never drifts silently: these digests are the FIPS
// 180-4 test vectors, and the parity pipeline compares the hashes of
// both platforms' final texts, so the hash has to be the real one.
TEST(InlineFormattingTranscript, Sha256TestVectors) {
    EXPECT_EQ(Sha256("").Hex(),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(Sha256("abc").Hex(),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

}  // namespace

TEST(InlineFormattingTranscript, BoldSingleRun) {
    RunCase("bold-single-run");
}

TEST(InlineFormattingTranscript, ItalicCaretPendingTyping) {
    RunCase("italic-caret-pending-typing");
}

TEST(InlineFormattingTranscript, StrikeThenUndo) {
    RunCase("strike-then-undo");
}

TEST(InlineFormattingTranscript, ClearFormatOverMultiRun) {
    RunCase("clear-format-over-multi-run");
}

TEST(InlineFormattingTranscript, BoldMultiRunToggleOnSharedSemantics) {
    RunCase("bold-multi-run-toggle-on-shared-semantics");
}

TEST(InlineFormattingTranscript, ReplaceInTableCellIsSanitized) {
    RunCase("replace-in-table-cell-is-sanitized");
}

TEST(InlineFormattingTranscript, ReplaceAllIsOneUndoStep) {
    RunCase("replace-all-is-one-undo-step");
}

TEST(InlineFormattingTranscript, ReplaceLastMatchKeepsTextSelected) {
    RunCase("replace-last-match-keeps-text-selected");
}

TEST(InlineFormattingTranscript, SplitCellRefusedOnHeader) {
    RunCase("table-split-refused-on-header");
}

TEST(InlineFormattingTranscript, RemoveTableKeepsCaretAtRemovalSite) {
    RunCase("table-remove-keeps-caret-at-removal-site");
}

TEST(InlineFormattingTranscript, RemoveTableLandsOnFollowingBlock) {
    RunCase("table-remove-lands-on-following-block");
}

TEST(InlineFormattingTranscript, InsertTableDoesNotNest) {
    RunCase("table-insert-does-not-nest");
}

TEST(InlineFormattingTranscript, InsertTableReplacesSelectionInOneStep) {
    RunCase("table-insert-replaces-selection");
}

TEST(InlineFormattingTranscript, AddRowIsOneUndoStep) {
    RunCase("table-add-row-is-one-undo-step");
}

TEST(InlineFormattingTranscript, AppendRowFromLastCell) {
    RunCase("table-append-row-from-last-cell");
}

TEST(InlineFormattingTranscript, RemoveLastColumnRefused) {
    RunCase("table-remove-last-column-refused");
}

TEST(InlineFormattingTranscript, AlignColumnIsOneUndoStep) {
    RunCase("table-align-column-is-one-undo-step");
}
