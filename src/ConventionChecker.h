#pragma once
#include "Checker.h"
#include <string>

namespace connect {

struct ConventionRules {
    // Per-direction port name prefixes. Empty string disables the
    // corresponding check (handy for projects that prefer bare names).
    std::string inputPrefix = "i_";
    std::string outputPrefix = "o_";
    std::string instancePrefix = "u_";

    // Round 36 lowRISC-style extension. Each pattern below is applied
    // as a std::regex (ECMAScript syntax) against the relevant name.
    // Empty string disables the check. All violations are emitted at
    // Severity::INFO to keep --check-convention non-blocking.
    //
    // Examples (lowRISC):
    //   clockPattern  = "^clk(_[a-z0-9]+)?$"      (clk, clk_aon, ...)
    //   resetPattern  = "^rst_n(_[a-z0-9]+)?[i]?$" (rst_n, rst_ni, rst_n_aon)
    //   activeLowSuffix = "_n"  (or "_ni") -- a port whose name ends
    //                     with this suffix is expected to be a reset
    //                     OR an explicit active-low signal.
    //   lowercasePortNames = true -- reject MixedCase port names.
    //   parameterCasePattern = "^[A-Z][A-Z0-9_]*$" (UPPER_CASE for params)
    //   typedefSuffixPattern = ".*_t$" (typedefs end with `_t`)
    std::string clockPattern;
    std::string resetPattern;
    std::string activeLowSuffix;
    bool lowercasePortNames = false;
    std::string parameterCasePattern;
    std::string typedefSuffixPattern;
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
