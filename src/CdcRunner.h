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
    std::string cdcConfigFile;  // YAML config for safe-cell registry (--cdc-config)
    std::string svaOutputFile;  // --emit-sva: optional SVA output path
    int syncStages = 2;
    bool strict = false;
    bool quiet = false;
    bool verbose = false;
    bool ignoreGated = false;
    bool autoClocks = false;
    bool showHelp = false;
    bool showVersion = false;
    bool checkClockMux = true;  // Ac_cdc05 default-on; --no-check-clock-mux disables
    // User-supplied safe-cell module names. Merged with the built-in
    // defaults inside CdcRunner. Empty by default.
    std::vector<std::string> userSyncCells;
    std::vector<std::string> userGlitchFreeMuxCells;
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
