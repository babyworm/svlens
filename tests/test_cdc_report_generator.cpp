#include <catch2/catch_test_macros.hpp>
#include "sv-cdccheck/report_generator.h"
#include "sv-cdccheck/types.h"
#include <slang/syntax/SyntaxTree.h>

#include <filesystem>
#include <fstream>
#include <tuple>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace sv_cdccheck;

static AnalysisResult makeCdcResult() {
    AnalysisResult result;

    auto sys = std::make_unique<ClockSource>();
    sys->name = "sys_clk";
    sys->type = ClockSource::Type::Primary;
    auto* sysPtr = result.clock_db.addSource(std::move(sys));
    auto* sysDom = result.clock_db.findOrCreateDomain(sysPtr, Edge::Posedge);

    auto ext = std::make_unique<ClockSource>();
    ext->name = "ext_clk";
    ext->type = ClockSource::Type::AutoDetected;
    auto* extPtr = result.clock_db.addSource(std::move(ext));
    auto* extDom = result.clock_db.findOrCreateDomain(extPtr, Edge::Posedge);

    result.clock_db.relationships.push_back({sysPtr, extPtr, DomainRelationship::Type::Asynchronous});

    CrossingReport violation;
    violation.id = "VIOLATION-001";
    violation.category = ViolationCategory::Violation;
    violation.severity = Severity::High;
    violation.source_signal = "top.u_a.q_data";
    violation.dest_signal = "top.u_b.q_data";
    violation.source_domain = sysDom;
    violation.dest_domain = extDom;
    violation.sync_type = SyncType::None;
    violation.recommendation = "Insert 2-FF synchronizer";
    violation.relationship = "asynchronous";
    violation.rationale = "Async domains require a synchronizer";
    result.crossings.push_back(violation);

    CrossingReport info;
    info.id = "INFO-001";
    info.category = ViolationCategory::Info;
    info.severity = Severity::Info;
    info.source_signal = "top.u_a.q_sync";
    info.dest_signal = "top.u_b.sync_ff2";
    info.source_domain = sysDom;
    info.dest_domain = extDom;
    info.sync_type = SyncType::TwoFF;
    info.relationship = "divided";
    info.rationale = "Related clocks share timing constraints";
    info.timing_basis_ns = 8.0;
    result.crossings.push_back(info);

    auto violationDest = std::make_unique<FFNode>();
    violationDest->hier_path = "top.u_b.q_data";
    violationDest->domain = extDom;
    violationDest->clock_path = "top.u_b.clk_i";
    violationDest->declared_path = violationDest->hier_path;
    result.ff_nodes.push_back(std::move(violationDest));

    return result;
}

static void addSvaSamplingFF(AnalysisResult& result, std::string path, ClockDomain* domain) {
    auto ff = std::make_unique<FFNode>();
    ff->hier_path = std::move(path);
    ff->domain = domain;
    ff->clock_path = "top.clk_b";
    ff->declared_path = ff->hier_path;
    result.ff_nodes.push_back(std::move(ff));
}

TEST_CASE("CDC ReportGenerator: counts reflect crossing categories", "[cdc][report]") {
    auto result = makeCdcResult();
    CHECK(result.violation_count() == 1);
    CHECK(result.info_count() == 1);
    CHECK(result.caution_count() == 0);
}

TEST_CASE("CDC ReportGenerator: markdown output contains summary and domains", "[cdc][report]") {
    auto result = makeCdcResult();
    result.crossings[0].sdc_false_path = true;
    result.crossings[0].sdc_max_delay_constraint_ns = 8.0;
    result.crossings[0].sdc_max_delay_datapath_only = true;
    ReportGenerator generator(result);

    auto path = fs::temp_directory_path() / "svlens_cdc_report.md";
    generator.generateMarkdown(path);

    std::ifstream ifs(path);
    REQUIRE(ifs.good());
    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(content.find("# CDC Analysis Report") != std::string::npos);
    CHECK(content.find("VIOLATION | 1") != std::string::npos);
    CHECK(content.find("sys_clk") != std::string::npos);
    CHECK(content.find("ext_clk") != std::string::npos);
    CHECK(content.find("SDC False Path: timing excluded; CDC classification unchanged") != std::string::npos);
    CHECK(content.find("Declared SDC Max Delay: 8 ns (datapath only); not measured path delay") != std::string::npos);
}

TEST_CASE("CDC ReportGenerator: json output contains summary and crossing ids", "[cdc][report]") {
    auto result = makeCdcResult();
    result.clock_db.sources[0]->period_ns = 8.5;
    result.crossings[0].capture_conditions = {"en_i"};
    result.crossings[0].sdc_false_path = true;
    result.crossings[0].sdc_max_delay_constraint_ns = 8.0;
    result.crossings[0].sdc_max_delay_datapath_only = true;
    ReportGenerator generator(result);

    auto path = fs::temp_directory_path() / "svlens_cdc_report.json";
    generator.generateJSON(path);

    std::ifstream ifs(path);
    REQUIRE(ifs.good());
    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(content.find("\"summary\"") != std::string::npos);
    CHECK(content.find("\"period_ns\": 8.5") != std::string::npos);
    CHECK(content.find("\"period_ns\": null") != std::string::npos);
    CHECK(content.find("\"violations\": 1") != std::string::npos);
    CHECK(content.find("VIOLATION-001") != std::string::npos);
    CHECK(content.find("INFO-001") != std::string::npos);
    CHECK(content.find("\"relationship\": \"asynchronous\"") != std::string::npos);
    CHECK(content.find("\"rationale\": \"Related clocks share timing constraints\"") != std::string::npos);
    CHECK(content.find("\"timing_basis_ns\": 8") != std::string::npos);
    CHECK(content.find("\"sdc_false_path\": true") != std::string::npos);
    CHECK(content.find("\"sdc_max_delay_constraint_ns\": 8") != std::string::npos);
    CHECK(content.find("\"sdc_max_delay_datapath_only\": true") != std::string::npos);
    CHECK(content.find("\"capture_conditions\": [\"en_i\"]") != std::string::npos);
}

TEST_CASE("CDC ReportGenerator: root provenance is separate from crossing classification", "[cdc][report][lineage]") {
    auto result = makeCdcResult();
    auto* root = result.clock_db.sources[0].get();
    auto* derived = result.clock_db.sources[1].get();
    root->origin_signal = "top.clk_main_i";
    derived->origin_signal = "u_gate/clk_o";
    result.clock_db.root_by_path[root->origin_signal] = root;
    result.clock_db.root_by_path["top.gated_clk"] = root;
    auto gatedNet = std::make_unique<ClockNet>();
    gatedNet->hier_path = "top.gated_clk";
    gatedNet->source = derived;
    result.clock_db.addNet(std::move(gatedNet));

    auto path = fs::temp_directory_path() / "svlens_cdc_lineage_report.json";
    ReportGenerator(result).generateJSON(path);
    std::ifstream ifs(path);
    REQUIRE(ifs.good());
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(content.find("\"source_root_domain\": \"sys_clk\"") != std::string::npos);
    CHECK(content.find("\"dest_root_domain\": \"sys_clk\"") != std::string::npos);
    CHECK(content.find("\"category\": \"VIOLATION\"") != std::string::npos);
}

TEST_CASE("CDC ReportGenerator: markdown output includes relationship and rationale details", "[cdc][report]") {
    auto result = makeCdcResult();
    ReportGenerator generator(result);

    auto path = fs::temp_directory_path() / "svlens_cdc_report_details.md";
    generator.generateMarkdown(path);

    std::ifstream ifs(path);
    REQUIRE(ifs.good());
    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(content.find("Relationship: asynchronous") != std::string::npos);
    CHECK(content.find("Rationale: Async domains require a synchronizer") != std::string::npos);
    CHECK(content.find("Timing Basis: 8") != std::string::npos);
}


TEST_CASE("CDC ReportGenerator: intentional primitive sync types remain visible in json output", "[cdc][report]") {
    auto result = makeCdcResult();

    CrossingReport handshakeInfo;
    handshakeInfo.id = "INFO-INTENT-001";
    handshakeInfo.category = ViolationCategory::Info;
    handshakeInfo.severity = Severity::Info;
    handshakeInfo.source_signal = "top.u_hs.src_req_q";
    handshakeInfo.dest_signal = "top.u_hs.dst_req_q";
    handshakeInfo.source_domain = result.clock_db.domains[0].get();
    handshakeInfo.dest_domain = result.clock_db.domains[1].get();
    handshakeInfo.sync_type = SyncType::Handshake;
    handshakeInfo.relationship = "asynchronous";
    handshakeInfo.rationale = "Intentional handshake primitive";
    result.crossings.push_back(handshakeInfo);

    ReportGenerator generator(result);
    auto path = fs::temp_directory_path() / "svlens_cdc_report_intent.json";
    generator.generateJSON(path);

    std::ifstream ifs(path);
    REQUIRE(ifs.good());
    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(content.find("INFO-INTENT-001") != std::string::npos);
    CHECK(content.find("\"sync_type\": \"handshake\"") != std::string::npos);
    CHECK(content.find("Intentional handshake primitive") != std::string::npos);
}

TEST_CASE("CDC ReportGenerator: SVA emitter prefixes underscore on leading-digit ids",
          "[cdc][report][sva][leading_digit]") {
    // Round 29 WARN #2: SystemVerilog identifiers cannot start with a
    // digit. svaSanitize replaces non-alnum with '_' but historically
    // would keep a leading digit, producing a property name like
    // `cdc_1_BAD_src_toggle` which wraps the digit safely -- BUT if a
    // crossing id sanitizes to a form that itself starts with a digit
    // (when used as a prefix elsewhere), the resulting identifier is
    // invalid. Lock the invariant that emitted property name (which
    // includes the sanitized id verbatim) starts with `cdc_` and the
    // sanitized id portion never independently begins with a digit.
    AnalysisResult result;
    auto sys = std::make_unique<ClockSource>();
    sys->name = "sys_clk";
    sys->type = ClockSource::Type::Primary;
    auto* sysPtr = result.clock_db.addSource(std::move(sys));
    auto* sysDom = result.clock_db.findOrCreateDomain(sysPtr, Edge::Posedge);
    auto ext = std::make_unique<ClockSource>();
    ext->name = "ext_clk";
    ext->type = ClockSource::Type::Primary;
    auto* extPtr = result.clock_db.addSource(std::move(ext));
    auto* extDom = result.clock_db.findOrCreateDomain(extPtr, Edge::Posedge);

    CrossingReport v;
    v.id = "1BAD"; // starts with a digit
    v.category = ViolationCategory::Violation;
    v.severity = Severity::High;
    v.source_signal = "top.q";
    v.dest_signal = "top.sync_q";
    v.source_domain = sysDom;
    v.dest_domain = extDom;
    v.sync_type = SyncType::None;
    result.crossings.push_back(std::move(v));
    addSvaSamplingFF(result, "top.sync_q", extDom);

    ReportGenerator gen(result);
    auto path = fs::temp_directory_path() / "svlens_sva_leading_digit.sva";
    REQUIRE(gen.generateSVA(path));

    std::ifstream ifs(path);
    REQUIRE(ifs.good());
    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    fs::remove(path);

    // Sanitized id portion must NOT begin with a digit. The full
    // property name is "cdc_<id_safe>_src_toggle"; the substring
    // immediately after "cdc_" must be an SV-identifier-safe prefix,
    // i.e. NOT a digit.
    auto pos = content.find("property cdc_");
    REQUIRE(pos != std::string::npos);
    char first_after_prefix = content[pos + std::string("property cdc_").size()];
    // SV identifiers cannot start with a digit; the sanitized id
    // portion must therefore not be in [0-9].
    bool is_digit = (first_after_prefix >= '0' && first_after_prefix <= '9');
    CHECK_FALSE(is_digit);
}

TEST_CASE("CDC ReportGenerator: SVA collision dedup avoids self-collision with literal _dup ids",
          "[cdc][report][sva][collision_self]") {
    // Round 32 ERROR-2 fix: the dedup logic synthesizes "_dup<n>" on
    // collisions, but if a *literal* crossing id already has the form
    // "X_dup2" (e.g. an upstream tool emitted that name), and a prior
    // "X" already collided once producing the same synthetic name, the
    // two distinct ids would still collapse to identical SVA property
    // declarations. Lock the invariant that EVERY emitted property
    // name appears exactly once across the whole file.
    AnalysisResult result;
    auto sys = std::make_unique<ClockSource>();
    sys->name = "sys_clk";
    sys->type = ClockSource::Type::Primary;
    auto* sysPtr = result.clock_db.addSource(std::move(sys));
    auto* sysDom = result.clock_db.findOrCreateDomain(sysPtr, Edge::Posedge);
    auto ext = std::make_unique<ClockSource>();
    ext->name = "ext_clk";
    ext->type = ClockSource::Type::Primary;
    auto* extPtr = result.clock_db.addSource(std::move(ext));
    auto* extDom = result.clock_db.findOrCreateDomain(extPtr, Edge::Posedge);

    auto mkViol = [&](const std::string& id) {
        CrossingReport v;
        v.id = id;
        v.category = ViolationCategory::Violation;
        v.severity = Severity::High;
        v.source_signal = "top.q";
        v.dest_signal = "top.sync_q";
        v.source_domain = sysDom;
        v.dest_domain = extDom;
        v.sync_type = SyncType::None;
        return v;
    };
    // First X collides with second X; would synthesize X_dup2 (the
    // post-increment value). Then a literal "X_dup2" comes through,
    // which would collide with the synthetic name without the guard.
    result.crossings.push_back(mkViol("X"));
    result.crossings.push_back(mkViol("X"));
    result.crossings.push_back(mkViol("X_dup2"));
    addSvaSamplingFF(result, "top.sync_q", extDom);

    ReportGenerator gen(result);
    auto path = fs::temp_directory_path() / "svlens_sva_collision_self.sva";
    REQUIRE(gen.generateSVA(path));
    std::ifstream ifs(path);
    REQUIRE(ifs.good());
    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    fs::remove(path);

    auto count_substr = [](const std::string& hay, const std::string& needle) {
        size_t n = 0, p = 0;
        while ((p = hay.find(needle, p)) != std::string::npos) {
            ++n; p += needle.size();
        }
        return n;
    };
    // Each emitted property declaration must be unique. Total
    // declarations equal the number of crossings (3, none dropped).
    CHECK(count_substr(content, "property cdc_") == 3);
    // The literal X_dup2 must have its own slot; if the synthetic
    // dedup collides with it, count would be 2 (both forms coalesced).
    // Expected: 1 occurrence of the bare X_dup2 name.
    CHECK(count_substr(content, "property cdc_X_dup2_src_toggle;") == 1);
}

TEST_CASE("CDC ReportGenerator: SVA emitter deduplicates colliding property names",
          "[cdc][report][sva][collision]") {
    // Round 29 WARN #2: two distinct crossing ids that collapse to the
    // same sanitized form (e.g. "VIOLATION-1" and "VIOLATION_1" both
    // sanitize to "VIOLATION_1") would produce duplicate property
    // declarations, which the SV parser rejects. Lock the invariant
    // that each emitted property name appears exactly once in the file.
    AnalysisResult result;
    auto sys = std::make_unique<ClockSource>();
    sys->name = "sys_clk";
    sys->type = ClockSource::Type::Primary;
    auto* sysPtr = result.clock_db.addSource(std::move(sys));
    auto* sysDom = result.clock_db.findOrCreateDomain(sysPtr, Edge::Posedge);
    auto ext = std::make_unique<ClockSource>();
    ext->name = "ext_clk";
    ext->type = ClockSource::Type::Primary;
    auto* extPtr = result.clock_db.addSource(std::move(ext));
    auto* extDom = result.clock_db.findOrCreateDomain(extPtr, Edge::Posedge);

    auto mkViol = [&](const std::string& id) {
        CrossingReport v;
        v.id = id;
        v.category = ViolationCategory::Violation;
        v.severity = Severity::High;
        v.source_signal = "top.q";
        v.dest_signal = "top.sync_q";
        v.source_domain = sysDom;
        v.dest_domain = extDom;
        v.sync_type = SyncType::None;
        return v;
    };
    result.crossings.push_back(mkViol("VIOLATION-1"));
    result.crossings.push_back(mkViol("VIOLATION_1"));
    addSvaSamplingFF(result, "top.sync_q", extDom);

    ReportGenerator gen(result);
    auto path = fs::temp_directory_path() / "svlens_sva_collision.sva";
    REQUIRE(gen.generateSVA(path));

    std::ifstream ifs(path);
    REQUIRE(ifs.good());
    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    fs::remove(path);

    // Count "property cdc_VIOLATION_1_src_toggle" occurrences. The
    // first crossing claims it; the second should disambiguate via an
    // ordinal suffix (e.g. _2 or similar) so the count of any single
    // exact name is at most 1.
    auto count_substr = [](const std::string& hay, const std::string& needle) {
        size_t n = 0, p = 0;
        while ((p = hay.find(needle, p)) != std::string::npos) {
            ++n; p += needle.size();
        }
        return n;
    };
    // Exactly one declaration of the bare name must appear; the
    // disambiguated form (whatever the implementation chooses) must
    // also be present so neither crossing is silently dropped.
    CHECK(count_substr(content, "property cdc_VIOLATION_1_src_toggle;") == 1);
    // The total number of `property cdc_` declarations should equal
    // the number of crossings (2) — none silently dropped.
    CHECK(count_substr(content, "property cdc_") == 2);
    // Round 33 WARN-2: pin the suffix-starts-at-2 contract so a
    // future change to the do-while index doesn't silently drift.
    CHECK(content.find("property cdc_VIOLATION_1_dup2_src_toggle;") !=
          std::string::npos);
}

TEST_CASE("CDC ReportGenerator: SVA emitter signals failure on unwritable path",
          "[cdc][report][sva][unwritable]") {
    // Round 29 WARN #3 fix: previously the SVA emitter opened an
    // ofstream and silently swallowed open-failure (e.g. parent
    // directory missing), returning void with no observable signal.
    // Locks the new contract: generateSVA returns false when the
    // output stream cannot be opened, true on success.
    auto result = makeCdcResult();
    ReportGenerator gen(result);

    // Force ofstream to fail by pointing at a parent directory that
    // does not exist. Use a path with random suffix to avoid stale
    // state from prior runs.
    auto bad_dir = fs::temp_directory_path() /
                   "svlens_sva_unwritable_DIR_THAT_DOES_NOT_EXIST_XYZ";
    auto bad_path = bad_dir / "out.sva";
    // Make sure the parent really doesn't exist before the call.
    fs::remove_all(bad_dir);

    bool ok = gen.generateSVA(bad_path);
    CHECK_FALSE(ok);
    CHECK_FALSE(fs::exists(bad_path));

    // Sanity: a writable path still returns true.
    auto good_path = fs::temp_directory_path() / "svlens_sva_writable.sva";
    fs::remove(good_path);
    bool ok2 = gen.generateSVA(good_path);
    CHECK(ok2);
    CHECK(fs::exists(good_path));
    fs::remove(good_path);
}

TEST_CASE("CDC ReportGenerator: SVA emitter on empty crossings emits header only",
          "[cdc][report][sva][empty]") {
    AnalysisResult result;  // no crossings
    ReportGenerator gen(result);
    auto path = fs::temp_directory_path() / "svlens_sva_empty.sva";
    REQUIRE(gen.generateSVA(path, "empty_top"));

    std::ifstream ifs(path);
    REQUIRE(ifs.good());
    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(content.find("Crossings:  0") != std::string::npos);
    CHECK(content.find("empty_top") != std::string::npos);
    // No per-crossing header divider should appear.
    CHECK(content.find("Crossing  :") == std::string::npos);
    CHECK(content.find("property cdc_") == std::string::npos);
}

TEST_CASE("CDC ReportGenerator: SVA emitter never emits unsafe leading-dot expression",
          "[cdc][report][sva][leading_dot]") {
    // Round 28 TDD: code-reviewer flagged that a violation crossing
    // whose source_signal begins with '.' (e.g. ".q_a" produced when
    // the analyzer fails to prefix the hier-path) renders an invalid
    // SVA expression "!$stable(.q_a)". Lock the invariant that the
    // emitter never produces a leading-dot expression in $stable.
    AnalysisResult result;

    auto src = std::make_unique<ClockSource>();
    src->name = "src_clk";
    src->type = ClockSource::Type::Primary;
    auto* srcPtr = result.clock_db.addSource(std::move(src));
    auto* srcDom = result.clock_db.findOrCreateDomain(srcPtr, Edge::Posedge);

    auto dst = std::make_unique<ClockSource>();
    dst->name = "dst_clk";
    dst->type = ClockSource::Type::Primary;
    auto* dstPtr = result.clock_db.addSource(std::move(dst));
    auto* dstDom = result.clock_db.findOrCreateDomain(dstPtr, Edge::Posedge);

    CrossingReport bad;
    bad.id = "VIOLATION-Z";
    bad.category = ViolationCategory::Violation;
    bad.severity = Severity::High;
    bad.source_signal = ".q_leading_dot";  // truncated path
    bad.dest_signal = ".sync_q";
    bad.source_domain = srcDom;
    bad.dest_domain = dstDom;
    bad.sync_type = SyncType::None;
    result.crossings.push_back(std::move(bad));

    ReportGenerator gen(result);
    auto path = fs::temp_directory_path() / "svlens_sva_leading_dot.sva";
    REQUIRE(gen.generateSVA(path));

    std::ifstream ifs(path);
    REQUIRE(ifs.good());
    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    fs::remove(path);

    // INVARIANT: never emit a leading-dot expression inside $stable.
    CHECK(content.find("!$stable(.q_leading_dot)") == std::string::npos);
    CHECK(content.find(": cover property (") == std::string::npos);
    // INVARIANT: the unsanitized source path remains in the comment
    // header so a human can still trace the finding back.
    CHECK(content.find(".q_leading_dot") != std::string::npos);
}

TEST_CASE("CDC ReportGenerator: SVA emitter doc-only block for GrayCode sync",
          "[cdc][report][sva][gray]") {
    auto result = makeCdcResult();

    CrossingReport gray;
    gray.id = "INFO-GRAY";
    gray.category = ViolationCategory::Info;
    gray.severity = Severity::Info;
    gray.source_signal = "top.fifo.gray_ptr_q";
    gray.dest_signal = "top.fifo.synced_ptr";
    gray.source_domain = result.clock_db.domains[0].get();
    gray.dest_domain = result.clock_db.domains[1].get();
    gray.sync_type = SyncType::GrayCode;
    result.crossings.push_back(std::move(gray));

    ReportGenerator gen(result);
    auto path = fs::temp_directory_path() / "svlens_sva_gray.sva";
    REQUIRE(gen.generateSVA(path));

    std::ifstream ifs(path);
    REQUIRE(ifs.good());
    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(content.find("INFO-GRAY") != std::string::npos);
    CHECK(content.find("Verified gray_code synchronizer") != std::string::npos);
    // Sanity: GrayCode is a verified sync, so no runtime cover.
    CHECK(content.find("property cdc_INFO_GRAY_src_toggle") ==
          std::string::npos);
}

TEST_CASE("CDC ReportGenerator: SVA emitter splits VIOLATION cover from INFO doc",
          "[cdc][report][sva]") {
    auto result = makeCdcResult();

    ReportGenerator generator(result);
    auto path = fs::temp_directory_path() / "svlens_cdc_report.sva";
    REQUIRE(generator.generateSVA(path, "top_module_under_test"));

    std::ifstream ifs(path);
    REQUIRE(ifs.good());
    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    fs::remove(path);

    // Header advertises top + crossing count.
    CHECK(content.find("svlens CDC analysis") != std::string::npos);
    CHECK(content.find("top_module_under_test") != std::string::npos);
    CHECK(content.find("Crossings:  2") != std::string::npos);

    // VIOLATION crossing emits a cover property using a sanitized id.
    CHECK(content.find("Crossing  : VIOLATION-001") != std::string::npos);
    CHECK(content.find("property cdc_VIOLATION_001_src_toggle") !=
          std::string::npos);
    CHECK(content.find("cover property (cdc_VIOLATION_001_src_toggle)") !=
          std::string::npos);
    // The property guards on the dest clock and asserts source toggle.
    CHECK(content.find("@(posedge top.u_b.clk_i) !$stable(top.u_a.q_data)") != std::string::npos);

    // Verified TwoFF synchronizer emits a doc-only block (no runtime
    // property to avoid bind-time signal-name fragility).
    CHECK(content.find("Crossing  : INFO-001") != std::string::npos);
    CHECK(content.find("Verified two_ff synchronizer") != std::string::npos);
    CHECK(content.find("property cdc_INFO_001_src_toggle") ==
          std::string::npos);
}

TEST_CASE("CDC ReportGenerator: linked 2FF and 3FF assertions use verified stages", "[cdc][report][sva][assert]") {
    auto result = makeCdcResult();
    auto* destDomain = result.clock_db.domains[1].get();

    auto addStage = [&](const std::string& path) {
        auto node = std::make_unique<FFNode>();
        node->hier_path = path;
        node->clock_path = path.substr(0, path.rfind('.')) + ".clk_i";
        node->declared_path = path;
        node->domain = destDomain;
        auto* ptr = node.get();
        result.ff_nodes.push_back(std::move(node));
        return ptr;
    };
    auto* first2 = addStage("top.sync2.q1");
    auto* second2 = addStage("top.sync2.q2");
    auto* first3 = addStage("top.sync3.q1");
    auto* second3 = addStage("top.sync3.q2");
    auto* third3 = addStage("top.sync3.q3");
    result.edges.push_back({first2, second2, {}, SyncType::None, false});
    result.edges.push_back({first3, second3, {}, SyncType::None, false});
    result.edges.push_back({second3, third3, {}, SyncType::None, false});

    for (auto [id, dest, type] : {std::tuple{"INFO-2", "top.sync2.q1", SyncType::TwoFF},
                                  std::tuple{"INFO-3", "top.sync3.q1", SyncType::ThreeFF}}) {
        CrossingReport crossing;
        crossing.id = id;
        crossing.category = ViolationCategory::Info;
        crossing.source_signal = "top.async_data";
        crossing.dest_signal = dest;
        crossing.source_domain = result.clock_db.domains[0].get();
        crossing.dest_domain = destDomain;
        crossing.sync_type = type;
        result.crossings.push_back(std::move(crossing));
    }

    ReportGenerator generator(result, true);
    auto svaPath = fs::temp_directory_path() / "svlens_sync_assertions.sva";
    REQUIRE(generator.generateSVA(svaPath));
    std::ifstream svaFile(svaPath);
    const std::string sva((std::istreambuf_iterator<char>(svaFile)), std::istreambuf_iterator<char>());
    fs::remove(svaPath);
    CHECK(sva.find("cdc_INFO_2_2ff: assert property") != std::string::npos);
    CHECK(sva.find("@(posedge top.sync2.clk_i)") != std::string::npos);
    CHECK(sva.find("top.sync2.q2 == $past(top.sync2.q1)") != std::string::npos);
    CHECK(sva.find("cdc_INFO_3_3ff: assert property") != std::string::npos);
    CHECK(sva.find("top.sync3.q3 == $past(top.sync3.q2)") != std::string::npos);

    auto jsonPath = fs::temp_directory_path() / "svlens_sync_assertions.json";
    generator.generateJSON(jsonPath);
    std::ifstream jsonFile(jsonPath);
    const std::string json((std::istreambuf_iterator<char>(jsonFile)), std::istreambuf_iterator<char>());
    fs::remove(jsonPath);
    CHECK(json.find("\"sva_assertion_id\": \"cdc_INFO_2_2ff\"") != std::string::npos);
    CHECK(json.find("\"sva_assertion_id\": \"cdc_INFO_3_3ff\"") != std::string::npos);

    second2->capture_conditions.push_back("en_i");
    const auto enabledPath = fs::temp_directory_path() / "svlens_sync_enabled_stage.sva";
    REQUIRE(generator.generateSVA(enabledPath));
    std::ifstream enabledFile(enabledPath);
    const std::string enabledSva((std::istreambuf_iterator<char>(enabledFile)), std::istreambuf_iterator<char>());
    fs::remove(enabledPath);
    CHECK(enabledSva.find("cdc_INFO_2_2ff: assert property") == std::string::npos);
    CHECK(enabledSva.find("cdc_INFO_3_3ff: assert property") != std::string::npos);
    second2->capture_conditions.clear();

    auto reset = std::make_unique<ResetSignal>();
    reset->hier_path = "top.sync2.rst_ni";
    reset->declared_path = reset->hier_path;
    second2->reset = reset.get();
    result.clock_db.resets.push_back(std::move(reset));
    second2->capture_conditions.push_back("rst_ni");
    const auto resetOnlyPath = fs::temp_directory_path() / "svlens_sync_reset_guard.sva";
    REQUIRE(generator.generateSVA(resetOnlyPath));
    std::ifstream resetOnlyFile(resetOnlyPath);
    const std::string resetOnlySva((std::istreambuf_iterator<char>(resetOnlyFile)), std::istreambuf_iterator<char>());
    fs::remove(resetOnlyPath);
    CHECK(resetOnlySva.find("cdc_INFO_2_2ff: assert property") != std::string::npos);
    second2->capture_conditions.clear();
    second2->reset = nullptr;

    second2->width = 2;
    const auto unequalPath = fs::temp_directory_path() / "svlens_sync_unequal_stage.sva";
    REQUIRE(generator.generateSVA(unequalPath));
    std::ifstream unequalFile(unequalPath);
    const std::string unequalSva((std::istreambuf_iterator<char>(unequalFile)), std::istreambuf_iterator<char>());
    fs::remove(unequalPath);
    CHECK(unequalSva.find("cdc_INFO_2_2ff: assert property") == std::string::npos);
    second2->width = 1;

    third3->capture_conditions.push_back("en_i");
    const auto thirdEnabledPath = fs::temp_directory_path() / "svlens_sync_enabled_third_stage.sva";
    REQUIRE(generator.generateSVA(thirdEnabledPath));
    std::ifstream thirdEnabledFile(thirdEnabledPath);
    const std::string thirdEnabledSva((std::istreambuf_iterator<char>(thirdEnabledFile)),
                                      std::istreambuf_iterator<char>());
    fs::remove(thirdEnabledPath);
    CHECK(thirdEnabledSva.find("cdc_INFO_2_2ff: assert property") != std::string::npos);
    CHECK(thirdEnabledSva.find("cdc_INFO_3_3ff: assert property") == std::string::npos);
    third3->capture_conditions.clear();

    first2->declared_path.clear();
    const auto skippedPath = fs::temp_directory_path() / "svlens_sync_opaque_stage.sva";
    REQUIRE(generator.generateSVA(skippedPath));
    std::ifstream skippedFile(skippedPath);
    const std::string skipped((std::istreambuf_iterator<char>(skippedFile)), std::istreambuf_iterator<char>());
    fs::remove(skippedPath);
    CHECK(skipped.find("cdc_INFO_2_2ff: assert property") == std::string::npos);
    CHECK(skipped.find("cdc_INFO_3_3ff: assert property") != std::string::npos);
}

TEST_CASE("CDC ReportGenerator: verified prim_fifo_async pointer gets a linked Gray assertion",
          "[cdc][report][sva][fifo]") {
    auto result = makeCdcResult();
    auto reset = std::make_unique<ResetSignal>();
    reset->hier_path = "top.fifo.rst_wr_ni";
    reset->is_async = true;
    reset->polarity = ResetSignal::Polarity::ActiveLow;
    auto* resetPtr = reset.get();
    result.clock_db.resets.push_back(std::move(reset));
    auto pointer = std::make_unique<FFNode>();
    pointer->hier_path = "top.fifo.fifo_wptr_gray_q";
    pointer->domain = result.clock_db.domains[0].get();
    pointer->reset = resetPtr;
    pointer->width = 3;
    pointer->primitive_name = "prim_fifo_async";
    auto* pointerPtr = pointer.get();
    result.ff_nodes.push_back(std::move(pointer));

    CrossingReport crossing;
    crossing.id = "CAUTION-FIFO";
    crossing.category = ViolationCategory::Caution;
    crossing.source_signal = pointerPtr->hier_path;
    crossing.dest_signal = "top.fifo.sync_wptr.q1";
    crossing.source_domain = pointerPtr->domain;
    crossing.dest_domain = result.clock_db.domains[1].get();
    crossing.sync_type = SyncType::AsyncFIFO;
    result.crossings.push_back(crossing);

    ReportGenerator generator(result, true);
    const auto svaPath = fs::temp_directory_path() / "svlens_fifo_gray_assertion.sva";
    REQUIRE(generator.generateSVA(svaPath));
    std::ifstream svaFile(svaPath);
    const std::string sva((std::istreambuf_iterator<char>(svaFile)), std::istreambuf_iterator<char>());
    fs::remove(svaPath);
    CHECK(sva.find("cdc_CAUTION_FIFO_fifo_gray: assert property") != std::string::npos);
    CHECK(sva.find("$countones(top.fifo.fifo_wptr_gray_q ^ $past(top.fifo.fifo_wptr_gray_q)) <= 1") !=
          std::string::npos);
    CHECK(sva.find("disable iff ((!top.fifo.rst_wr_ni) !== '0)") != std::string::npos);
    CHECK(sva.find("@(posedge top.fifo.clk_wr_i)") != std::string::npos);
    CHECK(slang::syntax::SyntaxTree::fromText(sva)->diagnostics().empty());

    const auto jsonPath = fs::temp_directory_path() / "svlens_fifo_gray_assertion.json";
    generator.generateJSON(jsonPath);
    std::ifstream jsonFile(jsonPath);
    const std::string json((std::istreambuf_iterator<char>(jsonFile)), std::istreambuf_iterator<char>());
    fs::remove(jsonPath);
    CHECK(json.find("\"sva_assertion_id\": \"cdc_CAUTION_FIFO_fifo_gray\"") != std::string::npos);

    const auto skippedPath = fs::temp_directory_path() / "svlens_fifo_gray_skipped.sva";
    auto emitsFifoGray = [&]() {
        CHECK(generator.generateSVA(skippedPath));
        std::ifstream skippedFile(skippedPath);
        const std::string skipped((std::istreambuf_iterator<char>(skippedFile)), std::istreambuf_iterator<char>());
        fs::remove(skippedPath);
        return skipped.find("cdc_CAUTION_FIFO_fifo_gray: assert property") != std::string::npos;
    };
    pointerPtr->primitive_name = "unverified_fifo";
    CHECK_FALSE(emitsFifoGray());
    pointerPtr->primitive_name = "prim_fifo_async";
    pointerPtr->width = 1;
    CHECK_FALSE(emitsFifoGray());
    pointerPtr->width = 3;
    resetPtr->is_async = false;
    CHECK_FALSE(emitsFifoGray());
    resetPtr->is_async = true;
    resetPtr->polarity = ResetSignal::Polarity::ActiveHigh;
    CHECK_FALSE(emitsFifoGray());
    resetPtr->polarity = ResetSignal::Polarity::ActiveLow;
    result.crossings.back().dest_signal = "top.other.q1";
    CHECK_FALSE(emitsFifoGray());
    result.crossings.back().dest_signal = "top.fifo.sync_wptr.q1";
    result.crossings.back().category = ViolationCategory::Violation;
    CHECK_FALSE(emitsFifoGray());
    result.crossings.back().category = ViolationCategory::Caution;
    result.crossings.back().sync_type = SyncType::None;
    CHECK_FALSE(emitsFifoGray());
    result.crossings.back().sync_type = SyncType::AsyncFIFO;
    pointerPtr->hier_path = "top.fifo.fifo_rptr_gray_q";
    resetPtr->hier_path = "top.fifo.rst_rd_ni";
    result.crossings.back().source_signal = pointerPtr->hier_path;
    result.crossings.back().dest_signal = "top.fifo.sync_rptr.q1";
    CHECK(emitsFifoGray());

    result.crossings.back().dest_signal = "top.other.q1";
    generator.generateJSON(jsonPath);
    std::ifstream skippedJsonFile(jsonPath);
    const std::string skippedJson((std::istreambuf_iterator<char>(skippedJsonFile)), std::istreambuf_iterator<char>());
    fs::remove(jsonPath);
    CHECK(skippedJson.find("cdc_CAUTION_FIFO_fifo_gray") == std::string::npos);
}

TEST_CASE("CDC ReportGenerator: FIFO Gray and 2FF stage assertions both link to one crossing",
          "[cdc][report][sva][fifo][assert]") {
    auto result = makeCdcResult();
    auto reset = std::make_unique<ResetSignal>();
    reset->hier_path = "top.fifo.rst_wr_ni";
    reset->is_async = true;
    auto* resetPtr = reset.get();
    result.clock_db.resets.push_back(std::move(reset));
    auto source = std::make_unique<FFNode>();
    source->hier_path = "top.fifo.fifo_wptr_gray_q";
    source->domain = result.clock_db.domains[0].get();
    source->reset = resetPtr;
    source->primitive_name = "prim_fifo_async";
    source->width = 4;
    result.ff_nodes.push_back(std::move(source));
    auto first = std::make_unique<FFNode>();
    first->hier_path = "top.fifo.sync_wptr.q1";
    first->clock_path = "top.fifo.sync_wptr.clk_i";
    first->declared_path = first->hier_path;
    first->domain = result.clock_db.domains[1].get();
    auto* firstPtr = first.get();
    result.ff_nodes.push_back(std::move(first));
    auto second = std::make_unique<FFNode>();
    second->hier_path = "top.fifo.sync_wptr.q2";
    second->declared_path = second->hier_path;
    second->domain = firstPtr->domain;
    auto* secondPtr = second.get();
    result.ff_nodes.push_back(std::move(second));
    result.edges.push_back({firstPtr, secondPtr, {}, SyncType::None, false});

    CrossingReport crossing;
    crossing.id = "INFO-FIFO";
    crossing.category = ViolationCategory::Info;
    crossing.source_signal = "top.fifo.fifo_wptr_gray_q";
    crossing.dest_signal = firstPtr->hier_path;
    crossing.source_domain = result.clock_db.domains[0].get();
    crossing.dest_domain = firstPtr->domain;
    crossing.sync_type = SyncType::TwoFF;
    result.crossings.push_back(crossing);

    ReportGenerator generator(result, true);
    const auto svaPath = fs::temp_directory_path() / "svlens_fifo_two_assertions.sva";
    REQUIRE(generator.generateSVA(svaPath));
    std::ifstream svaFile(svaPath);
    const std::string sva((std::istreambuf_iterator<char>(svaFile)), std::istreambuf_iterator<char>());
    fs::remove(svaPath);
    CHECK(sva.find("cdc_INFO_FIFO_2ff: assert property") != std::string::npos);
    CHECK(sva.find("cdc_INFO_FIFO_fifo_gray: assert property") != std::string::npos);
    CHECK(slang::syntax::SyntaxTree::fromText(sva)->diagnostics().empty());

    const auto jsonPath = fs::temp_directory_path() / "svlens_fifo_two_assertions.json";
    generator.generateJSON(jsonPath);
    std::ifstream jsonFile(jsonPath);
    const std::string json((std::istreambuf_iterator<char>(jsonFile)), std::istreambuf_iterator<char>());
    fs::remove(jsonPath);
    CHECK(json.find("\"sva_assertion_id\": \"cdc_INFO_FIFO_2ff\"") != std::string::npos);
    CHECK(json.find("\"sva_assertion_ids\": [\"cdc_INFO_FIFO_2ff\", \"cdc_INFO_FIFO_fifo_gray\"]") !=
          std::string::npos);
}

TEST_CASE("CDC ReportGenerator: verified prim_sync_reqack emits ACK-needs-REQ assertion",
          "[cdc][report][sva][handshake]") {
    auto result = makeCdcResult();
    auto sourceReset = std::make_unique<ResetSignal>();
    sourceReset->hier_path = "top.u_reqack.rst_src_ni";
    sourceReset->is_async = true;
    auto* sourceResetPtr = sourceReset.get();
    result.clock_db.resets.push_back(std::move(sourceReset));
    auto destReset = std::make_unique<ResetSignal>();
    destReset->hier_path = "top.u_reqack.req_sync.u_sync_1.rst_ni";
    destReset->is_async = true;
    auto* destResetPtr = destReset.get();
    result.clock_db.resets.push_back(std::move(destReset));
    auto source = std::make_unique<FFNode>();
    source->hier_path = "top.u_reqack.src_req_q";
    source->domain = result.clock_db.domains[0].get();
    source->reset = sourceResetPtr;
    source->primitive_name = "prim_sync_reqack";
    auto* sourcePtr = source.get();
    result.ff_nodes.push_back(std::move(source));
    auto dest = std::make_unique<FFNode>();
    dest->hier_path = "top.u_reqack.req_sync.u_sync_1.q_o";
    dest->domain = result.clock_db.domains[1].get();
    dest->reset = destResetPtr;
    auto* destPtr = dest.get();
    result.ff_nodes.push_back(std::move(dest));
    result.clock_db.directed_aliases["top.u_reqack.rst_dst_ni"] = {"top.u_reqack.req_sync.rst_ni"};
    result.clock_db.directed_aliases["top.u_reqack.req_sync.rst_ni"] = {destResetPtr->hier_path};

    CrossingReport crossing;
    crossing.id = "INFO-HANDSHAKE";
    crossing.category = ViolationCategory::Info;
    crossing.source_signal = sourcePtr->hier_path;
    crossing.dest_signal = destPtr->hier_path;
    crossing.source_domain = sourcePtr->domain;
    crossing.dest_domain = destPtr->domain;
    crossing.sync_type = SyncType::Handshake;
    result.crossings.push_back(crossing);

    ReportGenerator generator(result, true);
    const auto svaPath = fs::temp_directory_path() / "svlens_reqack_assertion.sva";
    REQUIRE(generator.generateSVA(svaPath));
    std::ifstream svaFile(svaPath);
    const std::string sva((std::istreambuf_iterator<char>(svaFile)), std::istreambuf_iterator<char>());
    fs::remove(svaPath);
    CHECK(sva.find("cdc_INFO_HANDSHAKE_ack_requires_req: assert property") != std::string::npos);
    CHECK(sva.find("top.u_reqack.dst_ack_i |-> top.u_reqack.dst_req_o") != std::string::npos);
    CHECK(sva.find("disable iff ((!top.u_reqack.rst_dst_ni) !== '0)") != std::string::npos);
    CHECK(sva.find("@(posedge top.u_reqack.clk_dst_i)") != std::string::npos);
    CHECK(slang::syntax::SyntaxTree::fromText(sva)->diagnostics().empty());

    const auto jsonPath = fs::temp_directory_path() / "svlens_reqack_assertion.json";
    generator.generateJSON(jsonPath);
    std::ifstream jsonFile(jsonPath);
    const std::string json((std::istreambuf_iterator<char>(jsonFile)), std::istreambuf_iterator<char>());
    fs::remove(jsonPath);
    CHECK(json.find("\"sva_assertion_id\": \"cdc_INFO_HANDSHAKE_ack_requires_req\"") != std::string::npos);

    const auto skippedPath = fs::temp_directory_path() / "svlens_reqack_skipped.sva";
    auto emitsReqAck = [&]() {
        CHECK(generator.generateSVA(skippedPath));
        std::ifstream skippedFile(skippedPath);
        const std::string skipped((std::istreambuf_iterator<char>(skippedFile)), std::istreambuf_iterator<char>());
        fs::remove(skippedPath);
        return skipped.find("cdc_INFO_HANDSHAKE_ack_requires_req: assert property") != std::string::npos;
    };
    sourcePtr->primitive_name = "unverified_reqack";
    CHECK_FALSE(emitsReqAck());
    sourcePtr->primitive_name = "prim_sync_reqack";
    sourcePtr->width = 2;
    CHECK_FALSE(emitsReqAck());
    sourcePtr->width = 1;
    sourceResetPtr->is_async = false;
    CHECK_FALSE(emitsReqAck());
    sourceResetPtr->is_async = true;
    destResetPtr->is_async = false;
    CHECK_FALSE(emitsReqAck());
    destResetPtr->is_async = true;
    result.crossings.back().category = ViolationCategory::Violation;
    CHECK_FALSE(emitsReqAck());
    result.crossings.back().category = ViolationCategory::Info;
    sourcePtr->hier_path = "top.u_reqack.dst_ack_q";
    result.crossings.back().source_signal = sourcePtr->hier_path;
    CHECK_FALSE(emitsReqAck());
    sourcePtr->hier_path = "top.u_reqack.src_req_q";
    result.crossings.back().source_signal = sourcePtr->hier_path;
    destPtr->hier_path = "top.u_reqack.other.q_o";
    result.crossings.back().dest_signal = destPtr->hier_path;
    CHECK_FALSE(emitsReqAck());
    destPtr->hier_path = "top.u_reqack.req_sync.u_sync_1.q_o";
    result.crossings.back().dest_signal = destPtr->hier_path;
    result.clock_db.directed_aliases.clear();
    CHECK_FALSE(emitsReqAck());
    generator.generateJSON(jsonPath);
    std::ifstream skippedJsonFile(jsonPath);
    const std::string skippedJson((std::istreambuf_iterator<char>(skippedJsonFile)), std::istreambuf_iterator<char>());
    fs::remove(jsonPath);
    CHECK(skippedJson.find("cdc_INFO_HANDSHAKE_ack_requires_req") == std::string::npos);
}

TEST_CASE("CDC ReportGenerator: HTML embeds navigable crossing data safely", "[cdc][report][html]") {
    auto result = makeCdcResult();
    result.crossings[0].recommendation = "review </script> path";
    ReportGenerator generator(result);
    auto path = fs::temp_directory_path() / "svlens_cdc_report.html";
    generator.generateHTML(path);

    std::ifstream file(path);
    REQUIRE(file.good());
    const std::string html((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(html.find("id=\"crossing-list\"") != std::string::npos);
    CHECK(html.find("id=\"module-filter\"") != std::string::npos);
    CHECK(html.find("VIOLATION-001") != std::string::npos);
    CHECK(html.find("review <\\/script> path") != std::string::npos);
    CHECK(html.find("review </script> path") == std::string::npos);
}

TEST_CASE("CDC ReportGenerator: reset usage groups FF sinks by reset path", "[cdc][report][reset]") {
    auto result = makeCdcResult();
    auto reset = std::make_unique<ResetSignal>();
    reset->hier_path = "top.rst_n";
    reset->is_async = true;
    reset->driver_ff_path = "top.reset_q";
    reset->source_domain = "sys_clk";
    reset->driver_inverted = true;
    auto* resetPtr = reset.get();
    result.clock_db.resets.push_back(std::move(reset));
    for (size_t domain = 0; domain < 2; ++domain) {
        auto ff = std::make_unique<FFNode>();
        ff->hier_path = "top.q" + std::to_string(domain);
        ff->domain = result.clock_db.domains[domain].get();
        ff->reset = resetPtr;
        result.ff_nodes.push_back(std::move(ff));
    }
    ReportGenerator generator(result);
    auto path = fs::temp_directory_path() / "svlens_reset_usage.json";
    generator.generateJSON(path);
    std::ifstream file(path);
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    fs::remove(path);
    CHECK(json.find("\"reset_usage\": [") != std::string::npos);
    CHECK(json.find("\"signal\": \"top.rst_n\"") != std::string::npos);
    CHECK(json.find("\"ff_count\": 2") != std::string::npos);
    CHECK(json.find("\"driver_ff\": \"top.reset_q\"") != std::string::npos);
    CHECK(json.find("\"driver_inverted\": true") != std::string::npos);
    CHECK(json.find("\"source_domain\": \"sys_clk\"") != std::string::npos);
    CHECK(json.find("\"dest_domains\": [\"ext_clk\", \"sys_clk\"]") != std::string::npos);
}

TEST_CASE("CDC ReportGenerator: conflicting mux-branch FF polarity stays ambiguous", "[cdc][report][reset][mux]") {
    auto result = makeCdcResult();
    auto reset = std::make_unique<ResetSignal>();
    reset->hier_path = "top.rst_n";
    reset->is_async = true;
    auto* resetPtr = reset.get();
    result.clock_db.resets.push_back(std::move(reset));

    auto source = std::make_unique<FFNode>();
    source->hier_path = "top.reset_q";
    source->domain = result.clock_db.domains[0].get();
    result.ff_nodes.push_back(std::move(source));
    auto sink = std::make_unique<FFNode>();
    sink->hier_path = "top.sink_q";
    sink->domain = result.clock_db.domains[1].get();
    sink->reset = resetPtr;
    result.ff_nodes.push_back(std::move(sink));

    result.clock_db.directed_aliases["top.reset_q"] = {"top.u_mux.clk0_i"};
    result.clock_db.reset_inversions["top.reset_q"] = {"top.u_mux.clk0_i"};
    result.clock_db.directed_aliases["top.u_mux.clk_o"] = {"top.rst_n"};
    result.clock_db.reset_mux_inputs["top.u_mux.clk_o"] =
        ResetMuxInputs{"top.u_mux.clk0_i", "top.u_mux.clk1_i", "top.u_mux.sel_i"};

    const auto path = fs::temp_directory_path() / ("svlens_reset_mux_polarity_" + std::to_string(::getpid()) + ".json");
    ReportGenerator(result).generateJSON(path);
    std::ifstream file(path);
    REQUIRE(file.good());
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    fs::remove(path);
    const auto signalPos = json.find("\"signal\": \"top.rst_n\"");
    REQUIRE(signalPos != std::string::npos);
    const auto lineEnd = json.find('\n', signalPos);
    const auto record = json.substr(signalPos, lineEnd - signalPos);
    CHECK(record.find("\"input0_candidate_ffs\": [{\"path\": \"top.reset_q\", \"domain\": \"sys_clk\", "
                      "\"inversion_ambiguous\": true}]") != std::string::npos);
    CHECK(record.find("\"inverted\"") == std::string::npos);
    CHECK(record.find("\"driver_ff\"") == std::string::npos);
}

TEST_CASE("CDC ReportGenerator: mixed reset provenance is not published as unique", "[cdc][report][reset]") {
    auto result = makeCdcResult();
    for (size_t i = 0; i < 2; ++i) {
        auto reset = std::make_unique<ResetSignal>();
        reset->hier_path = "top.rst_n";
        reset->is_async = true;
        if (i == 0) {
            reset->driver_ff_path = "top.reset_q";
            reset->source_domain = "sys_clk";
        }
        auto ff = std::make_unique<FFNode>();
        ff->hier_path = "top.q" + std::to_string(i);
        ff->domain = result.clock_db.domains[i].get();
        ff->reset = reset.get();
        result.clock_db.resets.push_back(std::move(reset));
        result.ff_nodes.push_back(std::move(ff));
    }
    const auto path = fs::temp_directory_path() / "svlens_reset_mixed_provenance.json";
    ReportGenerator(result).generateJSON(path);
    std::ifstream file(path);
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    fs::remove(path);
    CHECK(json.find("\"ff_count\": 2") != std::string::npos);
    CHECK(json.find("\"driver_ff\"") == std::string::npos);
}

TEST_CASE("CDC ReportGenerator: conflicting reset inversion parity omits driver provenance", "[cdc][report][reset]") {
    auto result = makeCdcResult();
    for (size_t i = 0; i < 2; ++i) {
        auto reset = std::make_unique<ResetSignal>();
        reset->hier_path = "top.rst_n";
        reset->is_async = true;
        reset->driver_ff_path = "top.reset_q";
        reset->source_domain = "sys_clk";
        reset->driver_inverted = i == 0;
        auto ff = std::make_unique<FFNode>();
        ff->hier_path = "top.q" + std::to_string(i);
        ff->domain = result.clock_db.domains[i].get();
        ff->reset = reset.get();
        result.clock_db.resets.push_back(std::move(reset));
        result.ff_nodes.push_back(std::move(ff));
    }
    const auto path = fs::temp_directory_path() / "svlens_reset_mixed_inversion.json";
    ReportGenerator(result).generateJSON(path);
    std::ifstream file(path);
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    fs::remove(path);
    CHECK(json.find("\"ff_count\": 2") != std::string::npos);
    CHECK(json.find("\"driver_ff\"") == std::string::npos);
    CHECK(json.find("\"driver_inverted\"") == std::string::npos);
}
