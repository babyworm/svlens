#include <catch2/catch_test_macros.hpp>
#include "TestHelpersCdc.h"
#include "sv-cdccheck/clock_tree.h"
#include "sv-cdccheck/crossing_detector.h"

#include <algorithm>
#include <filesystem>

using namespace sv_cdccheck;

TEST_CASE("CDC ClockTreeAnalyzer: clock/reset name classification", "[cdc][clock_tree]") {
    CHECK(ClockTreeAnalyzer::isClockName("clk"));
    CHECK(ClockTreeAnalyzer::isClockName("sys_clk"));
    CHECK(ClockTreeAnalyzer::isClockName("core_clock"));
    CHECK_FALSE(ClockTreeAnalyzer::isClockName("data_in"));

    CHECK(ClockTreeAnalyzer::isResetName("rst_n"));
    CHECK(ClockTreeAnalyzer::isResetName("sys_reset"));
    CHECK_FALSE(ClockTreeAnalyzer::isResetName("clk"));
}

TEST_CASE("CDC ClockTreeAnalyzer: reset mux and indexed struct route stay nontransparent",
          "[cdc][clock_tree][reset][mux]") {
    connect::CompilationSession session;
    std::vector<std::string> args = {"test",
                                     (std::filesystem::path(TEST_CDC_DIR) / "conditional_reset_mux.sv").string()};
    REQUIRE(session.compile(args));

    ClockDatabase db;
    ClockTreeAnalyzer analyzer(session.compilation(), db);
    analyzer.analyze();

    const std::string top = "conditional_reset_mux_top.";
    const auto mux = db.reset_mux_inputs.find(top + "u_source.u_mux.clk_o");
    REQUIRE(mux != db.reset_mux_inputs.end());
    CHECK(mux->second.input0 == top + "u_source.u_mux.clk0_i");
    CHECK(mux->second.input1 == top + "u_source.u_mux.clk1_i");
    CHECK(mux->second.select == top + "u_source.u_mux.sel_i");

    const auto selected = db.reset_selected_aliases.find(top + "u_source.u_mux.clk_o");
    REQUIRE(selected != db.reset_selected_aliases.end());
    CHECK(std::find(selected->second.begin(), selected->second.end(), top + "u_source.resets_o.rst_sys_n[0]") !=
          selected->second.end());

    const auto field = db.reset_indexed_field_aliases.find(top + "u_source.resets_o.rst_sys_n");
    REQUIRE(field != db.reset_indexed_field_aliases.end());
    CHECK(std::find(field->second.begin(), field->second.end(), top + "resets.rst_sys_n") != field->second.end());
    CHECK_FALSE(db.directed_aliases.contains(top + "u_source.u_mux.clk_o"));

    const auto inverted = db.reset_inversions.find("conditional_reset_mux_computed_top.u_source.rst_sync_n");
    REQUIRE(inverted != db.reset_inversions.end());
    CHECK(std::find(inverted->second.begin(), inverted->second.end(),
                    "conditional_reset_mux_computed_top.u_source.u_mux.clk0_i") != inverted->second.end());
    CHECK_FALSE(db.reset_inversions.contains("conditional_reset_mux_wide_inversion_top.u_source.rst_pair"));
}

TEST_CASE("CDC ClockTreeAnalyzer: width-changing casts do not become clock aliases", "[cdc][clock_tree][alias]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module width_cast_clock_top(input logic [3:0] data_i, input bit data_bit, input logic src_clk,
                                    output logic q_o);
            logic cast_clk, state_cast_clk, alias_clk;
            assign cast_clk = 1'(data_i);
            assign state_cast_clk = logic'(data_bit);
            assign alias_clk = 1'(src_clk);
            always_ff @(posedge alias_clk) q_o <= cast_clk;
        endmodule
    )",
                                                    "width_cast_clock_top");
    REQUIRE(compiled);

    ClockDatabase db;
    ClockTreeAnalyzer analyzer(*compiled.compilation, db);
    analyzer.analyze();
    CHECK_FALSE(db.directed_aliases.contains("width_cast_clock_top.data_i"));
    CHECK_FALSE(db.directed_aliases.contains("width_cast_clock_top.data_bit"));
    const auto alias = db.directed_aliases.find("width_cast_clock_top.src_clk");
    REQUIRE(alias != db.directed_aliases.end());
    CHECK(std::find(alias->second.begin(), alias->second.end(), "width_cast_clock_top.alias_clk") !=
          alias->second.end());
}

TEST_CASE("CDC ClockTreeAnalyzer: struct-member clock connection keeps one source", "[cdc][clock_tree][member]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module bundle_clock_child(input logic clk_i, d_i, output logic q_o);
            always_ff @(posedge clk_i) q_o <= d_i;
        endmodule
        module bundle_clock_top(input logic src_clk, d_i, output logic q_a, q_b);
            typedef struct packed { logic clk_proc_main; } clocks_t;
            clocks_t clocks;
            assign clocks.clk_proc_main = src_clk;
            bundle_clock_child u_a(.clk_i(clocks.clk_proc_main), .d_i(d_i), .q_o(q_a));
            bundle_clock_child u_b(.clk_i(clocks.clk_proc_main), .d_i(d_i), .q_o(q_b));
        endmodule
    )",
                                                    "bundle_clock_top");
    REQUIRE(compiled);

    ClockDatabase db;
    ClockTreeAnalyzer analyzer(*compiled.compilation, db);
    analyzer.analyze();

    auto a = db.net_by_path.find("bundle_clock_top.u_a.clk_i");
    auto b = db.net_by_path.find("bundle_clock_top.u_b.clk_i");
    REQUIRE(a != db.net_by_path.end());
    REQUIRE(b != db.net_by_path.end());
    CHECK(a->second->source == b->second->source);
    CHECK(a->second->source->origin_signal == "src_clk");
}

TEST_CASE("CDC ClockTreeAnalyzer: transparent buffer and struct output keep top clock lineage",
          "[cdc][clock_tree][lineage]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        package lineage_pkg;
            typedef struct packed { logic clk_run; logic clk_muxed; } clocks_t;
        endpackage
        module lineage_buf(input logic clk_i, output logic clk_o);
            assign clk_o = clk_i;
        endmodule
        module lineage_box(input logic clk_main_i, clk_alt_i,
                           output lineage_pkg::clocks_t clocks_o);
            logic clk_mid;
            lineage_buf u_buf(.clk_i(clk_main_i), .clk_o(clk_mid));
            assign clocks_o.clk_run = clk_mid;
            assign clocks_o.clk_muxed = clk_alt_i ? clk_main_i : clk_alt_i;
        endmodule
        module lineage_sink(input logic clk_i, d_i, output logic q_o);
            always_ff @(posedge clk_i) q_o <= d_i;
        endmodule
        module lineage_top(input logic clk_main_i, clk_alt_i, d_i,
                           output logic q_o);
            lineage_pkg::clocks_t clocks;
            lineage_sink u_before(.clk_i(clocks.clk_run), .d_i(d_i), .q_o(q_o));
            lineage_box u_box(.clk_main_i(clk_main_i), .clk_alt_i(clk_alt_i),
                              .clocks_o(clocks));
        endmodule
    )",
                                                    "lineage_top");
    REQUIRE(compiled);

    ClockDatabase db;
    ClockTreeAnalyzer analyzer(*compiled.compilation, db);
    analyzer.analyze();

    ClockSource* top_clock = nullptr;
    for (const auto& source : db.sources)
        if (source->origin_signal == "clk_main_i")
            top_clock = source.get();
    REQUIRE(top_clock);
    auto sink = db.net_by_path.find("lineage_top.u_before.clk_i");
    REQUIRE(sink != db.net_by_path.end());
    CHECK(sink->second->source == top_clock);
    CHECK(db.root_by_path.at("lineage_top.clocks.clk_run") == top_clock);
    auto muxed = db.net_by_path.find("lineage_top.clocks.clk_muxed");
    CHECK((muxed == db.net_by_path.end() || muxed->second->source != top_clock));
    CHECK_FALSE(db.root_by_path.contains("lineage_top.clocks.clk_muxed"));
}

TEST_CASE("CDC ClockTreeAnalyzer: generated aliases preserve distinct top-clock roots",
          "[cdc][clock_tree][lineage][generate]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module generated_root_sink(input logic clk_i, d_i, output logic q_o);
            always_ff @(posedge clk_i) q_o <= d_i;
        endmodule
        module generated_root_top(input logic clk_main_i, clk_io_i, d_i,
                                  output logic q_main_o, q_io_o);
            for (genvar lane = 0; lane < 2; lane++) begin : gen_lanes
                logic clk_local;
                if (lane == 0) begin : select_main
                    assign clk_local = clk_main_i;
                    generated_root_sink u_sink(.clk_i(clk_local), .d_i(d_i), .q_o(q_main_o));
                end else begin : select_io
                    assign clk_local = clk_io_i;
                    generated_root_sink u_sink(.clk_i(clk_local), .d_i(d_i), .q_o(q_io_o));
                end
            end
            if (0) begin : gen_dead
                logic clk_ghost;
                assign clk_ghost = clk_main_i;
                generated_root_sink u_dead(.clk_i(clk_ghost), .d_i(d_i), .q_o());
            end
        endmodule
    )",
                                                    "generated_root_top");
    REQUIRE(compiled);

    ClockDatabase db;
    ClockTreeAnalyzer analyzer(*compiled.compilation, db);
    analyzer.analyze();

    ClockSource* mainClock = nullptr;
    ClockSource* ioClock = nullptr;
    for (const auto& source : db.sources) {
        if (source->origin_signal == "clk_main_i")
            mainClock = source.get();
        if (source->origin_signal == "clk_io_i")
            ioClock = source.get();
    }
    REQUIRE(mainClock);
    REQUIRE(ioClock);
    REQUIRE(db.root_by_path.contains("generated_root_top.gen_lanes[0].clk_local"));
    REQUIRE(db.root_by_path.contains("generated_root_top.gen_lanes[1].clk_local"));
    CHECK(db.root_by_path.at("generated_root_top.gen_lanes[0].clk_local") == mainClock);
    CHECK(db.root_by_path.at("generated_root_top.gen_lanes[1].clk_local") == ioClock);
    bool mainSink = false;
    bool ioSink = false;
    for (const auto& [path, root] : db.root_by_path) {
        if (!path.ends_with(".u_sink.clk_i"))
            continue;
        auto net = db.net_by_path.find(path);
        REQUIRE(net != db.net_by_path.end());
        CHECK(net->second->source == root);
        mainSink |= root == mainClock;
        ioSink |= root == ioClock;
    }
    CHECK(mainSink);
    CHECK(ioSink);
    CHECK_FALSE(db.root_by_path.contains("generated_root_top.gen_dead.clk_ghost"));
}

TEST_CASE("CDC ClockTreeAnalyzer: generated SDC periods follow forward master chains",
          "[cdc][clock_tree][sdc][timing]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module generated_period_top(input logic clk_fast);
        endmodule
    )",
                                                    "generated_period_top");
    REQUIRE(compiled);

    SdcConstraints sdc;
    sdc.clocks.push_back({"fast", 10.0, "clk_fast"});
    sdc.generated_clocks.push_back({"div4", "div2", "u_div4/clk_o", 2, 1, false});
    sdc.generated_clocks.push_back({"div2", "fast", "u_div2/clk_o", 2, 1, false});
    sdc.generated_clocks.push_back({"mul2", "fast", "u_mul2/clk_o", 1, 2, false});
    sdc.generated_clocks.push_back({"bad_ratio", "fast", "u_bad/clk_o", 0, 1, false});
    sdc.generated_clocks.push_back({"child_bad", "bad_ratio", "u_child_bad/clk_o", 2, 1, false});
    sdc.generated_clocks.push_back({"cycle_a", "cycle_b", "u_ca/clk_o", 2, 1, false});
    sdc.generated_clocks.push_back({"cycle_b", "cycle_a", "u_cb/clk_o", 2, 1, false});

    ClockDatabase db;
    ClockTreeAnalyzer analyzer(*compiled.compilation, db);
    analyzer.loadSdc(sdc);
    analyzer.analyze();
    CHECK(analyzer.skippedSdcRelationshipGroups() == 0);
    auto findSource = [&](std::string_view name) -> ClockSource* {
        for (const auto& source : db.sources)
            if (source->name == name)
                return source.get();
        return nullptr;
    };
    auto* fast = findSource("fast");
    auto* div2 = findSource("div2");
    auto* div4 = findSource("div4");
    auto* mul2 = findSource("mul2");
    auto* bad = findSource("bad_ratio");
    auto* childBad = findSource("child_bad");
    auto* cycleA = findSource("cycle_a");
    auto* cycleB = findSource("cycle_b");
    REQUIRE(fast);
    REQUIRE(div2);
    REQUIRE(div4);
    REQUIRE(mul2);
    REQUIRE(bad);
    REQUIRE(childBad);
    REQUIRE(cycleA);
    REQUIRE(cycleB);
    CHECK(div2->master == fast);
    CHECK(div4->master == div2);
    REQUIRE(div2->period_ns);
    REQUIRE(div4->period_ns);
    REQUIRE(mul2->period_ns);
    CHECK(*div2->period_ns == 20.0);
    CHECK(*div4->period_ns == 40.0);
    CHECK(*mul2->period_ns == 5.0);
    CHECK_FALSE(bad->period_ns);
    CHECK(bad->master == nullptr);
    CHECK_FALSE(childBad->period_ns);
    CHECK(childBad->master == nullptr);
    CHECK(cycleA->master == nullptr);
    CHECK(cycleB->master == nullptr);

    FFNode fastFf;
    fastFf.hier_path = "generated_period_top.fast_q";
    fastFf.domain = db.findOrCreateDomain(fast, Edge::Posedge);
    FFNode slowFf;
    slowFf.hier_path = "generated_period_top.slow_q";
    slowFf.domain = db.findOrCreateDomain(div2, Edge::Posedge);
    std::vector<FFEdge> edges{{&fastFf, &slowFf}};
    CrossingDetector detector(edges, db);
    detector.analyze();
    const auto crossings = detector.getCrossings();
    REQUIRE(crossings.size() == 1);
    CHECK(crossings[0].category == ViolationCategory::Caution);
    CHECK(crossings[0].timing_basis_ns == 20.0);
    CHECK(crossings[0].recommendation.find("Ac_cdc08") != std::string::npos);
}

TEST_CASE("CDC ClockTreeAnalyzer: include_generated_clocks follows resolved master chains only",
          "[cdc][clock_tree][sdc][clock_groups]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module generated_group_top(input logic clk_a_i, clk_b_i);
        endmodule
    )",
                                                    "generated_group_top");
    REQUIRE(compiled);

    SdcConstraints sdc;
    sdc.clocks.push_back({"clk_a", 10.0, "clk_a_i"});
    sdc.clocks.push_back({"clk_b", 12.0, "clk_b_i"});
    sdc.generated_clocks.push_back({"div4", "div2", "u_div4/clk_o", 2, 1, false});
    sdc.generated_clocks.push_back({"div2", "clk_a", "u_div2/clk_o", 2, 1, false});
    sdc.generated_clocks.push_back({"orphan", "missing", "u_orphan/clk_o", 2, 1, false});
    SdcClockGroup group;
    group.type = SdcClockGroup::Type::Asynchronous;
    group.groups = {{"clk_a"}, {"clk_b"}};
    group.include_generated = {true, false};
    sdc.clock_groups.push_back(group);

    ClockDatabase db;
    ClockTreeAnalyzer analyzer(*compiled.compilation, db);
    analyzer.loadSdc(sdc);
    analyzer.analyze();
    CHECK(analyzer.skippedSdcRelationshipGroups() == 0);
    auto findSource = [&](std::string_view name) -> ClockSource* {
        for (const auto& source : db.sources)
            if (source->name == name)
                return source.get();
        return nullptr;
    };
    auto* clkA = findSource("clk_a");
    auto* clkB = findSource("clk_b");
    auto* div2 = findSource("div2");
    auto* div4 = findSource("div4");
    auto* orphan = findSource("orphan");
    REQUIRE(clkA);
    REQUIRE(clkB);
    REQUIRE(div2);
    REQUIRE(div4);
    REQUIRE(orphan);
    CHECK(div2->master == clkA);
    CHECK(div4->master == div2);
    CHECK(orphan->master == nullptr);
    const auto* domainB = db.findOrCreateDomain(clkB, Edge::Posedge);
    CHECK(db.isSdcDeclaredRelationship(db.findOrCreateDomain(clkA, Edge::Posedge), domainB));
    CHECK(db.isSdcDeclaredRelationship(db.findOrCreateDomain(div2, Edge::Posedge), domainB));
    CHECK(db.isSdcDeclaredRelationship(db.findOrCreateDomain(div4, Edge::Posedge), domainB));
    CHECK_FALSE(db.isSdcDeclaredRelationship(db.findOrCreateDomain(orphan, Edge::Posedge), domainB));

    sdc.clock_groups[0].include_generated = {false, false};
    ClockDatabase exactDb;
    ClockTreeAnalyzer exactAnalyzer(*compiled.compilation, exactDb);
    exactAnalyzer.loadSdc(sdc);
    exactAnalyzer.analyze();
    ClockSource* exactDiv2 = nullptr;
    ClockSource* exactB = nullptr;
    for (const auto& source : exactDb.sources) {
        if (source->name == "div2")
            exactDiv2 = source.get();
        if (source->name == "clk_b")
            exactB = source.get();
    }
    REQUIRE(exactDiv2);
    REQUIRE(exactB);
    CHECK_FALSE(exactDb.isSdcDeclaredRelationship(exactDb.findOrCreateDomain(exactDiv2, Edge::Posedge),
                                                  exactDb.findOrCreateDomain(exactB, Edge::Posedge)));
}

TEST_CASE("CDC ClockTreeAnalyzer: parsed SDC expands generated clocks across forward declarations",
          "[cdc][clock_tree][sdc][clock_groups]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module parsed_group_top(input logic clk_a_i, clk_b_i);
        endmodule
    )",
                                                    "parsed_group_top");
    REQUIRE(compiled);
    const auto sdc = SdcParser::parse(std::filesystem::path(TEST_CDC_DIR) / "generated_clock_groups.sdc");
    REQUIRE(sdc.clock_groups.size() == 1);
    REQUIRE(sdc.generated_clocks.size() == 2);
    REQUIRE(sdc.clock_groups[0].include_generated.size() == 2);
    CHECK(sdc.clock_groups[0].include_generated[0]);

    ClockDatabase db;
    ClockTreeAnalyzer analyzer(*compiled.compilation, db);
    analyzer.loadSdc(sdc);
    analyzer.analyze();
    CHECK(analyzer.skippedSdcRelationshipGroups() == 0);
    ClockSource* div4 = nullptr;
    ClockSource* clkB = nullptr;
    for (const auto& source : db.sources) {
        if (source->name == "div4")
            div4 = source.get();
        if (source->name == "clk_b")
            clkB = source.get();
    }
    REQUIRE(div4);
    REQUIRE(clkB);
    CHECK(db.isSdcDeclaredRelationship(db.findOrCreateDomain(div4, Edge::Posedge),
                                       db.findOrCreateDomain(clkB, Edge::Posedge)));
}

TEST_CASE("CDC ClockTreeAnalyzer: overlapping generated groups are not partially registered",
          "[cdc][clock_tree][sdc][clock_groups]") {
    auto compiled = testutils::cdc::compileInlineSV("module overlap_group_top; endmodule", "overlap_group_top");
    REQUIRE(compiled);
    SdcConstraints sdc;
    sdc.clocks.push_back({"clk_a", 10.0, "clk_a_i"});
    sdc.clocks.push_back({"clk_b", 12.0, "clk_b_i"});
    sdc.generated_clocks.push_back({"div2", "clk_a", "u_div2/clk_o", 2, 1, false});
    SdcClockGroup group;
    group.type = SdcClockGroup::Type::Asynchronous;
    group.groups = {{"clk_a"}, {"div2"}};
    group.include_generated = {true, false};
    sdc.clock_groups.push_back(group);

    ClockDatabase db;
    ClockTreeAnalyzer analyzer(*compiled.compilation, db);
    analyzer.loadSdc(sdc);
    analyzer.analyze();
    CHECK(analyzer.skippedSdcRelationshipGroups() == 1);
    ClockSource* clkA = nullptr;
    ClockSource* div2 = nullptr;
    for (const auto& source : db.sources) {
        if (source->name == "clk_a")
            clkA = source.get();
        if (source->name == "div2")
            div2 = source.get();
    }
    REQUIRE(clkA);
    REQUIRE(div2);
    CHECK_FALSE(db.isSdcDeclaredRelationship(db.findOrCreateDomain(clkA, Edge::Posedge),
                                             db.findOrCreateDomain(div2, Edge::Posedge)));
}

TEST_CASE("CDC ClockTreeAnalyzer: ambiguous clock names do not bind an SDC group",
          "[cdc][clock_tree][sdc][clock_groups]") {
    auto compiled = testutils::cdc::compileInlineSV("module ambiguous_group_top; endmodule", "ambiguous_group_top");
    REQUIRE(compiled);
    SdcConstraints sdc;
    sdc.clocks.push_back({"clk_a", 10.0, "clk_a_i"});
    sdc.clocks.push_back({"clk_a", 11.0, "clk_a_alt_i"});
    sdc.clocks.push_back({"clk_b", 12.0, "clk_b_i"});
    SdcClockGroup group;
    group.type = SdcClockGroup::Type::Asynchronous;
    group.groups = {{"clk_a"}, {"clk_b"}};
    sdc.clock_groups.push_back(group);

    ClockDatabase db;
    ClockTreeAnalyzer analyzer(*compiled.compilation, db);
    analyzer.loadSdc(sdc);
    analyzer.analyze();
    CHECK(analyzer.skippedSdcRelationshipGroups() == 1);
    ClockSource* clkB = nullptr;
    size_t ambiguousSources = 0;
    for (const auto& source : db.sources) {
        if (source->name == "clk_b")
            clkB = source.get();
        if (source->name == "clk_a")
            ++ambiguousSources;
    }
    REQUIRE(clkB);
    CHECK(ambiguousSources == 2);
    for (const auto& source : db.sources) {
        if (source->name == "clk_a")
            CHECK_FALSE(db.isSdcDeclaredRelationship(db.findOrCreateDomain(source.get(), Edge::Posedge),
                                                     db.findOrCreateDomain(clkB, Edge::Posedge)));
    }
}

TEST_CASE("CDC ClockTreeAnalyzer: sibling RTL dividers keep scoped masters and periods",
          "[cdc][clock_tree][divider][timing]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module divider_sink(input logic clk_i, d_i, output logic q_o);
            always_ff @(posedge clk_i) q_o <= d_i;
        endmodule
        module divider_lane(input logic clk_i, d_i, output logic q_o);
            logic clk_div2;
            always_ff @(posedge clk_i) clk_div2 <= ~clk_div2;
            divider_sink u_sink(.clk_i(clk_div2), .d_i(d_i), .q_o(q_o));
        endmodule
        module divider_pair_top(input logic clk_a_i, clk_b_i, d_i,
                                output logic q_a_o, q_b_o);
            divider_lane u_a(.clk_i(clk_a_i), .d_i(d_i), .q_o(q_a_o));
            divider_lane u_b(.clk_i(clk_b_i), .d_i(d_i), .q_o(q_b_o));
        endmodule
    )",
                                                    "divider_pair_top");
    REQUIRE(compiled);

    SdcConstraints sdc;
    sdc.clocks.push_back({"clk_a", 8.0, "clk_a_i"});
    sdc.clocks.push_back({"clk_b", 10.0, "clk_b_i"});
    ClockDatabase db;
    ClockTreeAnalyzer analyzer(*compiled.compilation, db);
    analyzer.loadSdc(sdc);
    analyzer.analyze();

    ClockSource* sourceA = nullptr;
    ClockSource* sourceB = nullptr;
    ClockSource* dividerA = nullptr;
    ClockSource* dividerB = nullptr;
    for (const auto& source : db.sources) {
        if (source->name == "clk_a")
            sourceA = source.get();
        if (source->name == "clk_b")
            sourceB = source.get();
        if (source->origin_signal == "divider_pair_top.u_a.clk_div2")
            dividerA = source.get();
        if (source->origin_signal == "divider_pair_top.u_b.clk_div2")
            dividerB = source.get();
    }
    REQUIRE(sourceA);
    REQUIRE(sourceB);
    REQUIRE(dividerA);
    REQUIRE(dividerB);
    CHECK(dividerA != dividerB);
    CHECK(dividerA->type == ClockSource::Type::Generated);
    CHECK(dividerB->type == ClockSource::Type::Generated);
    CHECK(dividerA->master == sourceA);
    CHECK(dividerB->master == sourceB);
    CHECK(db.net_by_path.at("divider_pair_top.u_a.clk_div2")->source == dividerA);
    CHECK(db.net_by_path.at("divider_pair_top.u_b.clk_div2")->source == dividerB);
    REQUIRE(dividerA->period_ns);
    REQUIRE(dividerB->period_ns);
    CHECK(*dividerA->period_ns == 16.0);
    CHECK(*dividerB->period_ns == 20.0);
    const auto relation = db.relationshipBetween(db.findOrCreateDomain(dividerA, Edge::Posedge),
                                                 db.findOrCreateDomain(dividerB, Edge::Posedge));
    REQUIRE(relation);
    CHECK(*relation == DomainRelationship::Type::Asynchronous);
}

TEST_CASE("CDC ClockTreeAnalyzer: enabled toggle is not a fixed-ratio divider", "[cdc][clock_tree][divider][timing]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module enabled_toggle_top(input logic clk_i, en_i, d_i, output logic q_o);
            logic clk_div2;
            always_ff @(posedge clk_i)
                if (en_i) clk_div2 <= ~clk_div2;
            always_ff @(posedge clk_div2) q_o <= d_i;
        endmodule
    )",
                                                    "enabled_toggle_top");
    REQUIRE(compiled);

    SdcConstraints sdc;
    sdc.clocks.push_back({"fast", 10.0, "clk_i"});
    ClockDatabase db;
    ClockTreeAnalyzer analyzer(*compiled.compilation, db);
    analyzer.loadSdc(sdc);
    analyzer.analyze();

    const auto net = db.net_by_path.find("enabled_toggle_top.clk_div2");
    REQUIRE(net != db.net_by_path.end());
    REQUIRE(net->second->source);
    CHECK_FALSE(net->second->source->period_ns);
    CHECK(net->second->source->master == nullptr);
}

TEST_CASE("CDC ClockTreeAnalyzer: reset-guarded enable is not a fixed-ratio divider",
          "[cdc][clock_tree][divider][timing]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module enabled_reset_toggle_top(input logic clk_i, rst_n, en_i, d_i, output logic q_o);
            logic clk_div2;
            always_ff @(posedge clk_i or negedge rst_n)
                if (!rst_n) clk_div2 <= 0;
                else if (en_i) clk_div2 <= ~clk_div2;
            always_ff @(posedge clk_div2) q_o <= d_i;
        endmodule
    )",
                                                    "enabled_reset_toggle_top");
    REQUIRE(compiled);

    SdcConstraints sdc;
    sdc.clocks.push_back({"fast", 10.0, "clk_i"});
    ClockDatabase db;
    ClockTreeAnalyzer analyzer(*compiled.compilation, db);
    analyzer.loadSdc(sdc);
    analyzer.analyze();

    const auto net = db.net_by_path.find("enabled_reset_toggle_top.clk_div2");
    REQUIRE(net != db.net_by_path.end());
    REQUIRE(net->second->source);
    CHECK_FALSE(net->second->source->period_ns);
    CHECK(net->second->source->master == nullptr);
}

TEST_CASE("CDC ClockTreeAnalyzer: reset branch must match reset event polarity", "[cdc][clock_tree][divider][timing]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module inverted_reset_guard_top(input logic clk_i, rst_n, d_i, output logic q_o);
            logic clk_div2;
            always_ff @(posedge clk_i or negedge rst_n)
                if (rst_n) clk_div2 <= 0;
                else clk_div2 <= ~clk_div2;
            always_ff @(posedge clk_div2) q_o <= d_i;
        endmodule
    )",
                                                    "inverted_reset_guard_top");
    REQUIRE(compiled);

    SdcConstraints sdc;
    sdc.clocks.push_back({"fast", 10.0, "clk_i"});
    ClockDatabase db;
    ClockTreeAnalyzer analyzer(*compiled.compilation, db);
    analyzer.loadSdc(sdc);
    analyzer.analyze();

    const auto net = db.net_by_path.find("inverted_reset_guard_top.clk_div2");
    REQUIRE(net != db.net_by_path.end());
    REQUIRE(net->second->source);
    CHECK_FALSE(net->second->source->period_ns);
    CHECK(net->second->source->master == nullptr);
}

TEST_CASE("CDC ClockTreeAnalyzer: gated clock keeps root provenance without merging domains",
          "[cdc][clock_tree][lineage]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module prim_clock_gating(input logic clk_i, en_i, output logic clk_o);
            assign clk_o = clk_i & en_i;
        endmodule
        module prim_clock_mux2(input logic clk0_i, clk1_i, sel_i,
                               output logic clk_o);
            assign clk_o = sel_i ? clk1_i : clk0_i;
        endmodule
        module gate_box(input logic clk_main_i, en_i, output logic clk_gated_o);
            prim_clock_gating u_gate(.clk_i(clk_main_i), .en_i(en_i),
                                     .clk_o(clk_gated_o));
        endmodule
        module gate_sink(input logic clk_i, d_i, output logic q_o);
            always_ff @(posedge clk_i) q_o <= d_i;
        endmodule
        module gate_top(input logic clk_main_i, clk_alt_i, en_i, d_i,
                        output logic q_o);
            logic clk_gated, clk_muxed;
            gate_sink u_sink(.clk_i(clk_gated), .d_i(d_i), .q_o(q_o));
            gate_box u_box(.clk_main_i(clk_main_i), .en_i(en_i),
                           .clk_gated_o(clk_gated));
            prim_clock_mux2 u_mux(.clk0_i(clk_main_i), .clk1_i(clk_alt_i),
                                  .sel_i(en_i), .clk_o(clk_muxed));
        endmodule
    )",
                                                    "gate_top");
    REQUIRE(compiled);

    ClockDatabase db;
    ClockTreeAnalyzer analyzer(*compiled.compilation, db);
    analyzer.analyze();

    ClockSource* top_clock = nullptr;
    for (const auto& source : db.sources)
        if (source->origin_signal == "clk_main_i")
            top_clock = source.get();
    REQUIRE(top_clock);
    CHECK(db.root_by_path.at("gate_top.clk_gated") == top_clock);
    auto sink = db.net_by_path.find("gate_top.u_sink.clk_i");
    REQUIRE(sink != db.net_by_path.end());
    CHECK(sink->second->source != top_clock);
    CHECK_FALSE(db.root_by_path.contains("gate_top.clk_muxed"));
}

TEST_CASE("CDC ClockTreeAnalyzer: gating marks only the exact output net", "[cdc][clock_tree][gate_scope]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module clk_gate(input logic clk_in, en, output logic clk_out);
            assign clk_out = clk_in & en;
        endmodule
        module gate_scope_top(input logic clk_main_i, clk_other_i, en,
                              output logic clk_gated);
            logic other_clk_gated;
            assign other_clk_gated = clk_other_i;
            clk_gate u_gate(.clk_in(clk_main_i), .en(en), .clk_out(clk_gated));
        endmodule
    )",
                                                    "gate_scope_top");
    REQUIRE(compiled);

    ClockDatabase db;
    ClockTreeAnalyzer analyzer(*compiled.compilation, db);
    analyzer.analyze();
    auto gated = db.net_by_path.find("gate_scope_top.clk_gated");
    auto other = db.net_by_path.find("gate_scope_top.other_clk_gated");
    REQUIRE(gated != db.net_by_path.end());
    REQUIRE(other != db.net_by_path.end());
    CHECK(gated->second->is_gated);
    CHECK_FALSE(other->second->is_gated);
}

TEST_CASE("CDC ClockTreeAnalyzer: grandchild generated clocks share root and are not async", "[cdc][clock_tree]") {
    // Build a trivial compilation (empty module) just to construct the analyzer
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module empty_mod(input logic clk);
        endmodule
    )", "cdc_root_chain");
    REQUIRE(compiled);

    ClockDatabase db;

    // Pre-populate 3 sources: pll (primary) -> div2 (generated) -> div4 (generated)
    auto pll = std::make_unique<ClockSource>();
    pll->id = "pll_out";
    pll->name = "pll_out";
    pll->type = ClockSource::Type::Primary;
    pll->origin_signal = "pll_out";
    auto* pll_ptr = db.addSource(std::move(pll));

    auto div2 = std::make_unique<ClockSource>();
    div2->id = "div2_clk";
    div2->name = "div2_clk";
    div2->type = ClockSource::Type::Generated;
    div2->origin_signal = "div2_clk";
    div2->master = pll_ptr;
    div2->divide_by = 2;
    auto* div2_ptr = db.addSource(std::move(div2));

    auto div4 = std::make_unique<ClockSource>();
    div4->id = "div4_clk";
    div4->name = "div4_clk";
    div4->type = ClockSource::Type::Generated;
    div4->origin_signal = "div4_clk";
    div4->master = div2_ptr;
    div4->divide_by = 2;
    db.addSource(std::move(div4));

    // Create domains for each source
    auto* domPll = db.findOrCreateDomain(pll_ptr, Edge::Posedge);
    auto* domDiv2 = db.findOrCreateDomain(div2_ptr, Edge::Posedge);
    auto* domDiv4 = db.findOrCreateDomain(db.sources[2].get(), Edge::Posedge);

    // Run analyzer — this will call inferRelationships() on the pre-populated sources
    ClockTreeAnalyzer analyzer(*compiled.compilation, db);
    analyzer.analyze();

    // All three should be related (Divided), not Asynchronous
    CHECK_FALSE(db.isAsynchronous(domPll, domDiv2));
    CHECK_FALSE(db.isAsynchronous(domPll, domDiv4));   // This was the bug: grandchild was wrongly async
    CHECK_FALSE(db.isAsynchronous(domDiv2, domDiv4));
}

TEST_CASE("CDC ClockTreeAnalyzer: auto-detects clock ports in single-domain design", "[cdc][clock_tree]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module single_domain(input logic clk, rst_n, input logic [7:0] data_in, output logic [7:0] data_out);
            logic [7:0] stage1;
            always_ff @(posedge clk or negedge rst_n) begin
                if (!rst_n) stage1 <= '0;
                else stage1 <= data_in;
            end
            assign data_out = stage1;
        endmodule
    )", "cdc_clock_tree");
    REQUIRE(compiled);

    ClockDatabase db;
    ClockTreeAnalyzer analyzer(*compiled.compilation, db);
    analyzer.analyze();

    bool foundClk = false;
    for (const auto& src : db.sources) {
        if (src->origin_signal == "clk")
            foundClk = true;
    }
    CHECK(foundClk);
}
