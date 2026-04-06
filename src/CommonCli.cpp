#include "CommonCli.h"

namespace commoncli {

bool optionTakesValue(std::string_view opt,
                      const std::unordered_set<std::string>& valueOptions) {
    return valueOptions.contains(std::string(opt));
}

bool consumeBasicCliArg(std::string_view arg,
                        int& i,
                        int argc,
                        const char* const* argv,
                        BasicCliState& state) {
    if (arg == "--help" || arg == "-h") {
        state.showHelp = true;
        return true;
    }
    if (arg == "--version") {
        state.showVersion = true;
        return true;
    }
    if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
        state.outputDir = argv[++i];
        return true;
    }
    return false;
}

std::string installHint() {
    return "For offline/preinstalled builds, configure with: cmake -B build-offline -DCMAKE_PREFIX_PATH=\"$HOME/.local\" -DSVLENS_FETCH_DEPS=OFF";
}

std::string docsHint() {
    return "docs/install.md, docs/cli-help.md, docs/schema/*.md";
}

std::string productBoundaryNote() {
    return "svlens is a pre-signoff structural analysis tool for CI gates, design review, and pre-check flows.";
}

std::string passThroughNote() {
    return "Pass-through flags such as -I, -D, --std, -f, and -F are forwarded to slang.";
}

BothModeDispatch routeBothModeArgs(int argc, char* argv[],
                                   int startIndex,
                                   const std::unordered_set<std::string>& connValueOptions,
                                   const std::unordered_set<std::string>& cdcValueOptions) {
    BothModeDispatch dispatch;
    dispatch.connArgs = {"sv-conncheck"};
    dispatch.cdcArgs = {"sv-cdccheck"};

    for (int i = startIndex; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            dispatch.outputBase = argv[++i];
            dispatch.explicitOutput = true;
            continue;
        }
        if (arg.rfind("--conn-", 0) == 0) {
            std::string stripped = "--" + arg.substr(7);
            dispatch.connArgs.push_back(stripped);
            if (i + 1 < argc && optionTakesValue(stripped, connValueOptions) &&
                std::string_view(argv[i + 1]).rfind("--", 0) != 0) {
                dispatch.connArgs.emplace_back(argv[++i]);
            }
            continue;
        }
        if (arg.rfind("--cdc-", 0) == 0) {
            std::string stripped = "--" + arg.substr(6);
            dispatch.cdcArgs.push_back(stripped);
            if (i + 1 < argc && optionTakesValue(stripped, cdcValueOptions) &&
                std::string_view(argv[i + 1]).rfind("--", 0) != 0) {
                dispatch.cdcArgs.emplace_back(argv[++i]);
            }
            continue;
        }
        if (arg.rfind("--metrics-", 0) == 0) {
            // Skip metrics-prefixed flags; they are parsed separately in main.
            static const std::unordered_set<std::string> metricsValueOptions = {
                "--metrics-topk", "--metrics-baseline", "--metrics-max-for-unroll"
            };
            if (metricsValueOptions.contains(arg) && i + 1 < argc) {
                ++i; // skip the value argument
            }
            continue;
        }

        dispatch.connArgs.push_back(arg);
        dispatch.cdcArgs.push_back(arg);
    }

    return dispatch;
}

} // namespace commoncli
