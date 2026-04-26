#include "AnalysisEngine.h"
#include "ConnRunner.h"
#include "CompilationSession.h"
#include "ConnectionFilters.h"
#include "CommonCli.h"
#include "ConnRunnerUtils.h"
#include "ConventionChecker.h"
#include "SourceTextScanner.h"
#include "WaiverFilter.h"

#include <fmt/core.h>
#include <string>
#include <vector>

void connect::printConnUsage() {
    const std::string usage =
        std::string("svlens conn v") + SVLENS_VERSION + " -- Module interconnect verification and analysis\n\n"
        "Usage: svlens conn [OPTIONS] <SV_FILES...>\n\n"
        "Required:\n"
        "  --top <module>                 Top-level module\n"
        "  <SV_FILES...>                  SystemVerilog source files\n\n"
        "Common:\n"
        "  -o, --output <dir>             Output directory (default: ./connect_reports/)\n"
        "  --format <fmt>                 json|md|csv|table|dot|html|all (default: all)\n"
        "  --version                      Show version\n"
        "  --help                         Show this help message\n"
        "  " + commoncli::passThroughNote() + "\n\n"
        "Analysis (all enabled by default, use --no-* to disable):\n"
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
        "Filtering:\n"
        "  --ignore-tie-off        Exclude ports tied to compile-time constants\n"
        "  --ignore-nc             Exclude ports named as no-connect / unused\n"
        "  --waiver <file>         YAML waiver file\n\n"
        "Tracing:\n"
        "  --trace <pattern>       Trace signal fan-out and fan-in (glob pattern)\n\n"
        "Comparison:\n"
        "  --diff <file>           Compare against a baseline JSON report\n\n"
        "Outputs:\n"
        "  json  -> connect_report.json\n"
        "  md    -> connect_report.md\n"
        "  csv   -> connection_matrix.csv\n"
        "  dot   -> connectivity.dot\n"
        "  html  -> connect_report.html\n"
        "  table -> stdout summary\n\n"
        "Examples:\n"
        "  svlens conn design.sv --top my_top\n"
        "  svlens conn -F rtl/filelist.f --top soc_top --format all -o reports\n"
        "  svlens conn design.sv --top my_top --check-protocol --check-convention\n"
        "  svlens conn design.sv --top my_top --trace \"*.u_cpu.o_addr\"\n\n"
        "Exit Codes:\n"
        "  Returns the number of active issues, capped at 255.\n"
        "  0 means no active issues after waiver filtering.\n\n"
        "Limitations:\n"
        "  Whole-interface / modport ports and procedural glue logic are only partially modeled.\n"
        "  Clock/reset analysis in conn mode is still heuristic and name-oriented, not full semantic domain analysis.\n\n"
        "Notes:\n"
        "  Use docs/schema/connect_report.md for the stable JSON contract.\n"
        "  Use 'svlens help both' for combined-run output behavior.\n"
        "  " + commoncli::productBoundaryNote() + "\n";
    fmt::print("{}", usage);
}

// Parse our custom CLI args and return a cleaned argv for slang.
// Our custom args are removed so slang only sees what it understands.
connect::ConnCliOptions connect::parseConnArgs(int argc, const char* const* argv,
                                               std::vector<std::string>& compilationArgs) {
    ConnCliOptions opts;
    compilationArgs.push_back(argv[0]); // program name always passed through

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        commoncli::BasicCliState basic{
            .outputDir = opts.outputDir,
            .showHelp = opts.showHelp,
            .showVersion = opts.showVersion
        };

        if (commoncli::consumeBasicCliArg(arg, i, argc, argv, basic)) {
            opts.outputDir = basic.outputDir;
            opts.showHelp = basic.showHelp;
            opts.showVersion = basic.showVersion;
        } else if (arg == "--top") {
            if (i + 1 >= argc) { fmt::print(stderr, "Error: --top requires a value\n"); return opts; }
            opts.topModule = argv[++i];
            // Also pass --top to slang so it elaborates the correct top module
            compilationArgs.push_back("--top");
            compilationArgs.push_back(argv[i]);
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
            opts.checkConvention = true;
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
            opts.ignoreTieOff = true;
        } else if (arg == "--ignore-nc") {
            opts.ignoreNc = true;
        } else {
            // Pass through to slang (SV files, -I, -D, --std, -f, etc.)
            compilationArgs.push_back(argv[i]);
        }
    }
    return opts;
}

// ===========================================================================
bool connect::validateConnOptions(const ConnCliOptions& opts) {
    if (opts.topModule.empty()) {
        fmt::print(stderr, "Error: --top <module> is required\n");
        printConnUsage();
        return false;
    }

    if (opts.format != "json" && opts.format != "md" && opts.format != "csv" &&
        opts.format != "table" && opts.format != "dot" && opts.format != "html" &&
        opts.format != "all") {
        fmt::print(stderr, "Error: invalid format '{}'. Use json|md|csv|table|dot|html|all\n",
                   opts.format);
        return false;
    }

    return true;
}

int connect::runConnWithCompilation(slang::ast::Compilation& compilation,
                                    const ConnCliOptions& opts) {
    connect::ConnectionGraph graph;
    if (!buildConnectionGraph(compilation, opts, graph))
        return 1;

    connect::GraphFilterOptions filterOptions;
    filterOptions.ignoreTieOff = opts.ignoreTieOff;
    filterOptions.ignoreNc = opts.ignoreNc;
    graph = connect::applyGraphFilters(graph, filterOptions);

    // US-39E / US-39F: source-text and file-naming checks run when
    // convention checking is enabled. Rules are loaded from the YAML
    // (or defaults used when no file is given) so the per-rule enable
    // flags are respected.
    //
    // Codex cross-review: wrap loadConventionRules + scan in try/catch
    // so a malformed convention YAML (or a scan-time exception) only
    // disables source-text rules instead of crashing conn mode before
    // report generation.
    if (opts.checkConvention) {
        try {
            auto textRules = opts.conventionFile.empty()
                ? connect::ConventionRules{}
                : connect::loadConventionRules(opts.conventionFile);
            connect::SourceTextScanner::scan(compilation, textRules, graph);
        } catch (const std::exception& e) {
            fmt::print(stderr,
                       "Warning: source-text convention scan skipped: {}\n",
                       e.what());
        }
    }

    std::vector<connect::Issue> issues;
    try {
        issues = runConnCheckers(opts, graph);
    } catch (const std::exception& e) {
        fmt::print(stderr, "Error: {}\n", e.what());
        return 1;
    }

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

    connect::ReportData reportData{opts.topModule, std::move(graph), active, waived};
    connect::AnalysisEngine analysisEngine;
    reportData.analysis = analysisEngine.analyze(reportData);

    generateConnReports(opts, reportData);
    printConnInterfaceSummary(opts, reportData);
    runConnDiffMode(opts, reportData);

    if (opts.checkClockReset)
        printConnClockResetTopology(reportData);

    runConnSignalTrace(opts, reportData);

    return std::min(static_cast<int>(active.size()), 255);
}

int runConnMain(int argc, char* argv[]) {
    std::vector<std::string> compilationArgs;
    auto opts = connect::parseConnArgs(argc, argv, compilationArgs);

    if (opts.showHelp) {
        connect::printConnUsage();
        return 0;
    }

    if (opts.showVersion) {
        fmt::print("svlens conn {}\n", SVLENS_VERSION);
        return 0;
    }

    if (!connect::validateConnOptions(opts))
        return 1;

    connect::CompilationSession session;
    std::string error;
    if (!session.compile(compilationArgs, &error)) {
        fmt::print(stderr, "Error: {}\n", error);
        return 1;
    }

    return connect::runConnWithCompilation(session.compilation(), opts);
}
