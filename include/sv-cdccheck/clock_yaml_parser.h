#pragma once

#include <string>
#include <vector>
#include <optional>

namespace sv_cdccheck {

struct ClockDatabase;

/// A single clock output from a clock source in the YAML spec
struct YamlClockOutput {
    std::string signal;
    std::string frequency;       // e.g., "200MHz"
    std::string relationship;    // e.g., "independent"
};

/// A clock source entry from clock_sources section
struct YamlClockSource {
    std::string name;            // e.g., "pll0"
    std::vector<YamlClockOutput> outputs;
};

/// Domain group: a set of clocks with a named relationship
struct YamlDomainGroup {
    std::string type;            // "async" or "related"
    std::vector<std::vector<std::string>> groups;  // each inner vector is a clock group
};

/// Parsed result of a clock YAML file
struct ClockYamlConfig {
    std::vector<YamlClockSource> clock_sources;
    std::vector<YamlDomainGroup> domain_groups;
};

/// Parse clock specification YAML (simple line-by-line, no library)
class ClockYamlParser {
public:
    /// Parse from file path; returns true on success
    bool loadFile(const std::string& path);

    /// Parse from string (for testing)
    bool loadString(const std::string& yaml_content);

    /// Get parsed config
    const ClockYamlConfig& getConfig() const { return config_; }

    /// Apply parsed clock info into a ClockDatabase
    void applyTo(ClockDatabase& clock_db) const;

private:
    ClockYamlConfig config_;

    void parseYaml(const std::string& content);
};

} // namespace sv_cdccheck
