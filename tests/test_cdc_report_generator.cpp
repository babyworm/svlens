#include <catch2/catch_test_macros.hpp>
#include "sv-cdccheck/report_generator.h"
#include "sv-cdccheck/types.h"

#include <filesystem>
#include <fstream>

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

    return result;
}

TEST_CASE("CDC ReportGenerator: counts reflect crossing categories", "[cdc][report]") {
    auto result = makeCdcResult();
    CHECK(result.violation_count() == 1);
    CHECK(result.info_count() == 1);
    CHECK(result.caution_count() == 0);
}

TEST_CASE("CDC ReportGenerator: markdown output contains summary and domains", "[cdc][report]") {
    auto result = makeCdcResult();
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
}

TEST_CASE("CDC ReportGenerator: json output contains summary and crossing ids", "[cdc][report]") {
    auto result = makeCdcResult();
    ReportGenerator generator(result);

    auto path = fs::temp_directory_path() / "svlens_cdc_report.json";
    generator.generateJSON(path);

    std::ifstream ifs(path);
    REQUIRE(ifs.good());
    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    fs::remove(path);

    CHECK(content.find("\"summary\"") != std::string::npos);
    CHECK(content.find("\"violations\": 1") != std::string::npos);
    CHECK(content.find("VIOLATION-001") != std::string::npos);
    CHECK(content.find("INFO-001") != std::string::npos);
    CHECK(content.find("\"relationship\": \"asynchronous\"") != std::string::npos);
    CHECK(content.find("\"rationale\": \"Related clocks share timing constraints\"") != std::string::npos);
    CHECK(content.find("\"timing_basis_ns\": 8") != std::string::npos);
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
    CHECK(content.find("@(posedge ext_clk) !$stable(top.u_a.q_data)") !=
          std::string::npos);

    // Verified TwoFF synchronizer emits a doc-only block (no runtime
    // property to avoid bind-time signal-name fragility).
    CHECK(content.find("Crossing  : INFO-001") != std::string::npos);
    CHECK(content.find("Verified two_ff synchronizer") != std::string::npos);
    CHECK(content.find("property cdc_INFO_001_src_toggle") ==
          std::string::npos);
}
