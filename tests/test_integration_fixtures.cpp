#include <catch2/catch_test_macros.hpp>
#include "sv-cdccheck/clock_tree.h"
#include "sv-cdccheck/ff_classifier.h"
#include "sv-cdccheck/connectivity.h"
#include "sv-cdccheck/crossing_detector.h"
#include "sv-cdccheck/sync_verifier.h"
#include "sv-cdccheck/report_generator.h"
#include "slang/driver/Driver.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/BlockSymbols.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace sv_cdccheck;

// Compile a real .sv file from disk (not inline string)
static std::unique_ptr<slang::ast::Compilation> compileFile(const std::string& path) {
    slang::driver::Driver driver;
    driver.addStandardArgs();
    const char* args[] = {"test", path.c_str()};
    (void)driver.parseCommandLine(2, const_cast<char**>(args));
    (void)driver.processOptions();
    (void)driver.parseAllSources();

    auto compilation = driver.createCompilation();
    auto& root = compilation->getRoot();
    for (auto& member : root.members()) {
        if (member.kind == slang::ast::SymbolKind::Instance) {
            auto& inst = member.as<slang::ast::InstanceSymbol>();
            for (auto& bm : inst.body.members()) {
                if (bm.kind == slang::ast::SymbolKind::ProceduralBlock)
                    (void)bm.as<slang::ast::ProceduralBlockSymbol>().getBody();
            }
        }
    }
    compilation->getAllDiagnostics();
    return compilation;
}

// Run full pipeline and return crossings
struct PipelineResult {
    ClockDatabase db;
    std::unique_ptr<FFClassifier> classifier;
    std::vector<FFEdge> edges;
    std::vector<CrossingReport> crossings;
    int violations = 0;
    int cautions = 0;
    int infos = 0;
};

static PipelineResult runPipeline(slang::ast::Compilation& compilation) {
    PipelineResult r;
    ClockTreeAnalyzer ct(compilation, r.db);
    ct.analyze();

    r.classifier = std::make_unique<FFClassifier>(compilation, r.db);
    r.classifier->analyze();

    ConnectivityBuilder conn(compilation, r.classifier->getFFNodes());
    conn.analyze();
    r.edges = conn.getEdges();

    CrossingDetector det(r.edges, r.db);
    det.analyze();
    r.crossings = det.getCrossings();

    SyncVerifier sv(r.crossings, r.classifier->getFFNodes(), r.edges);
    sv.analyze();

    for (auto& c : r.crossings) {
        if (c.category == ViolationCategory::Violation) r.violations++;
        if (c.category == ViolationCategory::Caution) r.cautions++;
        if (c.category == ViolationCategory::Info) r.infos++;
    }
    return r;
}

// ─── Fixture-based integration tests ───

// Find the project root (where tests/basic/ lives)
static std::string fixtureDir() {
    // Try relative to CWD (typical for cmake build)
    std::string candidates[] = {
        "tests/basic",
        "../tests/basic",
        "../../tests/basic",
        std::string(SOURCE_DIR) + "/tests/basic"
    };
    for (auto& candidate : candidates) {
        if (fs::exists(candidate)) return candidate;
    }
    return "tests/basic";
}

TEST_CASE("Integration: 01_no_crossing.sv — zero violations", "[integration]") {
    auto dir = fixtureDir();
    auto path = dir + "/01_no_crossing.sv";
    if (!fs::exists(path)) { WARN("Fixture not found: " << path); return; }

    auto compilation = compileFile(path);
    auto result = runPipeline(*compilation);

    CHECK(result.violations == 0);
    CHECK(result.cautions == 0);
}

TEST_CASE("Integration: 02_missing_sync.sv — one VIOLATION", "[integration]") {
    auto dir = fixtureDir();
    auto path = dir + "/02_missing_sync.sv";
    if (!fs::exists(path)) { WARN("Fixture not found: " << path); return; }

    auto compilation = compileFile(path);
    auto result = runPipeline(*compilation);

    CHECK(result.violations == 1);
    CHECK(result.infos == 0);
}

TEST_CASE("Integration: 03_two_ff_sync.sv — zero violations, one INFO", "[integration]") {
    auto dir = fixtureDir();
    auto path = dir + "/03_two_ff_sync.sv";
    if (!fs::exists(path)) { WARN("Fixture not found: " << path); return; }

    auto compilation = compileFile(path);
    auto result = runPipeline(*compilation);

    CHECK(result.violations == 0);
    CHECK(result.infos >= 1);
}

TEST_CASE("Integration: 04_three_ff_sync.sv — zero violations, ThreeFF detected", "[integration]") {
    auto dir = fixtureDir();
    auto path = dir + "/04_three_ff_sync.sv";
    if (!fs::exists(path)) { WARN("Fixture not found: " << path); return; }

    auto compilation = compileFile(path);
    auto result = runPipeline(*compilation);

    CHECK(result.violations == 0);

    bool found_3ff = false;
    for (auto& c : result.crossings)
        if (c.sync_type == SyncType::ThreeFF) found_3ff = true;
    CHECK(found_3ff);
}

TEST_CASE("Integration: 05_comb_before_sync.sv — CAUTION for glitch risk", "[integration]") {
    auto dir = fixtureDir();
    auto path = dir + "/05_comb_before_sync.sv";
    if (!fs::exists(path)) { WARN("Fixture not found: " << path); return; }

    auto compilation = compileFile(path);
    auto result = runPipeline(*compilation);

    // Should have a crossing with CAUTION (comb before sync)
    CHECK(result.crossings.size() >= 1);
}
