// The shared Office pipeline both ports call (batch 4, family 6). The
// round trip and the failure texts are the one oracle for what the Windows
// and macOS windows show.

#include "gtest_lite.h"
#include "office/office_session.h"

#include <string>

TEST(OfficeSession, WordRoundTripKeepsTheText) {
    const std::string md = "# Title\n\nHello **bold** world.\n";
    const office::ExportResult out = office::ExportWordBytes(md);
    ASSERT_TRUE(out.ok);
    ASSERT_TRUE(!out.bytes.empty());
    const office::ImportResult in = office::ImportWordBytes(out.bytes);
    ASSERT_TRUE(in.ok);
    ASSERT_TRUE(in.markdown.find("Title") != std::string::npos);
    ASSERT_TRUE(in.markdown.find("**bold**") != std::string::npos);
}

TEST(OfficeSession, GarbageImportFailsWithAHeadline) {
    const office::ImportResult in = office::ImportWordBytes("not a zip");
    ASSERT_TRUE(!in.ok);
    ASSERT_TRUE(in.message.rfind("The Word file could not be read.", 0) == 0);
    ASSERT_TRUE(in.markdown.empty());
}

TEST(OfficeSession, PdfExportReportsPages) {
    office::PdfExportOptions options;
    const office::ExportResult out =
        office::ExportPdfBytes("# Title\n\nBody.\n", options);
    ASSERT_TRUE(out.ok);
    ASSERT_TRUE(out.bytes.rfind("%PDF", 0) == 0);
    ASSERT_TRUE(out.pageCount >= 1);
}

TEST(OfficeSession, NotesTextAndDoneText) {
    office::CompatReport report;
    EXPECT_EQ(office::CompatNotesText(report), std::string());
    report.warnings.push_back({"Page break", "dropped"});
    report.warnings.push_back({"Underline", ""});
    EXPECT_EQ(office::CompatNotesText(report),
              std::string("- Page break: dropped\n- Underline\n"));
    const std::string done = office::ExportDoneText("/x/a.pdf", 3, report);
    ASSERT_TRUE(done.find("/x/a.pdf") != std::string::npos);
    ASSERT_TRUE(done.find("Pages: 3") != std::string::npos);
    ASSERT_TRUE(done.find("Compatibility notes:") != std::string::npos);
    EXPECT_EQ(office::ExportDoneText("/x/a.docx", 0, office::CompatReport()),
              std::string("The document was exported to:\n/x/a.docx"));
}
