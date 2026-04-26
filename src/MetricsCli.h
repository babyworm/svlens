#pragma once

#include "CommonCli.h"

#include <fmt/core.h>
#include <string>
#include <vector>

namespace metrics {

struct MetricsCliOptions {
    std::string topModule;
    std::string outputDir = "./metrics_reports";
    std::string format = "json";
    std::string roots = "all"; // outputs | ffd | all
    int maxDepth = 256;
    bool normalizeBitLanes = true;
    int laneMinWidth = 2;
    int maxForUnroll = 1024;
    int topK = 0; // 0 = all roots
    std::string baselineFile;
    bool failOnRegression = false;
    bool emitCones = false;
    bool emitRawGraph = false;
    bool showHelp = false;
    bool showVersion = false;
};

inline void printMetricsUsage() {
    const std::string usage =
        std::string("svlens metrics v") + SVLENS_VERSION +
        " -- RTL transformation complexity analysis\n\n"
        "Usage:\n"
        "  svlens metrics [OPTIONS] <SV_FILES...>\n\n"
        "Required:\n"
        "  --top <module>                 Top-level module name\n"
        "  <SV_FILES...> / -f / -F        SystemVerilog sources or filelists\n\n"
        "Common:\n"
        "  -o, --output <dir>             Output directory (default: ./metrics_reports)\n"
        "  --format json|md|both          Report format (default: json)\n"
        "  --help                         Show this help\n"
        "  --version                      Show version\n"
        "  " + commoncli::passThroughNote() + "\n\n"
        "Metrics-specific:\n"
        "  --roots outputs|ffd|all        Root selection (default: all)\n"
        "  --max-depth <N>                Backward traversal depth limit (default: 256)\n"
        "  --normalize-bit-lanes          Enable repeated bit-lane normalization (default)\n"
        "  --no-normalize-bit-lanes       Disable repeated bit-lane normalization\n"
        "  --lane-min-width <N>           Minimum width for lane grouping (default: 2)\n"
        "  --max-for-unroll <N>             Max for-loop unroll iterations (default: 1024)\n"
        "  --emit-cones                   Include per-root cone detail in output\n"
        "  --emit-raw-graph               Include raw transform graph in output\n"
        "  --topk <N>                     Show only top-N most complex roots (default: all)\n"
        "  --baseline <file>              Compare against previous metrics_report.json\n"
        "  --fail-on-regression           Exit 2 when metrics regress vs baseline\n\n"
        "Outputs:\n"
        "  <output>/metrics_report.json   Transformation complexity report\n\n"
        "Examples:\n"
        "  svlens metrics design.sv --top my_top\n"
        "  svlens metrics --top soc_top rtl/top.sv --format json --roots outputs\n"
        "  svlens metrics -F rtl/filelist.f --top soc_top -o reports --emit-cones\n\n"
        "Exit Codes:\n"
        "  0  Analysis completed successfully.\n"
        "  1  Error during compilation or analysis.\n\n"
        "Limitations:\n"
        "  MVP supports continuous assign, direct assignment, and limited procedural fragments.\n"
        "  Full always_comb semantics are deferred to v2.\n"
        "  FF D-side cones use CDC hint data; provenance_level indicates confidence.\n\n"
        "Notes:\n"
        "  Run 'svlens help conn' or 'svlens help cdc' for other analysis modes.\n"
        "  Schema: docs/schema/metrics_report.md\n\n";
    fmt::print("{}", usage);
    fmt::print("Docs:\n  {}\n\n", commoncli::docsHint());
}

namespace detail {
inline int safeStoi(const char* flag, const char* val, int minVal = 0) {
    int n;
    try { n = std::stoi(val); }
    catch (...) {
        fmt::print(stderr, "Error: {} requires a numeric value, got '{}'\n", flag, val);
        std::exit(1);
    }
    if (n < minVal) {
        fmt::print(stderr, "Error: {} must be >= {}, got {}\n", flag, minVal, n);
        std::exit(1);
    }
    return n;
}
} // namespace detail

inline MetricsCliOptions parseMetricsArgs(int argc, const char* const* argv,
                                          std::vector<std::string>& compilationArgs) {
    MetricsCliOptions opts;
    compilationArgs.clear();
    compilationArgs.push_back("slang");

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];

        commoncli::BasicCliState basic;
        basic.outputDir = opts.outputDir;
        if (commoncli::consumeBasicCliArg(arg, i, argc, argv, basic)) {
            opts.outputDir = basic.outputDir;
            opts.showHelp = opts.showHelp || basic.showHelp;
            opts.showVersion = opts.showVersion || basic.showVersion;
            continue;
        }

        if (arg == "--top" && i + 1 < argc) {
            opts.topModule = argv[++i];
            compilationArgs.push_back("--top");
            compilationArgs.push_back(opts.topModule);
            continue;
        }
        if (arg == "--format" && i + 1 < argc) {
            opts.format = argv[++i];
            continue;
        }
        if (arg == "--roots" && i + 1 < argc) {
            opts.roots = argv[++i];
            continue;
        }
        if (arg == "--max-depth" && i + 1 < argc) {
            opts.maxDepth = detail::safeStoi("--max-depth", argv[++i], 1);
            continue;
        }
        if (arg == "--normalize-bit-lanes") {
            opts.normalizeBitLanes = true;
            continue;
        }
        if (arg == "--no-normalize-bit-lanes") {
            opts.normalizeBitLanes = false;
            continue;
        }
        if (arg == "--lane-min-width" && i + 1 < argc) {
            opts.laneMinWidth = detail::safeStoi("--lane-min-width", argv[++i], 1);
            continue;
        }
        if (arg == "--baseline" && i + 1 < argc) {
            opts.baselineFile = argv[++i];
            continue;
        }
        if (arg == "--fail-on-regression") {
            opts.failOnRegression = true;
            continue;
        }
        if (arg == "--topk" && i + 1 < argc) {
            opts.topK = detail::safeStoi("--topk", argv[++i]);
            continue;
        }
        if (arg == "--max-for-unroll" && i + 1 < argc) {
            opts.maxForUnroll = detail::safeStoi("--max-for-unroll", argv[++i]);
            continue;
        }
        if (arg == "--emit-cones") {
            opts.emitCones = true;
            continue;
        }
        if (arg == "--emit-raw-graph") {
            opts.emitRawGraph = true;
            continue;
        }

        compilationArgs.push_back(std::string(arg));
    }

    return opts;
}

[[nodiscard]] inline bool validateMetricsOptions(const MetricsCliOptions& opts) {
    if (opts.topModule.empty()) {
        fmt::print(stderr, "Error: --top is required for metrics analysis\n");
        return false;
    }
    if (opts.roots != "outputs" && opts.roots != "ffd" && opts.roots != "all") {
        fmt::print(stderr, "Error: --roots must be one of: outputs, ffd, all\n");
        return false;
    }
    if (opts.format != "json" && opts.format != "md" && opts.format != "both") {
        fmt::print(stderr, "Error: --format must be one of: json, md, both\n");
        return false;
    }
    return true;
}

} // namespace metrics

int runMetricsMain(int argc, char* argv[]);
