#pragma once

#include "CompilationSession.h"

#include <string>
#include <vector>

namespace cdccli {

struct CdcCliOptions {
    std::string topModule;
    std::string outputDir = "./cdc_reports";
    std::string format = "all";
    std::string sdcFile;
    std::string clockYamlFile;
    std::string waiverFile;
    std::string dumpGraphFile;
    int syncStages = 2;
    bool strict = false;
    bool quiet = false;
    bool verbose = false;
    bool ignoreGated = false;
    bool autoClocks = false;
    bool showHelp = false;
    bool showVersion = false;
    std::string parseError;
};

void printCdcUsage();
CdcCliOptions parseCdcArgs(int argc, const char* const* argv,
                           std::vector<std::string>& compilationArgs);
bool validateCdcOptions(const CdcCliOptions& opts);
int runCdcWithCompilation(slang::ast::Compilation& compilation,
                          const CdcCliOptions& opts);

} // namespace cdccli

int runCdcMain(int argc, char* argv[]);
