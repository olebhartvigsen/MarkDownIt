// Family 6: what a file on disk becomes in the editor and back. Both
// ports decode and encode through DocumentController::LoadFile and
// BytesForSaving, so these tests are the one oracle for the BOM, the
// line ends and the standalone .mmd and .svg fence (the mac side knew
// neither the BOM nor the .mmd fence before this family moved).

#include "documentcontroller.h"
#include "documentfile.h"
#include "gtest_lite.h"

#include <string>

namespace {
const std::string kBom = "\xEF\xBB\xBF";

std::string RoundTrip(const std::string& raw, const std::string& path) {
    DocumentController c;
    c.LoadFile(raw, path);
    return c.BytesForSaving();
}
}  // namespace

TEST(DocumentFile, BomIsStrippedOnLoadAndPutBackOnSave) {
    DocumentController c;
    c.LoadFile(kBom + "# Title\n", "a.md");
    EXPECT_EQ(c.buffer().Text(), std::string("# Title\n"));
    EXPECT_TRUE(c.fileFormat().hasBom);
    EXPECT_EQ(c.BytesForSaving(), kBom + "# Title\n");
}

TEST(DocumentFile, FileWithoutBomStaysWithoutOne) {
    EXPECT_EQ(RoundTrip("plain\n", "a.md"), std::string("plain\n"));
}

TEST(DocumentFile, CrlfFileKeepsItsLineEnds) {
    const std::string raw = "one\r\ntwo\r\n";
    DocumentController c;
    c.LoadFile(raw, "a.md");
    EXPECT_TRUE(c.fileFormat().crlf);
    EXPECT_EQ(c.BytesForSaving(), raw);
}

TEST(DocumentFile, LineEndTypedInALfFileStaysLf) {
    EXPECT_EQ(RoundTrip("one\ntwo\n", "a.md"), std::string("one\ntwo\n"));
}

TEST(DocumentFile, MixedEndsInACrlfFileAreSavedAsCrlf) {
    DocumentController c;
    c.LoadFile("one\r\ntwo\r\n", "a.md");
    c.buffer().SetText("one\r\ntwo\nthree\n");
    EXPECT_EQ(c.BytesForSaving(), std::string("one\r\ntwo\r\nthree\r\n"));
}

TEST(DocumentFile, MmdFileIsShownInsideAMermaidFence) {
    DocumentController c;
    c.LoadFile("graph TD\nA-->B\n", "flow.mmd");
    EXPECT_EQ(c.buffer().Text(),
              std::string("```mermaid\ngraph TD\nA-->B\n```\n"));
    EXPECT_TRUE(c.fileFormat().mmdWrapped);
    EXPECT_EQ(c.BytesForSaving(), std::string("graph TD\nA-->B\n"));
}

TEST(DocumentFile, ExtensionIsComparedInLowerCase) {
    DocumentController c;
    c.LoadFile("<svg/>\n", "DIAGRAM.SVG");
    EXPECT_TRUE(c.fileFormat().isSvg);
    EXPECT_TRUE(c.fileFormat().svgWrapped);
    EXPECT_EQ(c.BytesForSaving(), std::string("<svg/>\n"));
}

TEST(DocumentFile, UserFencedMmdFileKeepsItsFence) {
    const std::string raw = "```mermaid\ngraph TD\n```\n";
    DocumentController c;
    c.LoadFile(raw, "flow.mmd");
    EXPECT_FALSE(c.fileFormat().mmdWrapped);
    EXPECT_EQ(c.buffer().Text(), raw);
    EXPECT_EQ(c.BytesForSaving(), raw);
}

TEST(DocumentFile, SvgFenceInAMarkdownFileIsCodeNotAPicture) {
    const std::string raw = "```svg\n<svg/>\n```\n";
    DocumentController c;
    c.LoadFile(raw, "notes.md");
    EXPECT_FALSE(c.fileFormat().isSvg);
    EXPECT_EQ(c.buffer().Text(), raw);
    EXPECT_EQ(c.BytesForSaving(), raw);
}

TEST(DocumentFile, ImportedTextWithNoPathIsNeverFenced) {
    DocumentController c;
    c.LoadFile("graph TD\n", "");
    EXPECT_EQ(c.buffer().Text(), std::string("graph TD\n"));
    EXPECT_FALSE(c.fileFormat().isMmd);
}

TEST(DocumentFile, LoadClearsUndoAndPutsTheCaretAtTheTop) {
    DocumentController c;
    c.LoadFile("abc\n", "a.md");
    c.selection().Collapse(CaretPos{2});
    c.LoadFile("def\n", "b.md");
    EXPECT_EQ(c.selection().active.offset, 0u);
    EXPECT_FALSE(c.undo().CanUndo());
}

TEST(DocumentFile, ForgetFileDropsTheFormatOfTheOldFile) {
    DocumentController c;
    c.LoadFile(kBom + "a\r\nb\r\n", "a.md");
    c.ForgetFile();
    EXPECT_FALSE(c.fileFormat().hasBom);
    EXPECT_FALSE(c.fileFormat().crlf);
}
