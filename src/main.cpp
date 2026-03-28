#include "AnalysisEngine.h"
#include "CheckerRunner.h"
#include "ClockResetAnalyzer.h"
#include "ConnectionExtractor.h"
#include "ConventionChecker.h"
#include "CsvReport.h"
#include "DanglingChecker.h"
#include "DotReport.h"
#include "ExpectChecker.h"
#include "GraphDiff.h"
#include "InterfaceGrouper.h"
#include "HtmlReport.h"
#include "TraceEngine.h"
#include "JsonReport.h"
#include "MarkdownReport.h"
#include "ProtocolChecker.h"
#include "ReportGenerator.h"
#include "TableReport.h"
#include "TypeChecker.h"
#include "UndrivenChecker.h"
#include "WaiverFilter.h"
#include "WidthChecker.h"
#include "slang/driver/Driver.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fmt/core.h>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

static void printUsage() {
    fmt::print(
        "slang-connect v0.1.0 -- Module interconnect verification\n\n"
        "Usage: slang-connect [OPTIONS] <SV_FILES...>\n\n"
        "Required:\n"
        "  --top <module>          Top-level module\n\n"
        "Options:\n"
        "  -o, --output <dir>      Output directory (default: ./connect_reports/)\n"
        "  --format <fmt>          json|md|csv|table|dot|html|all (default: all)\n"
        "  --help                  Show this help message\n\n"
        "Analysis (all enabled by default, --no-* to disable):\n"
        "  --no-check-width        Disable width mismatch checking\n"
        "  --no-check-type         Disable type mismatch checking\n"
        "  --no-check-dangling     Disable dangling output checking\n"
        "  --no-check-undriven     Disable undriven input checking\n"
        "  --check-protocol        Enable protocol completeness checking\n"
        "  --check-convention      Enable naming convention checking\n"
        "  --check-clock-reset     Enable clock/reset topology analysis\n"
        "  --convention <file>     Custom convention rules (YAML, optional)\n"
        "  --expect <file>         Expected/forbidden connectivity spec (YAML)\n"
        "  --depth <n>             Hierarchy depth (default: unlimited, -1)\n\n"
        "Tracing:\n"
        "  --trace <pattern>       Trace signal fan-out and fan-in (glob pattern)\n\n"
        "Comparison:\n"
        "  --diff <file>           Compare against a baseline JSON report\n\n"
        "Filtering:\n"
        "  --waiver <file>         YAML waiver file\n\n"
        "All other options (e.g. -I, -D, --std, -f) are passed to slang.\n");
}

struct CliOptions {
    std::string topModule;
    std::string outputDir = "./connect_reports/";
    std::string format = "all";
    std::string waiverFile;
    int depth = -1;
    bool checkWidth = true;
    bool checkType = true;
    bool checkDangling = true;
    bool checkUndriven = true;
    bool checkProtocol = false;
    bool checkConvention = false;
    bool checkClockReset = false;
    std::string conventionFile;
    std::string expectFile;
    std::string diffFile;
    std::string traceSignal;
    bool ignoreTieOff = false;
    bool ignoreNc = false;
    bool showHelp = false;
};

// Parse our custom CLI args and return a cleaned argv for slang.
// Our custom args are removed so slang only sees what it understands.
static CliOptions parseCustomArgs(int argc, const char* const* argv,
                                  std::vector<const char*>& slangArgs) {
    CliOptions opts;
    slangArgs.push_back(argv[0]); // program name always passed through

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            opts.showHelp = true;
        } else if (arg == "--top") {
            if (i + 1 >= argc) { fmt::print(stderr, "Error: --top requires a value\n"); return opts; }
            opts.topModule = argv[++i];
            // Also pass --top to slang so it elaborates the correct top module
            slangArgs.push_back("--top");
            slangArgs.push_back(argv[i]);
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc) { fmt::print(stderr, "Error: {} requires a value\n", arg); return opts; }
            opts.outputDir = argv[++i];
        } else if (arg == "--format") {
            if (i + 1 >= argc) { fmt::print(stderr, "Error: --format requires a value\n"); return opts; }
            opts.format = argv[++i];
        } else if (arg == "--waiver") {
            if (i + 1 >= argc) { fmt::print(stderr, "Error: --waiver requires a value\n"); return opts; }
            opts.waiverFile = argv[++i];
        } else if (arg == "--depth") {
            if (i + 1 >= argc) { fmt::print(stderr, "Error: --depth requires a value\n"); return opts; }
            try { opts.depth = std::stoi(argv[++i]); }
            catch (...) { fmt::print(stderr, "Error: --depth requires an integer value\n"); return opts; }
        } else if (arg == "--no-check-width") {
            opts.checkWidth = false;
        } else if (arg == "--no-check-type") {
            opts.checkType = false;
        } else if (arg == "--no-check-dangling") {
            opts.checkDangling = false;
        } else if (arg == "--no-check-undriven") {
            opts.checkUndriven = false;
        } else if (arg == "--check-protocol") {
            opts.checkProtocol = true;
        } else if (arg == "--check-convention") {
            opts.checkConvention = true;
        } else if (arg == "--check-clock-reset") {
            opts.checkClockReset = true;
        } else if (arg == "--convention") {
            if (i + 1 >= argc) { fmt::print(stderr, "Error: --convention requires a value\n"); return opts; }
            opts.conventionFile = argv[++i];
        } else if (arg == "--expect") {
            if (i + 1 >= argc) { fmt::print(stderr, "Error: --expect requires a value\n"); return opts; }
            opts.expectFile = argv[++i];
        } else if (arg == "--diff") {
            if (i + 1 >= argc) { fmt::print(stderr, "Error: --diff requires a value\n"); return opts; }
            opts.diffFile = argv[++i];
        } else if (arg == "--trace") {
            if (i + 1 >= argc) { fmt::print(stderr, "Error: --trace requires a value\n"); return opts; }
            opts.traceSignal = argv[++i];
        } else if (arg == "--ignore-tie-off") {
            // Parsed but not yet implemented - reserved for future use
            opts.ignoreTieOff = true;
        } else if (arg == "--ignore-nc") {
            // Parsed but not yet implemented - reserved for future use
            opts.ignoreNc = true;
        } else {
            // Pass through to slang (SV files, -I, -D, --std, -f, etc.)
            slangArgs.push_back(argv[i]);
        }
    }
    return opts;
}

// ---------------------------------------------------------------------------
// Helper: compile sources via slang Driver and extract connectivity graph
// ---------------------------------------------------------------------------
static bool runCompilation(const std::vector<const char*>& slangArgs,
                           const CliOptions& opts,
                           connect::ConnectionGraph& graph) {
    slang::driver::Driver driver;
    driver.addStandardArgs();

    if (!driver.parseCommandLine(static_cast<int>(slangArgs.size()), slangArgs.data())) {
        fmt::print(stderr, "Error: failed to parse command line\n");
        return false;
    }
    if (!driver.processOptions()) {
        fmt::print(stderr, "Error: failed to process options\n");
        return false;
    }
    if (!driver.parseAllSources()) {
        fmt::print(stderr, "Error: failed to parse sources\n");
        return false;
    }

    auto compilation = driver.createCompilation();
    if (!compilation) {
        fmt::print(stderr, "Error: failed to create compilation\n");
        return false;
    }

    connect::ConnectionExtractor extractor(*compilation, opts.topModule, opts.depth);
    graph = extractor.extract();

    if (graph.allPorts.empty()) {
        fmt::print(stderr, "Warning: no ports found for top module '{}'\n", opts.topModule);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Helper: configure and run all enabled checkers
// ---------------------------------------------------------------------------
static std::vector<connect::Issue> runCheckers(const CliOptions& opts,
                                               const connect::ConnectionGraph& graph) {
    connect::CheckerRunner runner;
    if (opts.checkWidth)
        runner.addChecker(std::make_unique<connect::WidthChecker>());
    if (opts.checkType)
        runner.addChecker(std::make_unique<connect::TypeChecker>());
    if (opts.checkDangling)
        runner.addChecker(std::make_unique<connect::DanglingChecker>());
    if (opts.checkUndriven)
        runner.addChecker(std::make_unique<connect::UndrivenChecker>());
    if (opts.checkProtocol)
        runner.addChecker(std::make_unique<connect::ProtocolChecker>());
    if (opts.checkConvention) {
        if (!opts.conventionFile.empty()) {
            fmt::print(stderr, "Warning: --convention file loading not yet implemented, using defaults\n");
        }
        runner.addChecker(std::make_unique<connect::ConventionChecker>());
    }
    if (!opts.expectFile.empty())
        runner.addChecker(std::make_unique<connect::ExpectChecker>(opts.expectFile));

    return runner.runAll(graph);
}

// ---------------------------------------------------------------------------
// Helper: write a single report format to a file
// ---------------------------------------------------------------------------
template <typename Generator>
static void writeReportFile(const std::string& outputDir, const std::string& filename,
                            const connect::ReportData& data) {
    fs::create_directories(outputDir);
    std::string path = (fs::path(outputDir) / filename).string();
    std::ofstream ofs(path);
    if (ofs) {
        Generator gen;
        gen.generate(data, ofs);
    } else {
        fmt::print(stderr, "Error: cannot write to {}\n", path);
    }
}

// ---------------------------------------------------------------------------
// Helper: generate all requested report outputs
// ---------------------------------------------------------------------------
static void generateReports(const CliOptions& opts, const connect::ReportData& data) {
    if (opts.format == "table" || opts.format == "all") {
        connect::TableReportGenerator tableGen;
        tableGen.generate(data, std::cout);
    }
    if (opts.format == "json" || opts.format == "all")
        writeReportFile<connect::JsonReportGenerator>(opts.outputDir, "connect_report.json", data);
    if (opts.format == "md" || opts.format == "all")
        writeReportFile<connect::MarkdownReportGenerator>(opts.outputDir, "connect_report.md", data);
    if (opts.format == "csv" || opts.format == "all")
        writeReportFile<connect::CsvReportGenerator>(opts.outputDir, "connection_matrix.csv", data);
    if (opts.format == "dot" || opts.format == "all")
        writeReportFile<connect::DotReportGenerator>(opts.outputDir, "connectivity.dot", data);
    if (opts.format == "html" || opts.format == "all")
        writeReportFile<connect::HtmlReportGenerator>(opts.outputDir, "connect_report.html", data);
}

// ---------------------------------------------------------------------------
// Helper: print interface summary (table/all only)
// ---------------------------------------------------------------------------
static void printInterfaceSummary(const CliOptions& opts, const connect::ReportData& data) {
    if (opts.format != "table" && opts.format != "all")
        return;

    connect::InterfaceGrouper grouper;
    auto groups = grouper.classify(data.graph);
    if (!groups.empty()) {
        fmt::print("\n=== Interface Summary ===\n");
        for (const auto& g : groups) {
            auto dotPos = g.instancePath.rfind('.');
            std::string shortName = (dotPos != std::string::npos)
                ? g.instancePath.substr(dotPos + 1) : g.instancePath;
            fmt::print("  {:16s}: {} {} ({} signals, prefix: {})\n",
                       shortName, g.protocol, g.role,
                       g.matchedPorts.size(), g.prefix);
        }
        fmt::print("\n");
    }
}

// ---------------------------------------------------------------------------
// Helper: run diff against a baseline JSON report
// ---------------------------------------------------------------------------
static void runDiffMode(const CliOptions& opts, const connect::ReportData& data) {
    if (opts.diffFile.empty())
        return;

    try {
        auto baseline = connect::loadDiffInputFromJson(opts.diffFile);

        // Build issue status lookup: O(I) build, O(1) per connection
        std::unordered_map<std::string, std::string> issueMap;
        for (const auto& issue : data.active) {
            if (issue.connection.has_value()) {
                auto key = issue.connection->source.fullPath() + "|" + issue.connection->dest.fullPath();
                issueMap[key] = connect::Issue::typeToString(issue.type);
            }
        }

        connect::DiffInput current;
        for (const auto& conn : data.graph.connections) {
            auto key = conn.source.fullPath() + "|" + conn.dest.fullPath();
            auto it = issueMap.find(key);
            std::string status = (it != issueMap.end()) ? it->second : "OK";
            current.connections.push_back({conn.source.fullPath(), conn.dest.fullPath(), status});
        }

        auto diff = connect::computeDiff(baseline, current);

        if (diff.empty()) {
            fmt::print("\nDiff: no connectivity changes vs baseline.\n");
        } else {
            fmt::print("\n=== Connectivity Diff vs {} ===\n", opts.diffFile);
            for (const auto& c : diff.added) {
                fmt::print("  + ADDED:   {} -> {}  [{}]\n", c.source, c.dest, c.status);
            }
            for (const auto& c : diff.removed) {
                fmt::print("  - REMOVED: {} -> {}  [was: {}]\n", c.source, c.dest, c.status);
            }
            for (const auto& c : diff.changed) {
                fmt::print("  ~ CHANGED: {} -> {}  status: {} -> {}\n",
                           c.source, c.dest, c.oldStatus, c.newStatus);
            }
            fmt::print("  Total: +{} -{} ~{}\n",
                       diff.added.size(), diff.removed.size(), diff.changed.size());
        }
    } catch (const std::exception& e) {
        fmt::print(stderr, "Error loading diff baseline: {}\n", e.what());
    }
}

// ---------------------------------------------------------------------------
// Helper: print clock/reset topology analysis
// ---------------------------------------------------------------------------
static void printClockResetTopology(const connect::ReportData& data) {
    connect::ClockResetAnalyzer crAnalyzer;
    auto topo = crAnalyzer.analyze(data.graph);

    fmt::print("\n=== Clock Topology ===\n");
    for (const auto& [portName, instances] : topo.clockGroups) {
        fmt::print("  {} ->", portName);
        for (size_t i = 0; i < instances.size(); ++i) {
            const auto& inst = instances[i];
            auto dotPos = inst.rfind('.');
            std::string shortName = (dotPos != std::string::npos)
                ? inst.substr(dotPos + 1) : inst;
            if (i > 0) fmt::print(",");
            fmt::print(" {}", shortName);
        }
        fmt::print("\n");
    }

    fmt::print("\n=== Reset Topology ===\n");
    for (const auto& [portName, instances] : topo.resetGroups) {
        fmt::print("  {} ->", portName);
        for (size_t i = 0; i < instances.size(); ++i) {
            const auto& inst = instances[i];
            auto dotPos = inst.rfind('.');
            std::string shortName = (dotPos != std::string::npos)
                ? inst.substr(dotPos + 1) : inst;
            if (i > 0) fmt::print(",");
            fmt::print(" {}", shortName);
        }
        fmt::print("\n");
    }

    if (!topo.warnings.empty()) {
        fmt::print("\n=== Clock/Reset Warnings ===\n");
        for (const auto& inst : topo.warnings) {
            auto dotPos = inst.rfind('.');
            std::string shortName = (dotPos != std::string::npos)
                ? inst.substr(dotPos + 1) : inst;
            fmt::print("  \u26a0 {} ({}): has clock input but no reset port detected\n",
                       inst, shortName);
        }
    }
    fmt::print("\n");
}

// ---------------------------------------------------------------------------
// Helper: run signal fan-out / fan-in tracing
// ---------------------------------------------------------------------------
static void runSignalTrace(const CliOptions& opts, const connect::ReportData& data) {
    if (opts.traceSignal.empty())
        return;

    connect::TraceEngine traceEngine(data.graph);

    auto fanOutHops = traceEngine.traceFanOut(opts.traceSignal);
    fmt::print("\n{}", connect::TraceEngine::formatTrace(
        fanOutHops, opts.traceSignal, true));

    auto fanInHops = traceEngine.traceFanIn(opts.traceSignal);
    fmt::print("\n{}", connect::TraceEngine::formatTrace(
        fanInHops, opts.traceSignal, false));
}

// ===========================================================================
int main(int argc, char* argv[]) {
    // --- Phase 1: Parse CLI args ---
    std::vector<const char*> slangArgs;
    auto opts = parseCustomArgs(argc, argv, slangArgs);

    if (opts.showHelp) {
        printUsage();
        return 0;
    }

    if (opts.topModule.empty()) {
        fmt::print(stderr, "Error: --top <module> is required\n");
        printUsage();
        return 1;
    }

    if (opts.format != "json" && opts.format != "md" && opts.format != "csv" &&
        opts.format != "table" && opts.format != "dot" && opts.format != "html" &&
        opts.format != "all") {
        fmt::print(stderr, "Error: invalid format '{}'. Use json|md|csv|table|dot|html|all\n",
                   opts.format);
        return 1;
    }

    // --- Phase 2+3: Compile and extract connections ---
    connect::ConnectionGraph graph;
    if (!runCompilation(slangArgs, opts, graph))
        return 1;

    // --- Phase 4: Run checkers ---
    auto issues = runCheckers(opts, graph);

    // --- Phase 5: Apply waivers ---
    std::vector<connect::Issue> active;
    std::vector<connect::Issue> waived;
    if (!opts.waiverFile.empty()) {
        connect::WaiverFilter filter(opts.waiverFile);
        auto result = filter.apply(issues);
        active = std::move(result.active);
        waived = std::move(result.waived);
    } else {
        active = std::move(issues);
    }

    // --- Phase 6: Build report data and generate outputs ---
    connect::ReportData reportData{opts.topModule, std::move(graph), active, waived};
    connect::AnalysisEngine analysisEngine;
    reportData.analysis = analysisEngine.analyze(reportData);

    generateReports(opts, reportData);
    printInterfaceSummary(opts, reportData);
    runDiffMode(opts, reportData);

    if (opts.checkClockReset)
        printClockResetTopology(reportData);

    runSignalTrace(opts, reportData);

    // --- Phase 7: Exit code ---
    return std::min(static_cast<int>(active.size()), 255);
}
