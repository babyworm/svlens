#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "test_helpers.h"
#include "sv-cdccheck/clock_tree.h"
#include "sv-cdccheck/ff_classifier.h"
#include "sv-cdccheck/connectivity.h"
#include "sv-cdccheck/crossing_detector.h"
#include "sv-cdccheck/sync_verifier.h"
#include "sv-cdccheck/report_generator.h"
#include "slang/driver/Driver.h"

#include <fstream>
#include <filesystem>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace sv_cdccheck;
using Catch::Matchers::ContainsSubstring;

namespace {

auto compileSpyglassSV(const std::string& sv_code) {
    return sv_cdccheck::test::compileSV(sv_code, "test_spyglass");
}

struct SpyglassPipeline {
    ClockDatabase db;
    std::unique_ptr<FFClassifier> classifier;
    std::vector<FFEdge> edges;
    std::vector<CrossingReport> crossings;

    void run(slang::ast::Compilation& compilation) {
        ClockTreeAnalyzer clock_analyzer(compilation, db);
        clock_analyzer.analyze();

        classifier = std::make_unique<FFClassifier>(compilation, db);
        classifier->analyze();

        ConnectivityBuilder conn(compilation, classifier->getFFNodes());
        conn.analyze();
        edges = conn.getEdges();

        CrossingDetector detector(edges, db);
        detector.analyze();
        crossings = detector.getCrossings();

        SyncVerifier verifier(crossings, classifier->getFFNodes(), edges, &db);
        verifier.analyze();
    }
};

} // anonymous namespace

// ─── Test 1: Clock-as-data detection [Ac_cdc09] ───

TEST_CASE("SpyGlass Ac_cdc09: clock signal used as data input", "[spyglass][cdc09]") {
    auto compilation = compileSpyglassSV(R"(
        module clk_as_data (input logic clk_a, clk_b, rst_n);
            logic q;
            always_ff @(posedge clk_b or negedge rst_n)
                if (!rst_n) q <= 0;
                else q <= clk_a;  // clock used as data!
        endmodule
    )");

    SpyglassPipeline pipeline;
    pipeline.run(*compilation);

    bool found_cdc09 = false;
    for (auto& c : pipeline.crossings) {
        if (c.rule == "Ac_cdc09") {
            found_cdc09 = true;
            CHECK(c.category == ViolationCategory::Caution);
            CHECK(c.source_signal == "clk_as_data.clk_a");
            CHECK_THAT(c.recommendation, ContainsSubstring("Ac_cdc09"));
            CHECK_THAT(c.recommendation, ContainsSubstring("Clock signal used as data"));
        }
    }
    CHECK(found_cdc09);
}

TEST_CASE("SpyGlass Ac_cdc09: a sibling clock port must not taint a data port with the same name",
          "[spyglass][cdc09]") {
    auto compilation = compileSpyglassSV(R"(
        module clock_consumer(input logic d_i, payload_i, output logic q_o);
            always_ff @(posedge d_i) q_o <= payload_i;
        endmodule
        module data_consumer(input logic clk_i, d_i, output logic q_o);
            always_ff @(posedge clk_i) q_o <= d_i;
        endmodule
        module clock_data_scope(input logic clk_i, other_clk, payload_i,
                                output logic q_clock, q_data);
            clock_consumer u_clock(.d_i(other_clk), .payload_i(payload_i),
                                   .q_o(q_clock));
            data_consumer u_data(.clk_i(clk_i), .d_i(payload_i), .q_o(q_data));
        endmodule
    )");
    SpyglassPipeline pipeline;
    pipeline.run(*compilation);
    for (const auto& crossing : pipeline.crossings)
        CHECK_FALSE((crossing.rule == "Ac_cdc09" && crossing.dest_signal == "clock_data_scope.u_data.q_o"));
}

TEST_CASE("SpyGlass Ac_cdc09: propagated child clock used as data is still reported", "[spyglass][cdc09]") {
    auto compilation = compileSpyglassSV(R"(
        module clock_data_child(input logic clk_a, clk_b, output logic q_o);
            always_ff @(posedge clk_b) q_o <= clk_a;
        endmodule
        module nested_clock_as_data(input logic clk_a, clk_b, output logic q_o);
            clock_data_child u_child(.clk_a(clk_a), .clk_b(clk_b), .q_o(q_o));
        endmodule
    )");
    SpyglassPipeline pipeline;
    pipeline.run(*compilation);
    bool found = false;
    for (const auto& crossing : pipeline.crossings) {
        if (crossing.rule == "Ac_cdc09" && crossing.dest_signal == "nested_clock_as_data.u_child.q_o") {
            found = true;
            CHECK(crossing.source_signal == "nested_clock_as_data.u_child.clk_a");
        }
    }
    CHECK(found);
}

TEST_CASE("SpyGlass Ac_cdc09: clock connected to a differently named data port", "[spyglass][cdc09]") {
    auto compilation = compileSpyglassSV(R"(
        module renamed_clock_data_child(input logic sample_clk, data_i, output logic q_o);
            always_ff @(posedge sample_clk) q_o <= data_i;
        endmodule
        module renamed_clock_data_top(input logic clk_a, clk_b, output logic q_o);
            renamed_clock_data_child u_child(.sample_clk(clk_b), .data_i(clk_a), .q_o(q_o));
        endmodule
    )");
    SpyglassPipeline pipeline;
    pipeline.run(*compilation);
    bool found = false;
    for (const auto& crossing : pipeline.crossings)
        found |= crossing.rule == "Ac_cdc09" && crossing.source_signal == "renamed_clock_data_top.u_child.data_i" &&
                 crossing.dest_signal == "renamed_clock_data_top.u_child.q_o";
    CHECK(found);
}

TEST_CASE("SpyGlass Ac_cdc09: a data port shadows a parent clock name", "[spyglass][cdc09]") {
    auto compilation = compileSpyglassSV(R"(
        module shadow_child(input logic clk_b, clk_a, output logic q_o);
            always_ff @(posedge clk_b) q_o <= clk_a;
        endmodule
        module shadow_middle(input logic clk_a, clk_b, payload_i,
                             output logic q_o);
            shadow_child u_child(.clk_b(clk_b), .clk_a(payload_i), .q_o(q_o));
        endmodule
        module shadow_top(input logic clk_a, clk_b, payload_i,
                          output logic q_o);
            shadow_middle u_mid(.clk_a(clk_a), .clk_b(clk_b),
                                .payload_i(payload_i), .q_o(q_o));
        endmodule
    )");
    SpyglassPipeline pipeline;
    pipeline.run(*compilation);
    for (const auto& crossing : pipeline.crossings)
        CHECK_FALSE((crossing.rule == "Ac_cdc09" && crossing.dest_signal == "shadow_top.u_mid.u_child.q_o"));
}

// ─── Test 2: Data-as-clock detection [Ac_cdc10] ───
// Note: structural detection of data-as-clock is limited since the ff_classifier
// will auto-create clock sources for unknown signals. The existing multi-clock
// error check [Ac_cdc11 in ff_classifier] covers the sensitivity-list case.
// This test verifies that a non-clock-named signal used as clock produces
// a classification error or at least gets flagged.

TEST_CASE("SpyGlass Ac_cdc10: data used as clock naming check", "[spyglass][cdc10]") {
    auto compilation = compileSpyglassSV(R"(
        module data_as_clock (input logic data_sig, clk_b, rst_n, d);
            logic q_a, q_b;
            always_ff @(posedge data_sig or negedge rst_n)
                if (!rst_n) q_a <= 0;
                else q_a <= d;
            always_ff @(posedge clk_b or negedge rst_n)
                if (!rst_n) q_b <= 0;
                else q_b <= q_a;
        endmodule
    )");

    SpyglassPipeline pipeline;
    pipeline.run(*compilation);

    // The crossing should exist and have a non-standard clock naming note
    bool found_naming_issue = false;
    for (auto& c : pipeline.crossings) {
        if (c.recommendation.find("non-standard clock naming") != std::string::npos) {
            found_naming_issue = true;
        }
    }
    CHECK(found_naming_issue);
}

// ─── Test 3: Quasi-static signal [Ac_cdc12] ───

TEST_CASE("SpyGlass Ac_cdc12: quasi-static signal detection", "[spyglass][cdc12]") {
    auto compilation = compileSpyglassSV(R"(
        module quasi_static (input logic clk_a, clk_b, rst_n);
            logic cfg_mode;  // quasi-static naming
            logic sync1, sync2;
            always_ff @(posedge clk_a or negedge rst_n)
                if (!rst_n) cfg_mode <= 0;
                // cfg_mode never reassigned after reset -> quasi-static
            always_ff @(posedge clk_b or negedge rst_n)
                if (!rst_n) begin sync1 <= 0; sync2 <= 0; end
                else begin sync1 <= cfg_mode; sync2 <= sync1; end
        endmodule
    )");

    SpyglassPipeline pipeline;
    pipeline.run(*compilation);

    bool found_cdc12 = false;
    for (auto& c : pipeline.crossings) {
        if (c.rule == "Ac_cdc12") {
            found_cdc12 = true;
            CHECK(c.category == ViolationCategory::Info);
            CHECK_THAT(c.recommendation, ContainsSubstring("Ac_cdc12"));
            CHECK_THAT(c.recommendation, ContainsSubstring("quasi-static"));
        }
    }
    CHECK(found_cdc12);
}

// ─── Test 4: Multi-domain crossing [Ac_cdc11] ───

TEST_CASE("SpyGlass Ac_cdc11: signal crosses to multiple clock domains", "[spyglass][cdc11]") {
    auto compilation = compileSpyglassSV(R"(
        module multi_domain (input logic clk_a, clk_b, clk_c, rst_n, d);
            logic q_a, q_b, q_c;
            always_ff @(posedge clk_a or negedge rst_n)
                if (!rst_n) q_a <= 0; else q_a <= d;
            always_ff @(posedge clk_b or negedge rst_n)
                if (!rst_n) q_b <= 0; else q_b <= q_a;  // crosses to clk_b
            always_ff @(posedge clk_c or negedge rst_n)
                if (!rst_n) q_c <= 0; else q_c <= q_a;  // same signal crosses to clk_c
        endmodule
    )");

    SpyglassPipeline pipeline;
    pipeline.run(*compilation);

    // q_a crosses to both clk_b domain and clk_c domain
    // The Ac_cdc11 annotation is appended to existing recommendations
    int cdc11_count = 0;
    for (auto& c : pipeline.crossings) {
        if (c.recommendation.find("Ac_cdc11") != std::string::npos) {
            cdc11_count++;
            CHECK_THAT(c.recommendation, ContainsSubstring("multiple clock domains"));
        }
    }
    CHECK(cdc11_count >= 2);
}

// ─── Test 5: Existing violations contain SpyGlass rule references ───

TEST_CASE("SpyGlass rules: VIOLATION crossings contain Ac_cdc01", "[spyglass][rules]") {
    auto compilation = compileSpyglassSV(R"(
        module unsync_crossing (input logic clk_a, clk_b, rst_n, d);
            logic q_a, q_b;
            always_ff @(posedge clk_a or negedge rst_n)
                if (!rst_n) q_a <= 0; else q_a <= d;
            always_ff @(posedge clk_b or negedge rst_n)
                if (!rst_n) q_b <= 0; else q_b <= q_a;
        endmodule
    )");

    SpyglassPipeline pipeline;
    pipeline.run(*compilation);

    bool found_violation = false;
    for (auto& c : pipeline.crossings) {
        if (c.category == ViolationCategory::Violation) {
            found_violation = true;
            CHECK(c.rule == "Ac_cdc01");
            CHECK_THAT(c.recommendation, ContainsSubstring("[Ac_cdc01]"));
        }
    }
    CHECK(found_violation);
}

TEST_CASE("SpyGlass rules: CAUTION crossings contain rule IDs", "[spyglass][rules]") {
    auto compilation = compileSpyglassSV(R"(
        module related_clocks (input logic clk_a, rst_n, d);
            logic clk_b;
            logic q_a, q_b;
            // Divided clock
            always_ff @(posedge clk_a or negedge rst_n)
                if (!rst_n) clk_b <= 0;
                else clk_b <= ~clk_b;
            always_ff @(posedge clk_a or negedge rst_n)
                if (!rst_n) q_a <= 0; else q_a <= d;
            always_ff @(posedge clk_b or negedge rst_n)
                if (!rst_n) q_b <= 0; else q_b <= q_a;
        endmodule
    )");

    SpyglassPipeline pipeline;
    pipeline.run(*compilation);

    // Any crossing should have a rule reference in its recommendation
    for (auto& c : pipeline.crossings) {
        if (!c.recommendation.empty()) {
            bool has_rule_ref = (c.recommendation.find("[Ac_") != std::string::npos);
            CHECK(has_rule_ref);
        }
    }
}

// ─── Test 6: JSON output includes rule field ───

TEST_CASE("SpyGlass rules: JSON output includes rule field", "[spyglass][json]") {
    auto compilation = compileSpyglassSV(R"(
        module json_rule_test (input logic clk_a, clk_b, rst_n, d);
            logic q_a, q_b;
            always_ff @(posedge clk_a or negedge rst_n)
                if (!rst_n) q_a <= 0; else q_a <= d;
            always_ff @(posedge clk_b or negedge rst_n)
                if (!rst_n) q_b <= 0; else q_b <= q_a;
        endmodule
    )");

    SpyglassPipeline pipeline;
    pipeline.run(*compilation);

    AnalysisResult result;
    result.clock_db = std::move(pipeline.db);
    result.crossings = std::move(pipeline.crossings);
    result.ff_nodes = pipeline.classifier->releaseFFNodes();
    result.edges = std::move(pipeline.edges);

    auto json_path = fs::temp_directory_path() / "test_spyglass_rules.json";
    ReportGenerator report(result);
    report.generateJSON(json_path);

    std::ifstream ifs(json_path);
    std::string json_content((std::istreambuf_iterator<char>(ifs)),
                              std::istreambuf_iterator<char>());

    CHECK_THAT(json_content, ContainsSubstring("\"rule\""));
    CHECK_THAT(json_content, ContainsSubstring("Ac_cdc01"));
}

// ─── Test 7: Latch warning contains Ac_conv03 ───

TEST_CASE("SpyGlass Ac_conv03: latch warning contains rule ID", "[spyglass][conv03]") {
    auto compilation = compileSpyglassSV(R"(
        module has_latch (input logic clk, d);
            logic q;
            always_latch
                if (clk) q <= d;
        endmodule
    )");

    ClockDatabase db;
    ClockTreeAnalyzer clock_analyzer(*compilation, db);
    clock_analyzer.analyze();

    FFClassifier classifier(*compilation, db);
    classifier.analyze();

    REQUIRE(!classifier.getLatchWarnings().empty());
    CHECK_THAT(classifier.getLatchWarnings()[0].message,
               ContainsSubstring("[Ac_conv03]"));
}
