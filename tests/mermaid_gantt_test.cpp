#include "gtest_lite.h"
#include "../src/mermaid/gantt_parse.h"
#include "../src/mermaid/gantt_layout.h"

TEST(MermaidGantt, RejectsNonGantt) {
    EXPECT_TRUE(!mermaid::ParseGantt("flowchart TD\n  A --> B\n").error.empty());
}

TEST(MermaidGantt, BasicHeaderAndTask) {
    auto g = mermaid::ParseGantt(
        "gantt\n"
        "  title Demo\n"
        "  dateFormat DD/MM/YYYY\n"
        "  axisFormat %d/%m\n"
        "  section S1\n"
        "  Task One :t1, 01/03/2026, 5d\n");
    EXPECT_TRUE(g.error.empty());
    EXPECT_EQ(g.title, "Demo");
    ASSERT_EQ(g.sections.size(), 1u);
    EXPECT_EQ(g.sections[0].title, "S1");
    ASSERT_EQ(g.tasks.size(), 1u);
    EXPECT_EQ(g.tasks[0].id, "t1");
    EXPECT_EQ(g.tasks[0].label, "Task One");
    EXPECT_EQ(g.tasks[0].duration_days, 5);
}

TEST(MermaidGantt, MilestoneZeroDuration) {
    auto g = mermaid::ParseGantt(
        "gantt\n"
        "  dateFormat DD/MM/YYYY\n"
        "  section S\n"
        "  Kickoff :milestone, m1, 01/03/2026, 0d\n");
    ASSERT_EQ(g.tasks.size(), 1u);
    EXPECT_TRUE(g.tasks[0].milestone);
    EXPECT_EQ(g.tasks[0].duration_days, 0);
}

TEST(MermaidGantt, AfterReferenceResolvesStartDate) {
    auto g = mermaid::ParseGantt(
        "gantt\n"
        "  dateFormat DD/MM/YYYY\n"
        "  section S\n"
        "  First :f1, 01/03/2026, 10d\n"
        "  Second :s1, after f1, 3d\n");
    ASSERT_EQ(g.tasks.size(), 2u);
    EXPECT_TRUE(g.tasks[1].start_kind == mermaid::GanttStartKind::AfterRef);
    EXPECT_EQ(g.tasks[1].after_id, "f1");

    auto laid = mermaid::LayoutGantt(g);
    // f1 runs 01/03 .. 10/03 (exclusive end on day 11), s1 starts there.
    ASSERT_EQ(laid.bars.size(), 2u);
    EXPECT_TRUE(laid.bars[1].x > laid.bars[0].x);
}

TEST(MermaidGantt, CritTagRecognized) {
    auto g = mermaid::ParseGantt(
        "gantt\n"
        "  dateFormat DD/MM/YYYY\n"
        "  section S\n"
        "  Risky :crit, r1, 01/03/2026, 2d\n");
    ASSERT_EQ(g.tasks.size(), 1u);
    EXPECT_TRUE(g.tasks[0].crit);
}

// The full example from the user request: 16 tasks across 4 sections,
// several after-references, a mid-chain milestone and a crit task.
TEST(MermaidGantt, FullRecruitmentIntegrationExample) {
    auto g = mermaid::ParseGantt(
        "gantt\n"
        "title Rekrutteringsintegrationer - udviklingsplan\n"
        "dateFormat DD/MM/YYYY\n"
        "axisFormat %d/%m\n"
        "section Ferie og pauser\n"
        "Paaskepause 2026 (ingen udvikling) :crit, easter_break, 02/04/2026, 5d\n"
        "section IDM integration (IDM team)\n"
        "Afklaringsmoeder (krav og tidsplan) :milestone, idm_m1, 13/03/2026, 0d\n"
        "IDM udvikling foer paaskepause :idm_dev_1, after idm_m1, 19d\n"
        "IDM udvikling efter paaskepause :idm_dev_2, 07/04/2026, 1d\n"
        "IDM leverance klar :milestone, idm_m2, after idm_dev_2, 0d\n"
        "section AU IT udviklingsteam\n"
        "Opstart AU IT team :milestone, au_m1, 01/04/2026, 0d\n"
        "Opgave A del 1 (foer paaskepause) :au_a_1, 01/04/2026, 1d\n"
        "Opgave B del 1 (foer paaskepause) :au_b_1, 01/04/2026, 1d\n"
        "Opgave A del 2 (efter paaskepause) :au_a_2, 07/04/2026, 13d\n"
        "Opgave B del 2 (efter paaskepause) :au_b_2, 07/04/2026, 27d\n"
        "API-kontrakt for jobopslag stabil :milestone, au_m2, after au_a_2, 0d\n"
        "AU IT spor faerdigt :milestone, au_m3, after au_b_2, 0d\n"
        "section Ekstern Typo3 konsulent\n"
        "Tilgaengelighed/startvindue :milestone, ex_m1, 15/03/2026, 0d\n"
        "Foranalyse og extension-afklaring (DTO) :ex_pre, 15/03/2026, 10d\n"
        "Integration mod BusinessLogicService (resterende arbejde op til 200t) :ex_impl, after ex_pre, 42d\n"
        "Typo3 integration klar :milestone, ex_m2, after ex_impl, 0d\n");

    EXPECT_TRUE(g.error.empty());
    EXPECT_EQ(g.title, "Rekrutteringsintegrationer - udviklingsplan");
    ASSERT_EQ(g.sections.size(), 4u);
    ASSERT_EQ(g.tasks.size(), 16u);

    EXPECT_TRUE(g.tasks[0].crit);

    // idm_dev_1 starts after idm_m1 (a 0-day milestone), so its start date
    // equals idm_m1's date: 13/03/2026.
    const mermaid::GanttTask* idm_dev_1 = nullptr;
    for (const auto& t : g.tasks) if (t.id == "idm_dev_1") idm_dev_1 = &t;
    ASSERT_TRUE(idm_dev_1 != nullptr);
    EXPECT_TRUE(idm_dev_1->start_kind == mermaid::GanttStartKind::AfterRef);
    EXPECT_EQ(idm_dev_1->after_id, "idm_m1");

    int milestones = 0;
    for (const auto& t : g.tasks) if (t.milestone) ++milestones;
    EXPECT_EQ(milestones, 7);

    auto laid = mermaid::LayoutGantt(g);
    EXPECT_EQ(laid.bars.size(), g.tasks.size());
    EXPECT_EQ(laid.sections.size(), g.sections.size());
    EXPECT_TRUE(laid.width > 0.0);
}
