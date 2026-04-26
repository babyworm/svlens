#pragma once
#include "Checker.h"
#include <optional>
#include <regex>
#include <string>
#include <vector>

namespace connect {

struct ConventionRules {
    // Per-direction port name prefixes (OpenTitan / SiFive style, e.g.
    // `i_data`, `o_status`). Empty string disables the check.
    std::string inputPrefix = "i_";
    std::string outputPrefix = "o_";
    std::string instancePrefix = "u_";

    // Round 36+ lowRISC-style extension. Each pattern below is applied
    // as a std::regex (ECMAScript syntax) against the relevant name.
    // Empty string disables the check. All violations are emitted at
    // Severity::INFO to keep --check-convention non-blocking.
    std::string clockPattern;
    std::string resetPattern;
    std::string activeLowSuffix;
    bool lowercasePortNames = false;
    std::string parameterCasePattern;
    std::string typedefSuffixPattern;

    // Round 37: lowRISC's actual port-direction marker is a SUFFIX
    // (`data_i`, `data_o`, `data_io`), not the prefix style. These
    // fields enable the suffix mode without breaking existing prefix
    // configs. Each may carry multiple alternates (comma-separated in
    // YAML, e.g. "_o,_po,_no" for differential-pair-aware outputs);
    // a port is accepted if it matches the prefix OR any of the
    // suffixes for its direction.
    std::vector<std::string> inputSuffixes;
    std::vector<std::string> outputSuffixes;
    std::vector<std::string> inoutSuffixes;

    // Round 37: registered-output `_q` and combinational-input `_d`
    // suffix (lowRISC convention). When set, ports/internal signals
    // ending with the digit-tail (`q2`, `q3`, ...) are also accepted
    // as pipeline stages. Currently checked only when the value is
    // non-empty; empty disables.
    std::string regOutputSuffix;   // e.g. "_q"
    std::string combInputSuffix;   // e.g. "_d"

    // Round 37: lowRISC prohibits signals ending with `_<digit>`
    // (e.g., `foo_1`, `bar_2`) -- such names hint at copy-paste
    // duplication. Set true to flag any port whose name matches
    // `_[0-9]+$`.
    bool rejectDigitOnlySuffix = false;

    // US-39E source-text style checks. Defaults disabled (0 / false)
    // so the YAML must opt-in. lowrisc.yaml enables them when the
    // user wants strict source-text style enforcement.
    int  maxLineLength = 0;                  // 0 disables
    bool prohibitHardTabs = false;
    bool prohibitTrailingWhitespace = false;

    // US-39F file/module naming checks (opt-in).
    bool prohibitMultipleModulesPerFile = false;
    bool enforceFileModuleMatch = false;
};

ConventionRules loadConventionRules(const std::string& yamlPath);

class ConventionChecker : public IChecker {
public:
    ConventionChecker() = default;
    explicit ConventionChecker(const ConventionRules& rules);
    std::vector<Issue> check(const ConnectionGraph& graph) const override;
private:
    ConventionRules rules_;

    // Round 39 review: cache compiled std::regex per pattern instead
    // of re-compiling on every check() call.  std::regex construction
    // is expensive; for an SoC-scale design check() is called once
    // per top, but for batched conn runs the cost adds up.  The
    // optional<bool> presence flag distinguishes "not yet attempted"
    // from "attempted and failed" so the duplicate-INFO entry is
    // emitted exactly once per pattern.
    struct CachedRegex {
        bool attempted = false;
        std::optional<std::regex> re;
    };
    mutable CachedRegex cachedClock_;
    mutable CachedRegex cachedReset_;
    mutable CachedRegex cachedParam_;
    mutable CachedRegex cachedTypedef_;
};

} // namespace connect
