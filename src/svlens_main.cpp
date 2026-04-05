#include "CompilationSession.h"
#include "CdcRunner.h"
#include "CommonCli.h"
#include "ConnRunner.h"

#include <algorithm>
#include <filesystem>
#include <fmt/core.h>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

namespace fs = std::filesystem;

void printDocsPointer() {
    fmt::print("Docs:\n  {}\n\n", commoncli::docsHint());
}

void printUsage() {
    const std::string usage =
        std::string("svlens v") + SVLENS_VERSION + " -- Unified SystemVerilog structural analysis\n\n"
        "Usage:\n"
        "  svlens conn [OPTIONS] <SV_FILES...>\n"
        "  svlens cdc  [OPTIONS] <SV_FILES...>\n"
        "  svlens both [COMMON_OPTIONS] [--conn-* ...] [--cdc-* ...] <SV_FILES...>\n"
        "  svlens help [conn|cdc|both]\n\n"
        "Modes:\n"
        "  conn   Port / connectivity analysis\n"
        "  cdc    Clock-domain crossing analysis\n"
        "  both   Run conn then cdc sequentially\n\n"
        "Quick start:\n"
        "  svlens conn design.sv --top my_top\n"
        "  svlens cdc --top soc_top rtl/top.sv --format json\n"
        "  svlens both rtl/top.sv --top soc_top -o reports --conn-format json --cdc-format json\n\n"
        "Install hint:\n"
        "  " + commoncli::installHint() + "\n\n"
        "Product boundary:\n"
        "  " + commoncli::productBoundaryNote() + "\n"
        "  Treat it as CI / review support, not a sign-off-equivalent replacement.\n\n";
    fmt::print("{}", usage);
    printDocsPointer();
}

void printBothUsage() {
    const std::string usage =
        std::string("svlens both v") + SVLENS_VERSION + " -- Combined connectivity and CDC analysis\n\n"
        "Usage:\n"
        "  svlens both [COMMON_OPTIONS] [--conn-* ...] [--cdc-* ...] <SV_FILES...>\n\n"
        "Required:\n"
        "  --top <module>                 Top-level module name\n"
        "  <SV_FILES...> / -f / -F        SystemVerilog sources or filelists\n\n"
        "Common:\n"
        "  -o, --output <dir>             Output root (default: ./svlens_reports)\n"
        "  --help                         Show this help\n"
        "  --version                      Show version\n"
        "  " + commoncli::passThroughNote() + "\n\n"
        "Outputs:\n"
        "  <output>/conn/connect_report.json\n"
        "  <output>/cdc/cdc_report.json\n"
        "  <output>/svlens_summary.json\n\n"
        "Examples:\n"
        "  svlens both rtl/top.sv --top soc_top -o reports --conn-format json --cdc-format json\n"
        "  svlens both -F rtl/filelist.f --top soc_top -o reports --conn-check-protocol --cdc-sync-stages 3\n\n"
        "Exit Codes:\n"
        "  Returns max(conn_exit_code, cdc_exit_code).\n"
        "  conn exit code is based on active issue count (capped at 255).\n"
        "  cdc exit code is based on violation count, or violation+caution count in --cdc-strict mode (capped at 255).\n\n"
        "Limitations:\n"
        "  both shares one elaboration frontend, but conn/cdc still keep separate analysis semantics and report schemas.\n"
        "  Use prefixed analysis flags such as --conn-format / --cdc-format to avoid ambiguity.\n\n"
        "Notes:\n"
        "  Run 'svlens help conn' or 'svlens help cdc' for mode-specific analysis details.\n\n";
    fmt::print("{}", usage);
    printDocsPointer();
}

int invoke(int (*fn)(int, char**), std::vector<std::string> args) {
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (auto& arg : args)
        argv.push_back(arg.data());
    return fn(static_cast<int>(argv.size()), argv.data());
}

const std::unordered_set<std::string> kConnValueOptions = {
    "--output", "-o", "--format", "--waiver", "--expect", "--diff",
    "--trace", "--convention", "--depth", "--top"
};

const std::unordered_set<std::string> kCdcValueOptions = {
    "--output", "-o", "--format", "--sdc", "--clock-yaml", "--waiver",
    "--dump-graph", "--sync-stages", "--top", "-f", "-F"
};

std::vector<std::string> extractSourceFiles(const std::vector<std::string>& expandedArgs) {
    static const std::unordered_set<std::string> optionsWithValues = {
        "--top", "-I", "-D", "--std", "-y", "-v"
    };

    std::vector<std::string> files;
    for (size_t i = 1; i < expandedArgs.size(); ++i) {
        const auto& arg = expandedArgs[i];
        if (!arg.empty() && arg[0] == '-') {
            if (optionsWithValues.contains(arg) && i + 1 < expandedArgs.size())
                ++i;
            continue;
        }
        files.push_back(arg);
    }
    return files;
}

std::vector<std::string> extractFilelists(const std::vector<std::string>& originalArgs) {
    std::vector<std::string> filelists;
    for (size_t i = 1; i < originalArgs.size(); ++i) {
        const auto& arg = originalArgs[i];
        if ((arg == "-f" || arg == "-F") && i + 1 < originalArgs.size())
            filelists.push_back(originalArgs[++i]);
    }
    return filelists;
}

std::string statusForExitCode(int exitCode) {
    return exitCode == 0 ? "ok" : "issues";
}

void writeBothSummary(const std::string& outputBase,
                      const std::string& topModule,
                      const std::string& connFormat,
                      const std::string& cdcFormat,
                      bool explicitOutput,
                      const std::vector<std::string>& filelists,
                      const std::vector<std::string>& sourceFiles,
                      int connExit,
                      int cdcExit) {
    fs::create_directories(outputBase);
    std::ofstream ofs(fs::path(outputBase) / "svlens_summary.json");
    if (!ofs)
        return;

    const auto connDir = fs::path(outputBase) / "conn";
    const auto cdcDir = fs::path(outputBase) / "cdc";

    ofs << "{\n";
    ofs << "  \"mode\": \"both\",\n";
    ofs << fmt::format("  \"top\": \"{}\",\n", topModule);
    ofs << fmt::format("  \"conn_format\": \"{}\",\n", connFormat);
    ofs << fmt::format("  \"cdc_format\": \"{}\",\n", cdcFormat);
    ofs << fmt::format("  \"explicit_output\": {},\n", explicitOutput ? "true" : "false");
    ofs << fmt::format("  \"used_filelist\": {},\n", filelists.empty() ? "false" : "true");
    ofs << fmt::format("  \"conn_exit_code\": {},\n", connExit);
    ofs << fmt::format("  \"cdc_exit_code\": {},\n", cdcExit);
    ofs << fmt::format("  \"exit_code\": {},\n", std::max(connExit, cdcExit));
    ofs << fmt::format("  \"source_file_count\": {},\n", sourceFiles.size());
    ofs << fmt::format("  \"conn_status\": \"{}\",\n", statusForExitCode(connExit));
    ofs << fmt::format("  \"cdc_status\": \"{}\",\n", statusForExitCode(cdcExit));
    ofs << "  \"filelists\": [\n";
    for (size_t i = 0; i < filelists.size(); ++i) {
        ofs << fmt::format("    \"{}\"", filelists[i]);
        if (i + 1 < filelists.size())
            ofs << ",";
        ofs << "\n";
    }
    ofs << "  ],\n";
    ofs << "  \"source_files\": [\n";
    for (size_t i = 0; i < sourceFiles.size(); ++i) {
        ofs << fmt::format("    \"{}\"", sourceFiles[i]);
        if (i + 1 < sourceFiles.size())
            ofs << ",";
        ofs << "\n";
    }
    ofs << "  ],\n";
    ofs << "  \"outputs\": {\n";
    ofs << fmt::format("    \"conn\": \"{}\",\n", connDir.string());
    ofs << fmt::format("    \"cdc\": \"{}\",\n", cdcDir.string());
    ofs << fmt::format("    \"conn_dir\": \"{}\",\n", connDir.string());
    ofs << fmt::format("    \"cdc_dir\": \"{}\"\n", cdcDir.string());
    ofs << "  },\n";
    ofs << "  \"reports\": {\n";
    ofs << fmt::format("    \"connect_report\": \"{}\",\n", (connDir / "connect_report.json").string());
    ofs << fmt::format("    \"cdc_report\": \"{}\"\n", (cdcDir / "cdc_report.json").string());
    ofs << "  }\n";
    ofs << "}\n";
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    const std::string mode = argv[1];
    if (mode == "-h" || mode == "--help") {
        printUsage();
        return 0;
    }
    if (mode == "--version") {
        fmt::print("svlens " SVLENS_VERSION "\n");
        return 0;
    }
    if (mode == "help") {
        if (argc < 3) {
            printUsage();
            return 0;
        }
        const std::string submode = argv[2];
        if (submode == "conn") {
            connect::printConnUsage();
            return 0;
        }
        if (submode == "cdc") {
            cdccli::printCdcUsage();
            return 0;
        }
        if (submode == "both") {
            printBothUsage();
            return 0;
        }
        fmt::print(stderr, "Unknown help topic '{}'\n\n", submode);
        printUsage();
        return 1;
    }
    if (mode == "conn") {
        std::vector<std::string> args = {"sv-conncheck"};
        for (int i = 2; i < argc; ++i)
            args.emplace_back(argv[i]);
        return invoke(runConnMain, std::move(args));
    }
    if (mode == "cdc") {
        std::vector<std::string> args = {"sv-cdccheck"};
        for (int i = 2; i < argc; ++i)
            args.emplace_back(argv[i]);
        return invoke(runCdcMain, std::move(args));
    }
    if (mode == "both") {
        auto dispatch = commoncli::routeBothModeArgs(argc, argv, 2,
                                                     kConnValueOptions,
                                                     kCdcValueOptions);

        std::vector<const char*> connArgv;
        std::vector<const char*> cdcArgv;
        connArgv.reserve(dispatch.connArgs.size());
        cdcArgv.reserve(dispatch.cdcArgs.size());
        for (auto& arg : dispatch.connArgs)
            connArgv.push_back(arg.c_str());
        for (auto& arg : dispatch.cdcArgs)
            cdcArgv.push_back(arg.c_str());

        std::vector<std::string> connCompilationArgs;
        auto connOpts = connect::parseConnArgs(static_cast<int>(connArgv.size()),
                                               connArgv.data(),
                                               connCompilationArgs);
        std::vector<std::string> cdcCompilationArgs;
        auto cdcOpts = cdccli::parseCdcArgs(static_cast<int>(cdcArgv.size()),
                                            cdcArgv.data(),
                                            cdcCompilationArgs);

        if (connOpts.showHelp || cdcOpts.showHelp) {
            printBothUsage();
            return 0;
        }
        if (connOpts.showVersion || cdcOpts.showVersion) {
            fmt::print("svlens both " SVLENS_VERSION "\n");
            return 0;
        }
        if (!connect::validateConnOptions(connOpts))
            return 1;

        if (dispatch.explicitOutput) {
            connOpts.outputDir = dispatch.outputBase + "/conn";
            cdcOpts.outputDir = dispatch.outputBase + "/cdc";
        }

        connect::CompilationSession session;
        std::string error;
        if (!session.compile(connCompilationArgs, &error)) {
            fmt::print(stderr, "Error: {}\n", error);
            return 1;
        }

        int connExit = connect::runConnWithCompilation(session.compilation(), connOpts);
        int cdcExit = cdccli::runCdcWithCompilation(session.compilation(), cdcOpts);
        if (dispatch.explicitOutput) {
            auto filelists = extractFilelists(connCompilationArgs);
            auto sourceFiles = extractSourceFiles(session.expandedArgs());
            writeBothSummary(dispatch.outputBase,
                             connOpts.topModule,
                             connOpts.format,
                             cdcOpts.format,
                             dispatch.explicitOutput,
                             filelists,
                             sourceFiles,
                             connExit,
                             cdcExit);
        }
        return std::max(connExit, cdcExit);
    }

    fmt::print(stderr, "Unknown mode '{}'\n\n", mode);
    printUsage();
    return 1;
}
