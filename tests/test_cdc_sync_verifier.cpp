#include <catch2/catch_test_macros.hpp>
#include "TestHelpersCdc.h"
#include "sv-cdccheck/clock_tree.h"
#include "sv-cdccheck/ff_classifier.h"
#include "sv-cdccheck/connectivity.h"
#include "sv-cdccheck/crossing_detector.h"
#include "sv-cdccheck/sync_verifier.h"

using namespace sv_cdccheck;

struct FullPipeline {
    ClockDatabase db;
    std::unique_ptr<FFClassifier> classifier;
    std::vector<FFEdge> edges;
    std::vector<CrossingReport> crossings;

    void run(slang::ast::Compilation& compilation) {
        ClockTreeAnalyzer clockAnalyzer(compilation, db);
        clockAnalyzer.analyze();

        classifier = std::make_unique<FFClassifier>(compilation, db);
        classifier->analyze();

        ConnectivityBuilder connectivity(compilation, classifier->getFFNodes());
        connectivity.analyze();
        edges = connectivity.getEdges();

        CrossingDetector detector(edges, db);
        detector.analyze();
        crossings = detector.getCrossings();

        SyncVerifier verifier(crossings, classifier->getFFNodes(), edges);
        verifier.analyze();
    }
};

TEST_CASE("CDC SyncVerifier: unsynchronized crossing remains VIOLATION", "[cdc][sync]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module no_sync(input logic clk_a, clk_b, rst_n, d);
            logic q_a, q_b;
            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) q_a <= 1'b0; else q_a <= d;
            end
            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) q_b <= 1'b0; else q_b <= q_a;
            end
        endmodule
    )", "cdc_sync_none");
    REQUIRE(compiled);

    FullPipeline pipeline;
    pipeline.run(*compiled.compilation);

    REQUIRE(!pipeline.crossings.empty());
    CHECK(pipeline.crossings[0].sync_type == SyncType::None);
    CHECK(pipeline.crossings[0].category == ViolationCategory::Violation);
}

TEST_CASE("CDC SyncVerifier: 2-FF synchronizer is recognized", "[cdc][sync]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module two_ff_sync(input logic clk_a, clk_b, rst_n, d);
            logic q_a, sync1, sync2;
            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) q_a <= 1'b0; else q_a <= d;
            end
            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) begin sync1 <= 1'b0; sync2 <= 1'b0; end
                else begin sync1 <= q_a; sync2 <= sync1; end
            end
        endmodule
    )", "cdc_sync_twoff");
    REQUIRE(compiled);

    FullPipeline pipeline;
    pipeline.run(*compiled.compilation);

    bool foundTwoFF = false;
    for (const auto& crossing : pipeline.crossings) {
        if (crossing.sync_type == SyncType::TwoFF) {
            foundTwoFF = true;
            CHECK(crossing.category == ViolationCategory::Info);
        }
    }
    CHECK(foundTwoFF);
}

// Round 7 sync_verifier::findNextFF relaxation lock: the strict
// `source_leaf == fanin[0]` check was dropped so cross-instance
// 2-FF chains (where dest reads its local port `d_i` while source's
// leaf is the upstream instance's `q_o`) classify as TwoFF. The
// safety net is the conjunction of (1) `!has_comb_logic`, (2)
// FFEdge built via port-aware resolveToFFs, (3) `fanin.size() <= 1`.
// The next three test cases lock those three pillars individually.

TEST_CASE("CDC SyncVerifier: cross-instance 2-FF chain classifies as TwoFF",
          "[cdc][sync][cross_inst]") {
    // Pillar 1: positive case. Two flop_w submodule instances form
    // the sync chain. The dest's fanin is its local port name
    // `d_i`, which differs from the source's leaf `q_o`. The pre-
    // Round 7 strict name check rejected this edge; the relaxation
    // accepts it.
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module xinst (input logic ca, cb, rstn, d);
            logic q_a;
            always_ff @(posedge ca or negedge rstn)
                if (!rstn) q_a <= 1'b0; else q_a <= d;
            xinst_flop u1 (.clk(cb), .rstn, .d_i(q_a));
        endmodule
        module xinst_flop (input logic clk, rstn, d_i);
            logic s1, s2;
            always_ff @(posedge clk or negedge rstn)
                if (!rstn) s1 <= 1'b0; else s1 <= d_i;
            always_ff @(posedge clk or negedge rstn)
                if (!rstn) s2 <= 1'b0; else s2 <= s1;
        endmodule
    )", "cdc_xinst");
    REQUIRE(compiled);

    FullPipeline pipeline;
    pipeline.run(*compiled.compilation);

    bool foundTwoFF = false;
    for (const auto& c : pipeline.crossings)
        if (c.sync_type == SyncType::TwoFF) foundTwoFF = true;
    CHECK(foundTwoFF);
}

TEST_CASE("CDC SyncVerifier: comb logic before sync still blocks TwoFF",
          "[cdc][sync][comb_guard]") {
    // Pillar 2: even with the relaxed name check, an edge with
    // `has_comb_logic == true` (e.g., AND/XOR between two FFs)
    // must NOT graduate to TwoFF. The crossing should be a
    // CAUTION (comb-before-sync), not an INFO 2-FF chain.
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module comb_block (input logic ca, cb, rstn, d, e);
            logic q_a, mid, q_b;
            always_ff @(posedge ca or negedge rstn)
                if (!rstn) q_a <= 1'b0; else q_a <= d;
            assign mid = q_a & e; // combinational gate before sync
            always_ff @(posedge cb or negedge rstn)
                if (!rstn) q_b <= 1'b0; else q_b <= mid;
        endmodule
    )", "cdc_comb_guard");
    REQUIRE(compiled);

    FullPipeline pipeline;
    pipeline.run(*compiled.compilation);

    REQUIRE(!pipeline.crossings.empty());
    bool anyTwoFF = false;
    for (const auto& c : pipeline.crossings)
        if (c.sync_type == SyncType::TwoFF) anyTwoFF = true;
    CHECK_FALSE(anyTwoFF);
}

TEST_CASE("CDC SyncVerifier: same-instance pulse sync graduates to PulseSync",
          "[cdc][sync][pulse]") {
    // Lock the same-instance baseline: toggle in src domain → 2-FF
    // sync in dst domain → XOR(synced, synced_d1) → registered
    // output FF. The XOR + delayed copy is the textbook pulse sync
    // edge detector. The detector requires a REGISTERED consumer of
    // the XOR output (fanin >= 2 on a downstream FF).
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module pulse_si (input logic ca, cb, rstn, pulse_in);
            logic toggle_a, s1, s2, s3, pulse_q;
            always_ff @(posedge ca or negedge rstn)
                if (!rstn) toggle_a <= 1'b0;
                else if (pulse_in) toggle_a <= ~toggle_a;
            always_ff @(posedge cb or negedge rstn) begin
                if (!rstn) begin s1 <= 1'b0; s2 <= 1'b0; s3 <= 1'b0; end
                else begin s1 <= toggle_a; s2 <= s1; s3 <= s2; end
            end
            // Registered XOR consumer -- required for pulse detection
            always_ff @(posedge cb or negedge rstn)
                if (!rstn) pulse_q <= 1'b0;
                else       pulse_q <= s2 ^ s3;
        endmodule
    )", "cdc_pulse_si");
    REQUIRE(compiled);

    FullPipeline pipeline;
    pipeline.run(*compiled.compilation);

    bool foundPulse = false;
    for (const auto& c : pipeline.crossings)
        if (c.sync_type == SyncType::PulseSync) foundPulse = true;
    CHECK(foundPulse);
}

TEST_CASE("CDC SyncVerifier: pulse sync with renamed XOR inputs graduates to PulseSync",
          "[cdc][sync][pulse][rename]") {
    // RED test for sync_verifier.cpp:595 -- the previous strict
    // `f == last_leaf` name match in detectPulseSyncPattern broke
    // the moment the last sync stage's output got renamed via a
    // continuous assign before being consumed by the XOR. The
    // FFEdge graph still carries the structural truth (s3 feeds
    // pulse_q via the comb XOR), so the relaxation removes the
    // name match and trusts the FFEdge.
    //
    // Same module / same domain (no cross-instance clock unification
    // needed); the only added wrinkle is `assign synced_alias = s2;`
    // and `assign delayed_alias = s3;` which renames the sync
    // outputs before the XOR. pulse_q's fanin reads
    // [synced_alias, delayed_alias], NOT [s2, s3]. The
    // post-relaxation detector classifies this as PulseSync.
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module pulse_renamed (input logic ca, cb, rstn, pulse_in);
            logic toggle_a, s1, s2, s3;
            logic synced_alias, delayed_alias;
            logic pulse_q;
            always_ff @(posedge ca or negedge rstn)
                if (!rstn) toggle_a <= 1'b0;
                else if (pulse_in) toggle_a <= ~toggle_a;
            always_ff @(posedge cb or negedge rstn) begin
                if (!rstn) begin s1 <= 1'b0; s2 <= 1'b0; s3 <= 1'b0; end
                else begin s1 <= toggle_a; s2 <= s1; s3 <= s2; end
            end
            assign synced_alias  = s2;
            assign delayed_alias = s3;
            always_ff @(posedge cb or negedge rstn)
                if (!rstn) pulse_q <= 1'b0;
                else       pulse_q <= synced_alias ^ delayed_alias;
        endmodule
    )", "cdc_pulse_renamed");
    REQUIRE(compiled);

    FullPipeline pipeline;
    pipeline.run(*compiled.compilation);

    bool foundPulse = false;
    for (const auto& c : pipeline.crossings)
        if (c.sync_type == SyncType::PulseSync) foundPulse = true;
    CHECK(foundPulse);
}

TEST_CASE("CDC SyncVerifier: param-only-reset FF must NOT pose as sync stage",
          "[cdc][sync][empty_fanin]") {
    // Code-reviewer Round 8 Finding #3 (MEDIUM). After the
    // parameter-fanin filter landed in collectReferencedSignals,
    // a flip-flop whose only non-reset assignment is `q <= P`
    // (where P is a parameter) ends up with EMPTY fanin_signals.
    // The relaxed findNextFF accepts edge->dest with empty fanin
    // unconditionally, which would falsely classify a parameter-
    // driven downstream FF as a sync stage. The fix uses
    // fanin_populated to distinguish "no fanin walked" (library-
    // cell stub: accept) from "fanin walked but everything was
    // filtered" (suspicious: reject).
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module empty_fanin_top #(parameter logic INIT_VAL = 1'b0)
            (input logic ca, cb, rstn, d);
            logic q_a, fake_sync;
            // Genuine cross-domain source.
            always_ff @(posedge ca or negedge rstn)
                if (!rstn) q_a <= 1'b0; else q_a <= d;
            // Destination FF whose ONLY runtime assignment is to a
            // parameter. fanin_signals will be empty after the
            // parameter filter. Without fanin_populated, findNextFF
            // would incorrectly accept this as a sync stage.
            always_ff @(posedge cb or negedge rstn)
                if (!rstn) fake_sync <= INIT_VAL;
                else       fake_sync <= INIT_VAL;
        endmodule
    )", "cdc_empty_fanin");
    REQUIRE(compiled);

    FullPipeline pipeline;
    pipeline.run(*compiled.compilation);

    // The fake_sync FF must NOT be classified as a sync stage of
    // a crossing from q_a (no FFEdge from q_a feeds fake_sync, so
    // there should be NO crossing reported here). At minimum no
    // crossing must be classified as TwoFF/ThreeFF for q_a.
    bool falseTwoFF = false;
    for (const auto& c : pipeline.crossings) {
        if (c.sync_type == SyncType::TwoFF ||
            c.sync_type == SyncType::ThreeFF) {
            falseTwoFF = true;
        }
    }
    CHECK_FALSE(falseTwoFF);
}

TEST_CASE("CDC SyncVerifier: multi-hop delay tap pulse sync graduates to PulseSync",
          "[cdc][sync][pulse_multihop]") {
    // Round 10 US-803: a 4-stage sync chain (s1->s2->s3->s4) where
    // the XOR taps `s4` and a delayed `s4_d` register that lives
    // TWO hops downstream of last_sync (which is s4 in the chain).
    // Pre-fix: the forward extension only walks ONE hop, so s4_d
    // is missed and chain_inputs stays at 1 -> classification falls
    // back to TwoFF/ThreeFF instead of PulseSync. With multi-hop
    // BFS, both s4_d (2 hops from s4) and the XOR consumer are
    // visible, and chain_inputs reaches 2.
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module pulse_mh (input logic ca, cb, rstn, pulse_in);
            logic toggle_a, s1, s2, s3, s4, s4_d, pulse_q;
            always_ff @(posedge ca or negedge rstn)
                if (!rstn) toggle_a <= 1'b0;
                else if (pulse_in) toggle_a <= ~toggle_a;
            // 4-stage sync chain
            always_ff @(posedge cb or negedge rstn) begin
                if (!rstn) begin
                    s1 <= 1'b0; s2 <= 1'b0; s3 <= 1'b0; s4 <= 1'b0;
                end else begin
                    s1 <= toggle_a; s2 <= s1; s3 <= s2; s4 <= s3;
                end
            end
            // s4_d sits 2 hops downstream of s4 (s4 -> s4_d via
            // single-fanin no-comb edge). Walking only one hop
            // from last_sync misses this.
            always_ff @(posedge cb or negedge rstn)
                if (!rstn) s4_d <= 1'b0; else s4_d <= s4;
            // XOR edge detector consumes BOTH s4 and the deeper
            // delayed copy.
            always_ff @(posedge cb or negedge rstn)
                if (!rstn) pulse_q <= 1'b0;
                else       pulse_q <= s4 ^ s4_d;
        endmodule
    )", "cdc_pulse_mh");
    REQUIRE(compiled);

    FullPipeline pipeline;
    pipeline.run(*compiled.compilation);

    bool foundPulse = false;
    for (const auto& c : pipeline.crossings)
        if (c.sync_type == SyncType::PulseSync) foundPulse = true;
    CHECK(foundPulse);
}

TEST_CASE("CDC: hier-ref into generate-array (gen_blk[i].sub.q) is detected",
          "[cdc][sync][gen_array_href]") {
    // Round 10 US-802: parent reads `u_top.gen_blk[1].sub.q` -- a
    // hierarchical reference INTO a generate-array entry. The
    // architect noted a potential format mismatch between
    // getHierarchicalPath() (e.g. "gen_blk[1].sub") and the
    // ff_classifier's entry_path (e.g. "gen_blk.gen_blk[1]").
    // Lock the desired behavior: the cross-domain read must produce
    // a CDC crossing in the report.
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module gar_top (input logic ca, cb, rst_n, d);
            logic q_dst;
            gar_sub u_sub (.clk_i(ca), .rst_ni(rst_n), .d_i(d));
            // Hierarchical read into a generate-array entry.
            always_ff @(posedge cb or negedge rst_n)
                if (!rst_n) q_dst <= 1'b0;
                else        q_dst <= u_sub.gen_blk[1].q_inner;
        endmodule
        module gar_sub (input logic clk_i, rst_ni, d_i);
            for (genvar i = 0; i < 4; i++) begin : gen_blk
                logic q_inner;
                always_ff @(posedge clk_i or negedge rst_ni)
                    if (!rst_ni) q_inner <= 1'b0;
                    else         q_inner <= d_i;
            end
        endmodule
    )", "cdc_gen_array_href");
    REQUIRE(compiled);

    FullPipeline pipeline;
    pipeline.run(*compiled.compilation);

    // The crossing u_sub.gen_blk[1].q_inner -> q_dst must be
    // detected. Domain identity uses ca on the source side and
    // cb on the dest side after clock unification.
    bool found = false;
    for (const auto& c : pipeline.crossings) {
        // svlens flattens labeled-genvar entries to "genblk<N>"
        // (no underscore, no brackets) in the FFNode hier_path.
        // The test accepts both forms in case slang's
        // getExternalName() behavior changes in a future version.
        bool src_is_gen = c.source_signal.find("genblk") != std::string::npos ||
                          c.source_signal.find("gen_blk") != std::string::npos;
        bool dst_is_q_dst = c.dest_signal.find(".q_dst") != std::string::npos;
        if (src_is_gen && dst_is_q_dst) found = true;
    }
    CHECK(found);
}

TEST_CASE("CDC: multi-level genvar + shift-register sync chain detected end-to-end",
          "[cdc][sync][genvar_multi_level]") {
    // Round 10 US-801: a 2-level instance hierarchy where the parent
    // module wraps a sub-module that contains a genvar for-block of
    // shift-register synchronizers. Mirrors the structural shape of
    // pulp-platform/common_cells/src/cdc_fifo_gray.sv (without the
    // FFLARN macros so the test is self-contained).
    //
    // After Round 9 clock unification + hierarchical-port handling,
    // the cross-domain crossing src_clk -> sync FFs in dst domain
    // is detected for each genvar bit, and the shift-register
    // sync.sv idiom is recognized as a multi-FF synchronizer.
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module gv_top (input logic src_clk_i, dst_clk_i, rst_n,
                       input logic [3:0] data_in);
            logic [3:0] data_src_q;
            always_ff @(posedge src_clk_i or negedge rst_n)
                if (!rst_n) data_src_q <= '0; else data_src_q <= data_in;
            gv_dst u_dst (.clk_i(dst_clk_i), .rst_ni(rst_n),
                          .async_data(data_src_q));
        endmodule
        module gv_dst (input logic clk_i, rst_ni, input logic [3:0] async_data);
            for (genvar i = 0; i < 4; i++) begin : g_sync
                gv_shift #(.STAGES(2)) u_sync
                    (.clk_i, .rst_ni, .serial_i(async_data[i]));
            end
        endmodule
        module gv_shift #(parameter int STAGES = 2)
                         (input logic clk_i, rst_ni, serial_i);
            logic [STAGES-1:0] reg_q;
            always_ff @(posedge clk_i or negedge rst_ni)
                if (!rst_ni) reg_q <= '0;
                else         reg_q <= {reg_q[STAGES-2:0], serial_i};
        endmodule
    )", "cdc_gv_multi");
    REQUIRE(compiled);

    FullPipeline pipeline;
    pipeline.run(*compiled.compilation);

    // At least 4 cross-domain crossings (one per genvar bit).
    // sync_type may be gray_code, johnson_counter, or two_ff
    // depending on the shift-register signature -- accept any
    // recognised sync. The KEY invariant is that the crossing IS
    // detected (not lost in domain unification merge).
    int crossingsToSync = 0;
    for (const auto& c : pipeline.crossings) {
        if (c.sync_type != SyncType::None &&
            c.source_signal.find("data_src_q") != std::string::npos)
            crossingsToSync++;
    }
    CHECK(crossingsToSync >= 4);
}

TEST_CASE("CDC clock_tree: shadowed `clk` names in different submodules must NOT merge",
          "[cdc][clock][shadow_dedup]") {
    // Silent-failure-hunter Round 9 Finding #3 (HIGH).
    // Top has TWO independent submodules, each receiving a
    // separately-driven clock that is locally named `clk` in the
    // parent expression. With name-only de-dup the second
    // unification call would reuse the first ClockSource and
    // collapse two physically distinct domains into one,
    // suppressing real CDC crossings between them. The fix uses
    // an instance-qualified key so the two `clk`s stay distinct.
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module shadow_dedup_top (input logic ck_a, ck_b, rst_n, d);
            logic clk_a_local, clk_b_local;
            // Each "clk_x_local" is a SEPARATE physical clock that
            // happens to share the local name `clk` in the parent
            // expression of the submodule instantiation.
            assign clk_a_local = ck_a;
            assign clk_b_local = ck_b;
            sub_clk_dedup u_a (.clk(clk_a_local), .rstn(rst_n), .d_i(d));
            sub_clk_dedup u_b (.clk(clk_b_local), .rstn(rst_n), .d_i(u_a.q));
        endmodule
        module sub_clk_dedup (input logic clk, rstn, d_i);
            logic q;
            always_ff @(posedge clk or negedge rstn)
                if (!rstn) q <= 1'b0; else q <= d_i;
        endmodule
    )", "cdc_shadow_dedup");
    REQUIRE(compiled);

    FullPipeline pipeline;
    pipeline.run(*compiled.compilation);

    // u_a.q and u_b.q must be in DIFFERENT domains. The crossing
    // u_a.q -> u_b.q must be reported as a CDC crossing.
    bool foundCrossing = false;
    for (const auto& c : pipeline.crossings) {
        if (c.source_signal.find("u_a.q") != std::string::npos &&
            c.dest_signal.find("u_b.q") != std::string::npos) {
            foundCrossing = true;
        }
    }
    CHECK(foundCrossing);
}

TEST_CASE("CDC clock_tree: plain `always` block must NOT trigger clock unification",
          "[cdc][clock][port_clock_neg]") {
    // Code-reviewer Round 8 Finding #2 (HIGH) sub-issue 1.
    // isPortUsedAsClock currently scans both AlwaysFF AND plain
    // Always blocks. A simulation/library helper such as a memory
    // model often uses `always @(*)` or `always @(posedge phi)` for
    // non-synthesizable purposes. Promoting `phi` to a ClockSource
    // via port-driven unification leaks phantom domains into the
    // CDC analysis. Restrict the scan to AlwaysFF only.
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module port_clock_neg_top (input logic ca, phi, rst_n, d);
            logic q_a;
            always_ff @(posedge ca or negedge rst_n)
                if (!rst_n) q_a <= 1'b0; else q_a <= d;
            sub_helper u_helper (.tap(phi), .x(q_a));
        endmodule
        // Plain `always` block (NOT always_ff). Models a behavioral
        // monitor / memory testbench helper. Should NOT cause
        // `phi` to be auto-registered as a clock source on the
        // parent side.
        module sub_helper (input logic tap, x);
            logic captured;
            always @(posedge tap)
                captured = x;
        endmodule
    )", "cdc_port_clock_neg");
    REQUIRE(compiled);

    FullPipeline pipeline;
    pipeline.run(*compiled.compilation);

    // The only legitimate clock domain is `ca`. `phi` MUST NOT be
    // auto-registered.
    bool foundPhiDomain = false;
    for (const auto& src : pipeline.db.sources) {
        if (src->name == "phi") foundPhiDomain = true;
    }
    CHECK_FALSE(foundPhiDomain);
}

TEST_CASE("CDC clock_tree: clock_rst_n style port stays a reset (not a clock)",
          "[cdc][clock][reset_filter]") {
    // Code-reviewer Round 8 Finding #2 (HIGH) sub-issue 2.
    // A port literally named `clk_rst_n` (clock-domain reset, name
    // contains `rst`) used in `negedge clk_rst_n` of an always_ff
    // sensitivity must NOT be auto-registered as a clock. The
    // current isPortUsedAsClock filter rejects on isResetName
    // alone, but if the heuristic ever changes (e.g., to favour
    // clock-pattern over reset-pattern when both match), this
    // test guards the desired behavior.
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module reset_filter_top (input logic clk, clk_rst_n, d);
            logic q;
            sub_filter u_sub (.clk_i(clk), .reset_n(clk_rst_n), .d_i(d));
        endmodule
        module sub_filter (input logic clk_i, reset_n, d_i);
            logic q;
            always_ff @(posedge clk_i or negedge reset_n)
                if (!reset_n) q <= 1'b0; else q <= d_i;
        endmodule
    )", "cdc_reset_filter");
    REQUIRE(compiled);

    FullPipeline pipeline;
    pipeline.run(*compiled.compilation);

    bool foundResetClock = false;
    for (const auto& src : pipeline.db.sources) {
        if (src->name == "clk_rst_n") foundResetClock = true;
    }
    CHECK_FALSE(foundResetClock);
}

TEST_CASE("CDC SyncVerifier: synced AND local_en into downstream FF is NOT PulseSync",
          "[cdc][sync][pulse_neg]") {
    // Code-reviewer Round 8 Finding #1 (HIGH). After dropping the
    // strict `f == last_leaf` membership check in
    // detectPulseSyncPattern, any downstream FF with fanin.size()>=2
    // would be silently graduated to PulseSync regardless of whether
    // the second fanin is a delayed copy of the synced output. This
    // fixture exercises the false-positive path: a properly synced
    // signal feeds a downstream FF that ALSO takes a same-domain
    // local enable -- absolutely NOT a pulse synchronizer. The
    // detector must keep the underlying TwoFF / ThreeFF / Caution
    // classification, NOT reclassify as PulseSync.
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module pulse_fp (input logic ca, cb, rstn, d, local_en);
            logic toggle_a, s1, s2, gated_q, en_q;
            always_ff @(posedge ca or negedge rstn)
                if (!rstn) toggle_a <= 1'b0;
                else       toggle_a <= d;
            always_ff @(posedge cb or negedge rstn) begin
                if (!rstn) begin s1 <= 1'b0; s2 <= 1'b0; en_q <= 1'b0; end
                else begin s1 <= toggle_a; s2 <= s1; en_q <= local_en; end
            end
            // s2 (synced) AND en_q (local enable, NOT a delayed copy)
            // -- NOT a pulse-sync XOR edge detector.
            always_ff @(posedge cb or negedge rstn)
                if (!rstn) gated_q <= 1'b0;
                else       gated_q <= s2 & en_q;
        endmodule
    )", "cdc_pulse_fp");
    REQUIRE(compiled);

    FullPipeline pipeline;
    pipeline.run(*compiled.compilation);

    bool falsePulseClassification = false;
    for (const auto& c : pipeline.crossings)
        if (c.sync_type == SyncType::PulseSync) falsePulseClassification = true;
    CHECK_FALSE(falsePulseClassification);
}

TEST_CASE("CDC: hierarchical reference cross-domain read is detected",
          "[cdc][sync][hier_ref]") {
    // Lock the hierarchical-reference fix: when a parent module
    // reads a sub-module's internal FF via a hierarchical access
    // (`u_sync.q`), the connectivity tracker must still build the
    // FFEdge from the source FF inside the submodule to the parent's
    // dest FF. Before the fix, only the LEAF name "q" was recorded
    // in fanin, so findFFByName couldn't disambiguate which `q`
    // (multiple submodules can share the same internal name) and
    // dropped the edge silently. After the fix, HierarchicalValue
    // expressions push the FULL hierarchical path
    // (`href_top.u_sync.q`) so output_map's direct lookup hits.
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module href_top (input logic ca, cb, rst_n, d);
            logic q_a, q_dst;
            always_ff @(posedge ca or negedge rst_n)
                if (!rst_n) q_a <= 1'b0; else q_a <= d;
            sub_clk u_sync (.clk_i(ca), .rst_ni(rst_n), .d_i(q_a));
            always_ff @(posedge cb or negedge rst_n)
                if (!rst_n) q_dst <= 1'b0;
                else        q_dst <= u_sync.q;
        endmodule
        module sub_clk (input logic clk_i, rst_ni, d_i);
            logic q;
            always_ff @(posedge clk_i or negedge rst_ni)
                if (!rst_ni) q <= 1'b0; else q <= d_i;
        endmodule
    )", "cdc_href");
    REQUIRE(compiled);

    FullPipeline pipeline;
    pipeline.run(*compiled.compilation);

    // u_sync.q (in ca) -> q_dst (in cb) MUST appear as a crossing.
    bool found = false;
    for (const auto& c : pipeline.crossings) {
        if (c.source_signal.find("u_sync.q") != std::string::npos &&
            c.dest_signal.find(".q_dst") != std::string::npos) {
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("CDC: hierarchical reference SAME domain read is NOT a crossing",
          "[cdc][sync][hier_ref]") {
    // Paired negative: a parent module reads u_sync.q via
    // hierarchical access but BOTH FFs are clocked by the same
    // physical clock (cb). After clock unification + hierarchical
    // ref tracking, the resulting FFEdge is intra-domain and must
    // NOT be reported as a CDC crossing.
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module href_neg_top (input logic cb, rst_n, d);
            logic q_dst;
            href_neg_sub u_sync (.clk_i(cb), .rst_ni(rst_n), .d_i(d));
            always_ff @(posedge cb or negedge rst_n)
                if (!rst_n) q_dst <= 1'b0;
                else        q_dst <= u_sync.q;
        endmodule
        module href_neg_sub (input logic clk_i, rst_ni, d_i);
            logic q;
            always_ff @(posedge clk_i or negedge rst_ni)
                if (!rst_ni) q <= 1'b0; else q <= d_i;
        endmodule
    )", "cdc_href_neg");
    REQUIRE(compiled);

    FullPipeline pipeline;
    pipeline.run(*compiled.compilation);

    // No cross-domain crossing should be reported.
    CHECK(pipeline.crossings.empty());
}

TEST_CASE("CDC SyncVerifier: cross-instance clock unification via port mapping",
          "[cdc][sync][clk_unify]") {
    // Without an SDC, the auto-clock detector currently treats the
    // submodule's `clk_i` port as a brand-new ClockSource because
    // FFClassifier's path 3 creates a fresh source for any
    // unmatched sensitivity signal. This means an FF inside the
    // submodule (clocked by clk_i, which is wired to parent's
    // `clk_b`) ends up in a different domain than parent FFs that
    // also use `clk_b` -- producing spurious cross-domain
    // crossings inside what is actually a single domain.
    //
    // Lock the desired behavior: only TWO domains should exist
    // (clk_a, clk_b), and the only crossing reported should be
    // q_a (clk_a) -> u_sync.q (clk_b) -- NOT u_sync.q -> q_dst.
    // Use port names that DO NOT match the isClockName heuristic
    // ("ca"/"cb"/"sub_clk_i"/...) so autoDetectClockPorts misses
    // them. Without unification, the submodule's `clk_i` (which IS
    // a clock-named port) becomes a fresh ClockSource via
    // FFClassifier's path 3, completely separate from the parent's
    // `cb`-driven clock domain. The test locks that the unification
    // happens via PORT BINDING (.clk_i(cb)) regardless of whether
    // the parent expression's name matches isClockName.
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module clk_unify_top (input logic ca, cb, rst_n, d);
            logic q_a, q_dst, sync_out;
            always_ff @(posedge ca or negedge rst_n)
                if (!rst_n) q_a <= 1'b0; else q_a <= d;
            sub_clk u_sync (.clk_i(cb), .rst_ni(rst_n),
                            .d_i(q_a), .q_o(sync_out));
            always_ff @(posedge cb or negedge rst_n)
                if (!rst_n) q_dst <= 1'b0; else q_dst <= sync_out;
        endmodule
        module sub_clk (input logic clk_i, rst_ni, d_i,
                        output logic q_o);
            logic q;
            always_ff @(posedge clk_i or negedge rst_ni)
                if (!rst_ni) q <= 1'b0; else q <= d_i;
            assign q_o = q;
        endmodule
    )", "cdc_clk_unify");
    REQUIRE(compiled);

    FullPipeline pipeline;
    pipeline.run(*compiled.compilation);

    // Only u_sync.q (in clk_b domain after unification) reads from
    // q_a (in clk_a). The chain u_sync.q -> q_dst is intra-clk_b
    // and must NOT appear as a CDC crossing.
    bool found_intra_clkb = false;
    for (const auto& c : pipeline.crossings) {
        bool src_is_sub = c.source_signal.find("u_sync.q") != std::string::npos;
        bool dst_is_top_qdst = c.dest_signal.find(".q_dst") != std::string::npos;
        if (src_is_sub && dst_is_top_qdst) {
            found_intra_clkb = true;
        }
    }
    CHECK_FALSE(found_intra_clkb);
}

TEST_CASE("CDC SyncVerifier: parameterized reset value does not break TwoFF detection",
          "[cdc][sync][param_reset]") {
    // Regression lock for the parameter-fanin bug. The OpenTitan
    // prim_flop pattern is `q_o <= ResetValue` (parameter), and
    // before the fix `ResetValue` was being collected into
    // fanin_signals as a runtime signal. This inflated
    // fanin.size() to 2 and silently broke 2-FF sync detection
    // for every OpenTitan-style synchronizer that parameterizes
    // its reset value -- including prim_fifo_async,
    // prim_pulse_sync, and any IP that uses prim_flop_2sync.
    //
    // After the fix in collectReferencedSignals (filtering
    // SymbolKind::Parameter / TypeParameter / EnumValue),
    // fanin.size() = 1 (`d_i` only) and the sync chain is
    // recognized correctly.
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module pf_top (input logic ca, cb, rstn, d);
            logic q_a, q_sync;
            always_ff @(posedge ca or negedge rstn)
                if (!rstn) q_a <= 1'b0; else q_a <= d;
            pf2sync_param u_sync (.clk_i(cb), .rst_ni(rstn),
                                  .d_i(q_a), .q_o(q_sync));
        endmodule
        module pf2sync_param #(
            parameter logic ResetValue = 1'b0
        ) (input logic clk_i, rst_ni, d_i, output logic q_o);
            logic d_o, intq;
            always_comb d_o = d_i;
            pflop_param #(.ResetValue(ResetValue))
                u_sync_1 (.clk_i, .rst_ni, .d_i(d_o), .q_o(intq));
            pflop_param #(.ResetValue(ResetValue))
                u_sync_2 (.clk_i, .rst_ni, .d_i(intq), .q_o);
        endmodule
        module pflop_param #(
            parameter logic ResetValue = 1'b0
        ) (input logic clk_i, rst_ni, d_i, output logic q_o);
            always_ff @(posedge clk_i or negedge rst_ni)
                if (!rst_ni) q_o <= ResetValue;  // parameter fanin
                else         q_o <= d_i;
        endmodule
    )", "cdc_param_reset");
    REQUIRE(compiled);

    FullPipeline pipeline;
    pipeline.run(*compiled.compilation);

    bool foundTwoFF = false;
    for (const auto& c : pipeline.crossings)
        if (c.sync_type == SyncType::TwoFF) foundTwoFF = true;
    CHECK(foundTwoFF);
}

TEST_CASE("CDC SyncVerifier: multi-fanin dest blocks TwoFF",
          "[cdc][sync][multi_fanin]") {
    // Pillar 3: the relaxation accepts cross-name fanin only when
    // `fanin.size() <= 1`. If the dest FF reads two signals in
    // its always_ff body, it is NOT a single sync stage and
    // findNextFF must reject it.
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module multi_fanin (input logic ca, cb, rstn, d, e);
            logic q_a, q_e, sync1, sync2;
            always_ff @(posedge ca or negedge rstn)
                if (!rstn) q_a <= 1'b0; else q_a <= d;
            always_ff @(posedge cb or negedge rstn)
                if (!rstn) q_e <= 1'b0; else q_e <= e;
            // sync1 has TWO fanin (q_a from src + q_e from same dst
            // domain) -- this is NOT a clean 2-FF chain.
            always_ff @(posedge cb or negedge rstn)
                if (!rstn) sync1 <= 1'b0; else sync1 <= q_a | q_e;
            always_ff @(posedge cb or negedge rstn)
                if (!rstn) sync2 <= 1'b0; else sync2 <= sync1;
        endmodule
    )", "cdc_multi_fanin");
    REQUIRE(compiled);

    FullPipeline pipeline;
    pipeline.run(*compiled.compilation);

    // The q_a -> sync1 edge must NOT be classified as TwoFF
    // because sync1's fanin contains both q_a and q_e.
    REQUIRE(!pipeline.crossings.empty());
    bool foundQaTwoFF = false;
    for (const auto& c : pipeline.crossings) {
        if (c.sync_type != SyncType::TwoFF) continue;
        if (c.source_signal.find("q_a") != std::string::npos)
            foundQaTwoFF = true;
    }
    CHECK_FALSE(foundQaTwoFF);
}

TEST_CASE("CDC SyncVerifier: 1-bit XOR self-feedback is NOT a TwoFF chain",
          "[cdc][sync][self_feedback]") {
    // Round 35 US-35F: the self-shift TwoFF heuristic at
    // sync_verifier.cpp:96-110 was over-eager. Any FF whose own
    // leaf appears in fanin_signals was classified as TwoFF, even
    // when the underlying RHS was XOR feedback (`q <= q ^ data`)
    // rather than a canonical shift-register concat. A 1-bit FF
    // can never be a multi-stage shift register, so width >= 2 is
    // a necessary structural condition for the self-shift idiom.
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module self_xor (input logic ca, cb, rstn, d);
            logic q_a, q_b;
            always_ff @(posedge ca or negedge rstn)
                if (!rstn) q_a <= 1'b0; else q_a <= d;
            // 1-bit XOR feedback: NOT a shift register, just
            // accumulator-style storage. fanin = {q_a, q_b}.
            always_ff @(posedge cb or negedge rstn)
                if (!rstn) q_b <= 1'b0; else q_b <= q_b ^ q_a;
        endmodule
    )", "cdc_self_xor");
    REQUIRE(compiled);

    FullPipeline pipeline;
    pipeline.run(*compiled.compilation);

    // The q_a -> q_b edge must NOT be classified as TwoFF: q_b is
    // 1-bit and the self-shift heuristic should reject because no
    // 1-bit signal can host a shift-register chain.
    REQUIRE(!pipeline.crossings.empty());
    bool foundFalseTwoFF = false;
    for (const auto& c : pipeline.crossings) {
        if (c.sync_type == SyncType::TwoFF &&
            c.source_signal.find("q_a") != std::string::npos)
            foundFalseTwoFF = true;
    }
    CHECK_FALSE(foundFalseTwoFF);
}
