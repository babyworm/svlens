#include <catch2/catch_test_macros.hpp>
#include "TestHelpersCdc.h"
#include "sv-cdccheck/clock_tree.h"
#include "sv-cdccheck/ff_classifier.h"
#include "sv-cdccheck/connectivity.h"

using namespace sv_cdccheck;

TEST_CASE("CDC FFClassifier: detects FFs in two-clock design", "[cdc][ff]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module two_clk(input logic clk_a, clk_b, rst_n, d);
            logic q_a, q_b;
            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) q_a <= 1'b0; else q_a <= d;
            end
            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) q_b <= 1'b0; else q_b <= q_a;
            end
        endmodule
    )", "cdc_ff");
    REQUIRE(compiled);

    ClockDatabase db;
    ClockTreeAnalyzer clockAnalyzer(*compiled.compilation, db);
    clockAnalyzer.analyze();

    FFClassifier classifier(*compiled.compilation, db);
    classifier.analyze();

    const auto& ffs = classifier.getFFNodes();
    REQUIRE(ffs.size() >= 2);
    bool foundDifferent = false;
    for (size_t i = 0; i < ffs.size(); ++i) {
        for (size_t j = i + 1; j < ffs.size(); ++j) {
            if (ffs[i]->domain != ffs[j]->domain)
                foundDifferent = true;
        }
    }
    CHECK(foundDifferent);
}

TEST_CASE("CDC FFClassifier: always_comb does not create FFs", "[cdc][ff]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module no_ff(input logic a, b, output logic y);
            always_comb begin
                y = a & b;
            end
        endmodule
    )", "cdc_no_ff");
    REQUIRE(compiled);

    ClockDatabase db;
    ClockTreeAnalyzer clockAnalyzer(*compiled.compilation, db);
    clockAnalyzer.analyze();

    FFClassifier classifier(*compiled.compilation, db);
    classifier.analyze();

    CHECK(classifier.getFFNodes().empty());
}

TEST_CASE("CDC FFClassifier: packed struct member assignment keeps its FF path and width", "[cdc][ff][member]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        typedef struct packed {logic [1:0] data;} sample_t;
        module member_ff(input logic clk_i, input logic [1:0] d_i, output sample_t sample_o);
            always_ff @(posedge clk_i) sample_o.data <= d_i;
        endmodule
    )",
                                                    "member_ff");
    REQUIRE(compiled);

    ClockDatabase db;
    ClockTreeAnalyzer clockAnalyzer(*compiled.compilation, db);
    clockAnalyzer.analyze();
    FFClassifier classifier(*compiled.compilation, db);
    classifier.analyze();

    bool found = false;
    for (const auto& ff : classifier.getFFNodes()) {
        CHECK(ff->hier_path.find(".__always_ff_") == std::string::npos);
        if (ff->hier_path == "member_ff.sample_o.data") {
            found = true;
            CHECK(ff->width == 2);
        }
    }
    CHECK(found);
}

TEST_CASE("CDC FFClassifier: packed struct member read connects cross-domain FFs", "[cdc][ff][member]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        typedef struct packed {logic data;} sample_t;
        module member_crossing(input logic clk_a, clk_b, d_i, output logic q_o);
            sample_t bundle;
            always_ff @(posedge clk_a) bundle.data <= d_i;
            always_ff @(posedge clk_b) q_o <= bundle.data;
        endmodule
    )",
                                                    "member_crossing");
    REQUIRE(compiled);

    ClockDatabase db;
    ClockTreeAnalyzer clockAnalyzer(*compiled.compilation, db);
    clockAnalyzer.analyze();
    FFClassifier classifier(*compiled.compilation, db);
    classifier.analyze();
    ConnectivityBuilder connectivity(*compiled.compilation, classifier.getFFNodes());
    connectivity.analyze();

    bool found = false;
    for (const auto& edge : connectivity.getEdges())
        found |=
            edge.source->hier_path == "member_crossing.bundle.data" && edge.dest->hier_path == "member_crossing.q_o";
    CHECK(found);
}

TEST_CASE("CDC FFClassifier: scoped clock net wins over same-named global source", "[cdc][ff][clock_scope]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module scoped_clock_child(input logic clk_i, d_i, output logic q_o);
            always_ff @(posedge clk_i) q_o <= d_i;
        endmodule
        module scoped_clock_top(input logic clk_i, clk_alt, d_i,
                                output logic q_top, q_a, q_b);
            always_ff @(posedge clk_i) q_top <= d_i;
            scoped_clock_child u_a(.clk_i(clk_i), .d_i(d_i), .q_o(q_a));
            scoped_clock_child u_b(.clk_i(clk_alt), .d_i(d_i), .q_o(q_b));
        endmodule
    )",
                                                    "scoped_clock_top");
    REQUIRE(compiled);

    ClockDatabase db;
    ClockTreeAnalyzer clockAnalyzer(*compiled.compilation, db);
    clockAnalyzer.analyze();
    FFClassifier classifier(*compiled.compilation, db);
    classifier.analyze();

    const FFNode* a = nullptr;
    const FFNode* b = nullptr;
    for (const auto& ff : classifier.getFFNodes()) {
        if (ff->hier_path == "scoped_clock_top.u_a.q_o")
            a = ff.get();
        if (ff->hier_path == "scoped_clock_top.u_b.q_o")
            b = ff.get();
    }
    REQUIRE(a);
    REQUIRE(b);
    REQUIRE(a->domain);
    REQUIRE(b->domain);
    CHECK(a->domain != b->domain);
    CHECK(b->domain->source->origin_signal == "clk_alt");
}

TEST_CASE("CDC FFClassifier: sibling internal data clocks keep separate origins", "[cdc][ff][clock_scope]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module local_pulse(input logic clk_i, data_i, output logic sampled_o);
            logic pulse_q;
            always_ff @(posedge clk_i) pulse_q <= data_i;
            always_ff @(posedge pulse_q) sampled_o <= data_i;
        endmodule
        module sibling_pulses(input logic clk_a, clk_b, data_i,
                              output logic sampled_a, sampled_b);
            local_pulse u_a(.clk_i(clk_a), .data_i(data_i), .sampled_o(sampled_a));
            local_pulse u_b(.clk_i(clk_b), .data_i(data_i), .sampled_o(sampled_b));
        endmodule
    )",
                                                    "sibling_pulses");
    REQUIRE(compiled);

    ClockDatabase db;
    ClockTreeAnalyzer clockAnalyzer(*compiled.compilation, db);
    clockAnalyzer.analyze();
    FFClassifier classifier(*compiled.compilation, db);
    classifier.analyze();

    const FFNode* a = nullptr;
    const FFNode* b = nullptr;
    for (const auto& ff : classifier.getFFNodes()) {
        if (ff->hier_path == "sibling_pulses.u_a.sampled_o")
            a = ff.get();
        if (ff->hier_path == "sibling_pulses.u_b.sampled_o")
            b = ff.get();
    }
    REQUIRE(a);
    REQUIRE(b);
    REQUIRE(a->domain);
    REQUIRE(b->domain);
    CHECK(a->domain->source != b->domain->source);
    CHECK(a->domain->source->origin_signal == "sibling_pulses.u_a.pulse_q");
    CHECK(b->domain->source->origin_signal == "sibling_pulses.u_b.pulse_q");
}

TEST_CASE("CDC FFClassifier: sibling direct and generated FFs share a parent clock port", "[cdc][ff][clock_scope]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        typedef struct packed {logic cpu;} clocks_t;
        module direct_stage(input logic clk_i, d_i, output logic q_o);
            always_ff @(posedge clk_i) q_o <= d_i;
        endmodule
        module generated_stage(input logic clk_i, d_i, output logic q_o);
            if (1) begin : g_stage
                always_ff @(posedge clk_i) q_o <= d_i;
            end
        endmodule
        module core(input logic clk_i, d_i, output logic q_a, q_b);
            direct_stage u_a(.clk_i(clk_i), .d_i(d_i), .q_o(q_a));
            generated_stage u_b(.clk_i(clk_i), .d_i(d_i), .q_o(q_b));
        endmodule
        module clock_port_top(input clocks_t clks, input logic d_i,
                              output logic q_a, q_b);
            core u_core(.clk_i(clks.cpu), .d_i(d_i), .q_a(q_a), .q_b(q_b));
        endmodule
    )",
                                                    "clock_port_top");
    REQUIRE(compiled);

    ClockDatabase db;
    ClockTreeAnalyzer clockAnalyzer(*compiled.compilation, db);
    clockAnalyzer.analyze();
    FFClassifier classifier(*compiled.compilation, db);
    classifier.analyze();

    const FFNode* a = nullptr;
    const FFNode* b = nullptr;
    for (const auto& ff : classifier.getFFNodes()) {
        if (ff->hier_path == "clock_port_top.u_core.u_a.q_o")
            a = ff.get();
        if (ff->hier_path == "clock_port_top.u_core.u_b.g_stage.q_o")
            b = ff.get();
    }
    REQUIRE(a);
    REQUIRE(b);
    REQUIRE(a->domain);
    REQUIRE(b->domain);
    CHECK(a->domain == b->domain);
    CHECK(a->module_path == "clock_port_top.u_core.u_a");
    CHECK(b->module_path == "clock_port_top.u_core.u_b");
}

TEST_CASE("CDC FFClassifier: generated FF uses scoped clock before global namesake", "[cdc][ff][clock_scope]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module generated_clock_child(input logic clk_i, d_i, output logic q_o);
            if (1) begin : g_ff
                always_ff @(posedge clk_i) q_o <= d_i;
            end
        endmodule
        module generated_clock_top(input logic clk_i, clk_alt, d_i,
                                   output logic q_top, q_alt);
            always_ff @(posedge clk_i) q_top <= d_i;
            generated_clock_child u_alt(.clk_i(clk_alt), .d_i(d_i), .q_o(q_alt));
        endmodule
    )",
                                                    "generated_clock_top");
    REQUIRE(compiled);

    ClockDatabase db;
    ClockTreeAnalyzer clockAnalyzer(*compiled.compilation, db);
    clockAnalyzer.analyze();
    FFClassifier classifier(*compiled.compilation, db);
    classifier.analyze();

    const FFNode* alt = nullptr;
    for (const auto& ff : classifier.getFFNodes())
        if (ff->hier_path == "generated_clock_top.u_alt.g_ff.q_o")
            alt = ff.get();
    REQUIRE(alt);
    REQUIRE(alt->domain);
    CHECK(alt->domain->source->origin_signal == "clk_alt");
}

TEST_CASE("CDC FFClassifier: generated-clock declaration applies only to its scoped target", "[cdc][ff][clock_scope]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module declared_clock_child(input logic clk_i, d_i, output logic q_o);
            always_ff @(posedge clk_i) q_o <= d_i;
        endmodule
        module declared_clock_top(input logic clk_i, clk_alt, d_i,
                                  output logic q_decl, q_alt);
            declared_clock_child u_decl(.clk_i(clk_i), .d_i(d_i), .q_o(q_decl));
            declared_clock_child u_alt(.clk_i(clk_alt), .d_i(d_i), .q_o(q_alt));
        endmodule
    )",
                                                    "declared_clock_top");
    REQUIRE(compiled);

    ClockDatabase db;
    ClockTreeAnalyzer clockAnalyzer(*compiled.compilation, db);
    clockAnalyzer.analyze();
    ClockSource* main_clock = nullptr;
    for (const auto& source : db.sources)
        if (source->origin_signal == "clk_i")
            main_clock = source.get();
    REQUIRE(main_clock);
    auto generated = std::make_unique<ClockSource>();
    generated->type = ClockSource::Type::Generated;
    generated->name = "clk_i";
    generated->origin_signal = "declared_clock_top.u_decl.clk_i";
    generated->master = main_clock;
    auto* declared_clock = db.addSource(std::move(generated));

    FFClassifier classifier(*compiled.compilation, db);
    classifier.analyze();
    const FFNode* decl = nullptr;
    const FFNode* alt = nullptr;
    for (const auto& ff : classifier.getFFNodes()) {
        if (ff->hier_path == "declared_clock_top.u_decl.q_o")
            decl = ff.get();
        if (ff->hier_path == "declared_clock_top.u_alt.q_o")
            alt = ff.get();
    }
    REQUIRE(decl);
    REQUIRE(alt);
    REQUIRE(decl->domain);
    REQUIRE(alt->domain);
    CHECK(decl->domain->source == declared_clock);
    CHECK(alt->domain->source->origin_signal == "clk_alt");
}
