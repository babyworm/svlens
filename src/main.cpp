#include "CheckerRunner.h"
#include "ConnectionExtractor.h"
#include "ConventionChecker.h"
#include "CsvReport.h"
#include "DanglingChecker.h"
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
        "  --format <fmt>          json|md|csv|table|all (default: all)\n"
        "  --help                  Show this help message\n\n"
        "Analysis (all enabled by default, --no-* to disable):\n"
        "  --no-check-width        Disable width mismatch checking\n"
        "  --no-check-type         Disable type mismatch checking\n"
        "  --no-check-dangling     Disable dangling output checking\n"
        "  --no-check-undriven     Disable undriven input checking\n"
        "  --check-protocol        Enable protocol completeness checking\n"
        "  --check-convention      Enable naming convention checking\n"
        "  --convention <file>     Custom convention rules (YAML, optional)\n"
        "  --depth <n>             Hierarchy depth (default: unlimited, -1)\n\n"
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
    std::string conventionFile;
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
        } else if (arg == "--convention") {
            if (i + 1 >= argc) { fmt::print(stderr, "Error: --convention requires a value\n"); return opts; }
            opts.conventionFile = argv[++i];
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

int main(int argc, char* argv[]) {
    // --- Phase 1: Parse our custom args ---
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

    // Validate format
    if (opts.format != "json" && opts.format != "md" && opts.format != "csv" &&
        opts.format != "table" && opts.format != "all") {
        fmt::print(stderr, "Error: invalid format '{}'. Use json|md|csv|table|all\n",
                   opts.format);
        return 1;
    }

    // --- Phase 2: slang compilation via Driver ---
    slang::driver::Driver driver;
    driver.addStandardArgs();

    if (!driver.parseCommandLine(static_cast<int>(slangArgs.size()), slangArgs.data())) {
        fmt::print(stderr, "Error: failed to parse command line\n");
        return 1;
    }
    if (!driver.processOptions()) {
        fmt::print(stderr, "Error: failed to process options\n");
        return 1;
    }
    if (!driver.parseAllSources()) {
        fmt::print(stderr, "Error: failed to parse sources\n");
        return 1;
    }

    auto compilation = driver.createCompilation();
    if (!compilation) {
        fmt::print(stderr, "Error: failed to create compilation\n");
        return 1;
    }

    // --- Phase 3: Extract connections ---
    connect::ConnectionExtractor extractor(*compilation, opts.topModule, opts.depth);
    auto graph = extractor.extract();

    if (graph.allPorts.empty()) {
        fmt::print(stderr, "Warning: no ports found for top module '{}'\n", opts.topModule);
    }

    // --- Phase 4: Run checkers ---
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

    auto issues = runner.runAll(graph);

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

    // --- Phase 6: Generate reports ---
    connect::ReportData reportData{
        opts.topModule,
        std::move(graph),
        active,
        waived
    };

    bool needFileOutput = (opts.format != "table");

    if (needFileOutput) {
        fs::create_directories(opts.outputDir);
    }

    // Table to stdout
    if (opts.format == "table" || opts.format == "all") {
        connect::TableReportGenerator tableGen;
        tableGen.generate(reportData, std::cout);
    }

    // JSON to file
    if (opts.format == "json" || opts.format == "all") {
        fs::create_directories(opts.outputDir);
        std::string path = (fs::path(opts.outputDir) / "connect_report.json").string();
        std::ofstream ofs(path);
        if (ofs) {
            connect::JsonReportGenerator jsonGen;
            jsonGen.generate(reportData, ofs);
        } else {
            fmt::print(stderr, "Error: cannot write to {}\n", path);
        }
    }

    // Markdown to file
    if (opts.format == "md" || opts.format == "all") {
        fs::create_directories(opts.outputDir);
        std::string path = (fs::path(opts.outputDir) / "connect_report.md").string();
        std::ofstream ofs(path);
        if (ofs) {
            connect::MarkdownReportGenerator mdGen;
            mdGen.generate(reportData, ofs);
        } else {
            fmt::print(stderr, "Error: cannot write to {}\n", path);
        }
    }

    // CSV to file
    if (opts.format == "csv" || opts.format == "all") {
        fs::create_directories(opts.outputDir);
        std::string path = (fs::path(opts.outputDir) / "connection_matrix.csv").string();
        std::ofstream ofs(path);
        if (ofs) {
            connect::CsvReportGenerator csvGen;
            csvGen.generate(reportData, ofs);
        } else {
            fmt::print(stderr, "Error: cannot write to {}\n", path);
        }
    }

    // --- Phase 7: Exit code ---
    int issueCount = static_cast<int>(active.size());
    return std::min(issueCount, 255);
}
