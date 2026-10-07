#pragma once

#include "sv-cdccheck/types.h"
#include <cstddef>
#include <filesystem>
#include <optional>
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
    std::vector<bool> include_generated;          // parallel to groups; expand resolved master descendants
};

/// Parsed SDC false path: set_false_path
struct SdcFalsePath {
    std::string from;  // -from clock/pin name
    std::string to;    // -to clock/pin name
    bool clock_to_clock = false; // only these can annotate an entire clock-domain pair
};

/// Parsed static clock-to-clock SDC max delay: set_max_delay
struct SdcMaxDelay {
    double delay = 0.0;
    std::string from;
    std::string to;
    bool datapath_only = false;
};

/// Complete SDC constraints relevant to CDC analysis
struct SdcConstraints {
    std::vector<SdcClockDef> clocks;
    std::vector<SdcGeneratedClockDef> generated_clocks;
    std::vector<SdcClockGroup> clock_groups;
    std::vector<SdcFalsePath> false_paths;
    std::vector<SdcMaxDelay> max_delays;
    size_t skipped_clock_groups = 0; // unsupported set_clock_groups commands, not partial groups
    size_t skipped_max_delays = 0;   // malformed or non-clock-pair set_max_delay commands
};

/// SDC file parser — extracts clock/reset constraints for CDC analysis.
/// Supports a static subset of SDC (Tcl), including create_clock,
/// create_generated_clock, literal or get_clocks clock groups, and a static
/// -include_generated_clocks selector. It does not evaluate general Tcl
/// expressions, other options, or wildcard clock collections.
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
    static std::optional<SdcClockGroup> parseClockGroups(const std::vector<std::string>& tokens);
    static SdcFalsePath parseSetFalsePath(const std::vector<std::string>& tokens);
    static std::optional<SdcMaxDelay> parseSetMaxDelay(const std::vector<std::string>& tokens);

    /// Extract signal name from Tcl expression: [get_ports sys_clk] → "sys_clk"
    static std::string extractTarget(const std::string& tcl_expr);

    /// Parse a static brace list or get_clocks selector; reject dynamic Tcl.
    static std::optional<std::vector<std::string>> parseClockGroupNames(const std::string& s, bool& includeGenerated);
};

} // namespace sv_cdccheck
