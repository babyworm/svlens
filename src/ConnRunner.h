#pragma once

#include "CompilationSession.h"
#include "ConnectionGraph.h"

#include <string>
#include <vector>

namespace connect {

struct ConnCliOptions {
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
    bool showVersion = false;
};

void printConnUsage();
ConnCliOptions parseConnArgs(int argc, const char* const* argv,
                             std::vector<std::string>& compilationArgs);
bool validateConnOptions(const ConnCliOptions& opts);
int runConnWithCompilation(slang::ast::Compilation& compilation,
                           const ConnCliOptions& opts);

} // namespace connect

int runConnMain(int argc, char* argv[]);
