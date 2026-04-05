#pragma once

#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace commoncli {

struct BasicCliState {
    std::string outputDir;
    bool showHelp = false;
    bool showVersion = false;
};

struct BothModeDispatch {
    std::vector<std::string> connArgs;
    std::vector<std::string> cdcArgs;
    std::string outputBase = "./svlens_reports";
    bool explicitOutput = false;
};

bool optionTakesValue(std::string_view opt,
                      const std::unordered_set<std::string>& valueOptions);

bool consumeBasicCliArg(std::string_view arg,
                        int& i,
                        int argc,
                        const char* const* argv,
                        BasicCliState& state);

std::string installHint();
std::string docsHint();
std::string productBoundaryNote();
std::string passThroughNote();

BothModeDispatch routeBothModeArgs(int argc, char* argv[],
                                   int startIndex,
                                   const std::unordered_set<std::string>& connValueOptions,
                                   const std::unordered_set<std::string>& cdcValueOptions);

} // namespace commoncli
