#pragma once

#include "sv-cdccheck/types.h"
#include <filesystem>
#include <string>
#include <vector>

namespace sv_cdccheck {

/// Parsed SDC clock definition: create_clock
struct SdcClockDef {
    std::string name;
    std::optional<double> period;
    std::string target;          // extracted from [get_ports sys_clk]
};

/// Parsed SDC generated clock: create_generated_clock
struct SdcGeneratedClockDef {
    std::string name;
    std::string source_clock;    // -source target
    std::string target;          // output pin/port
    int divide_by = 1;
    int multiply_by = 1;
    bool invert = false;
};

/// Parsed SDC clock group: set_clock_groups
struct SdcClockGroup {
    enum class Type { Asynchronous, Exclusive, LogicallyExclusive };
    Type type;
    std::vector<std::vector<std::string>> groups; // each inner vector is one group
};

/// Complete SDC constraints relevant to CDC analysis
struct SdcConstraints {
    std::vector<SdcClockDef> clocks;
    std::vector<SdcGeneratedClockDef> generated_clocks;
    std::vector<SdcClockGroup> clock_groups;
    // TODO Phase 4: set_false_path, set_max_delay
};

/// SDC file parser — extracts clock/reset constraints for CDC analysis.
/// Supports a subset of SDC (Tcl) sufficient for CDC: create_clock,
/// create_generated_clock, set_clock_groups.
class SdcParser {
public:
    /// Parse an SDC file and return extracted constraints
    static SdcConstraints parse(const std::filesystem::path& sdc_path);

private:
    /// Join backslash-continued lines and strip comments
    static std::vector<std::string> preprocessLines(const std::filesystem::path& path);

    /// Tokenize a single SDC command line
    static std::vector<std::string> tokenize(const std::string& line);

    static SdcClockDef parseCreateClock(const std::vector<std::string>& tokens);
    static SdcGeneratedClockDef parseGeneratedClock(const std::vector<std::string>& tokens);
    static SdcClockGroup parseClockGroups(const std::vector<std::string>& tokens);

    /// Extract signal name from Tcl expression: [get_ports sys_clk] → "sys_clk"
    static std::string extractTarget(const std::string& tcl_expr);

    /// Parse brace-delimited list: {clk_a clk_b} → ["clk_a", "clk_b"]
    static std::vector<std::string> parseBraceList(const std::string& s);
};

} // namespace sv_cdccheck
