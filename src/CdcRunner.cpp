#include "CdcRunner.h"

#include "CommonCli.h"
#include "CdcRunnerUtils.h"

#include <iostream>

#ifndef SVLENS_VERSION
#define SVLENS_VERSION "0.2.2"
#endif

void cdccli::printCdcUsage() {
    std::cout << "svlens cdc v" << SVLENS_VERSION << " — Structural CDC Analysis Tool\n\n"
              << "Usage: svlens cdc [OPTIONS] <SV_FILES...>\n\n"
              << "Required:\n"
              << "  <SV_FILES...>           SystemVerilog source files\n"
              << "  --top <module>          Top-level module name\n\n"
              << "Output:\n"
              << "  -o, --output <dir>      Output directory (default: ./cdc_reports/)\n"
              << "  --format <fmt>          md|json|sdc|waiver|all (default: all)\n"
              << "  --dump-graph <file>     Export DOT graph to file\n\n"
              << "Filelist:\n"
              << "  -f <filelist>           Read source files and options from filelist\n"
              << "  -F <filelist>           Same as -f, but paths relative to filelist location\n\n"
              << "Options:\n"
              << "  --sdc <file>            SDC file with clock definitions\n"
              << "  --clock-yaml <file>     Clock specification YAML file\n"
              << "  --waiver <file>         Waiver YAML file\n"
              << "  --sync-stages <n>       Required synchronizer stages (default: 2)\n"
              << "  --strict                Treat CAUTION as VIOLATION in exit code\n"
              << "  --ignore-gated          Skip gated-clock crossings (Severity::Low) from report\n"
              << "  --auto-clocks           Auto-detect clocks (default, for spec compliance)\n"
              << "  -v, --verbose           Detailed output\n"
              << "  -q, --quiet             Only violations and summary\n"
              << "  --version               Show version\n"
              << "  -h, --help              Show this help\n";
}

cdccli::CdcCliOptions cdccli::parseCdcArgs(int argc, const char* const* argv,
                                           std::vector<std::string>& compilationArgs) {
    CdcCliOptions opts;
    compilationArgs.push_back(argv[0]);

    for (int i = 1; i < argc; i++) {
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
            continue;
        }
        if (arg == "--top" && i + 1 < argc) {
            opts.topModule = argv[++i];
            compilationArgs.push_back("--top");
            compilationArgs.push_back(opts.topModule);
        }
        else if (arg == "--format" && i + 1 < argc)
            opts.format = argv[++i];
        else if (arg == "--sdc" && i + 1 < argc)
            opts.sdcFile = argv[++i];
        else if (arg == "--clock-yaml" && i + 1 < argc)
            opts.clockYamlFile = argv[++i];
        else if (arg == "--waiver" && i + 1 < argc)
            opts.waiverFile = argv[++i];
        else if (arg == "--dump-graph" && i + 1 < argc)
            opts.dumpGraphFile = argv[++i];
        else if (arg == "--sync-stages" && i + 1 < argc) {
            try {
                opts.syncStages = std::stoi(argv[++i]);
            } catch (...) {
                opts.parseError = "--sync-stages requires an integer value";
            }
        }
        else if (arg == "--strict")
            opts.strict = true;
        else if (arg == "--ignore-gated")
            opts.ignoreGated = true;
        else if (arg == "--auto-clocks")
            opts.autoClocks = true;
        else if (arg == "-q" || arg == "--quiet")
            opts.quiet = true;
        else if (arg == "-v" || arg == "--verbose")
            opts.verbose = true;
        else
            compilationArgs.push_back(argv[i]);
    }

    (void)opts.autoClocks; // no-op flag: auto-detection is always on
    return opts;
}

bool cdccli::validateCdcOptions(const CdcCliOptions& opts) {
    if (!opts.parseError.empty()) {
        std::cerr << "sv-cdccheck: error: " << opts.parseError << "\n";
        return false;
    }
    if (opts.topModule.empty()) {
        std::cerr << "sv-cdccheck: error: --top <module> is required\n";
        printCdcUsage();
        return false;
    }
    if (opts.format != "md" && opts.format != "json" && opts.format != "sdc" &&
        opts.format != "waiver" && opts.format != "all") {
        std::cerr << "sv-cdccheck: error: invalid format '" << opts.format
                  << "'. Use md|json|sdc|waiver|all\n";
        return false;
    }
    return true;
}

int cdccli::runCdcWithCompilation(slang::ast::Compilation& compilation,
                                  const CdcCliOptions& opts) {
    auto result = analyzeCdcCompilation(compilation, opts);
    emitCdcReports(opts, result);
    printCdcSummary(opts, result);
    int exit_count = result.violation_count();
    if (opts.strict)
        exit_count += result.caution_count();
    return std::min(exit_count, 255);
}

int runCdcMain(int argc, char** argv) {
    if (argc < 2) {
        cdccli::printCdcUsage();
        return 1;
    }

    std::vector<std::string> compilationArgs;
    auto opts = cdccli::parseCdcArgs(argc, argv, compilationArgs);

    if (opts.showVersion) {
        std::cout << "svlens cdc " << SVLENS_VERSION << "\n";
        return 0;
    }
    if (opts.showHelp) {
        cdccli::printCdcUsage();
        return 0;
    }
    if (!cdccli::validateCdcOptions(opts))
        return 1;

    connect::CompilationSession session;
    std::string error;
    if (!session.compile(compilationArgs, &error)) {
        std::cerr << "sv-cdccheck: error: " << error << "\n";
        return 1;
    }

    return cdccli::runCdcWithCompilation(session.compilation(), opts);
}
