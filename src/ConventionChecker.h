#pragma once
#include "Checker.h"
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
};

ConventionRules loadConventionRules(const std::string& yamlPath);

class ConventionChecker : public IChecker {
public:
    ConventionChecker() = default;
    explicit ConventionChecker(const ConventionRules& rules);
    std::vector<Issue> check(const ConnectionGraph& graph) const override;
private:
    ConventionRules rules_;
};

} // namespace connect
