#include <catch2/catch_test_macros.hpp>
#include "sv-cdccheck/clock_tree.h"
#include "sv-cdccheck/ff_classifier.h"
#include "sv-cdccheck/connectivity.h"
#include "sv-cdccheck/crossing_detector.h"
#include "sv-cdccheck/sync_verifier.h"
#include "slang/driver/Driver.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/BlockSymbols.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace sv_cdccheck;

// Compile a .sv file from disk
static std::unique_ptr<slang::ast::Compilation> compileFixture(const std::string& path) {
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

// Run the 6-pass pipeline and collect counts
struct GoldenResult {
    int violations = 0;
    int infos = 0;
    int crossings = 0;
};

static GoldenResult runGoldenPipeline(slang::ast::Compilation& compilation) {
    GoldenResult r;
    ClockDatabase db;
    ClockTreeAnalyzer ct(compilation, db);
    ct.analyze();

    FFClassifier classifier(compilation, db);
    classifier.analyze();

    ConnectivityBuilder conn(compilation, classifier.getFFNodes());
    conn.analyze();
    auto edges = conn.getEdges();

    CrossingDetector det(edges, db);
    det.analyze();
    auto crossings = det.getCrossings();

    SyncVerifier sv(crossings, classifier.getFFNodes(), edges);
    sv.analyze();

    r.crossings = static_cast<int>(crossings.size());
    for (auto& c : crossings) {
        if (c.category == ViolationCategory::Violation) r.violations++;
        if (c.category == ViolationCategory::Info) r.infos++;
    }
    return r;
}

// Parse a simple golden JSON: {"expected_violations": N, "expected_infos": N, "expected_crossings": N}
struct GoldenExpected {
    int expected_violations = -1;
    int expected_infos = -1;
    int expected_crossings = -1;
};

static int extractJsonInt(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return -1;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return -1;
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    std::string num;
    while (pos < json.size() && (json[pos] >= '0' && json[pos] <= '9')) {
        num += json[pos++];
    }
    return num.empty() ? -1 : std::stoi(num);
}

static GoldenExpected parseGolden(const std::string& path) {
    std::ifstream f(path);
    std::stringstream buf;
    buf << f.rdbuf();
    std::string json = buf.str();

    GoldenExpected e;
    e.expected_violations = extractJsonInt(json, "expected_violations");
    e.expected_infos = extractJsonInt(json, "expected_infos");
    e.expected_crossings = extractJsonInt(json, "expected_crossings");
    return e;
}

// Locate directories
static std::string findDir(const std::string& subdir) {
    std::string candidates[] = {
        subdir,
        "../" + subdir,
        "../../" + subdir,
        std::string(SOURCE_DIR) + "/" + subdir
    };
    for (auto& c : candidates)
        if (fs::exists(c)) return c;
    return std::string(SOURCE_DIR) + "/" + subdir;
}

TEST_CASE("Golden: 01_no_crossing", "[golden]") {
    auto sv_path = findDir("tests/basic") + "/01_no_crossing.sv";
    auto golden_path = findDir("tests/golden") + "/01_no_crossing.json";
    if (!fs::exists(sv_path) || !fs::exists(golden_path)) {
        WARN("Fixture or golden not found"); return;
    }

    auto compilation = compileFixture(sv_path);
    auto result = runGoldenPipeline(*compilation);
    auto expected = parseGolden(golden_path);

    CHECK(result.violations == expected.expected_violations);
    CHECK(result.crossings == expected.expected_crossings);
}

TEST_CASE("Golden: 02_missing_sync", "[golden]") {
    auto sv_path = findDir("tests/basic") + "/02_missing_sync.sv";
    auto golden_path = findDir("tests/golden") + "/02_missing_sync.json";
    if (!fs::exists(sv_path) || !fs::exists(golden_path)) {
        WARN("Fixture or golden not found"); return;
    }

    auto compilation = compileFixture(sv_path);
    auto result = runGoldenPipeline(*compilation);
    auto expected = parseGolden(golden_path);

    CHECK(result.violations == expected.expected_violations);
    CHECK(result.infos == expected.expected_infos);
    CHECK(result.crossings == expected.expected_crossings);
}

TEST_CASE("Golden: 03_two_ff_sync", "[golden]") {
    auto sv_path = findDir("tests/basic") + "/03_two_ff_sync.sv";
    auto golden_path = findDir("tests/golden") + "/03_two_ff_sync.json";
    if (!fs::exists(sv_path) || !fs::exists(golden_path)) {
        WARN("Fixture or golden not found"); return;
    }

    auto compilation = compileFixture(sv_path);
    auto result = runGoldenPipeline(*compilation);
    auto expected = parseGolden(golden_path);

    CHECK(result.violations == expected.expected_violations);
    CHECK(result.infos >= expected.expected_infos);
    CHECK(result.crossings >= expected.expected_crossings);
}

TEST_CASE("Golden: 04_three_ff_sync", "[golden]") {
    auto sv_path = findDir("tests/basic") + "/04_three_ff_sync.sv";
    auto golden_path = findDir("tests/golden") + "/04_three_ff_sync.json";
    if (!fs::exists(sv_path) || !fs::exists(golden_path)) {
        WARN("Fixture or golden not found"); return;
    }

    auto compilation = compileFixture(sv_path);
    auto result = runGoldenPipeline(*compilation);
    auto expected = parseGolden(golden_path);

    CHECK(result.violations == expected.expected_violations);
    CHECK(result.infos >= expected.expected_infos);
    CHECK(result.crossings >= expected.expected_crossings);
}

TEST_CASE("Golden: 05_comb_before_sync", "[golden]") {
    auto sv_path = findDir("tests/basic") + "/05_comb_before_sync.sv";
    auto golden_path = findDir("tests/golden") + "/05_comb_before_sync.json";
    if (!fs::exists(sv_path) || !fs::exists(golden_path)) {
        WARN("Fixture or golden not found"); return;
    }

    auto compilation = compileFixture(sv_path);
    auto result = runGoldenPipeline(*compilation);
    auto expected = parseGolden(golden_path);

    CHECK(result.violations == expected.expected_violations);
    CHECK(result.crossings >= expected.expected_crossings);
}
