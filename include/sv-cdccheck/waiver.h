#pragma once

#include <string>
#include <vector>
#include <optional>

namespace sv_cdccheck {

/// A single waiver entry from the YAML waiver file
struct WaiverEntry {
    std::string id;           // e.g., "WAIVE-001"
    std::string crossing;     // exact crossing: "src -> dest"
    std::string pattern;      // glob pattern: "top.u_debug.*"
    std::string reason;
    std::string owner;
    std::string date;         // e.g., "2025-01-15"
};

/// Manages waivers loaded from YAML files
class WaiverManager {
public:
    /// Parse a waiver YAML file (simple line-by-line key-value parser)
    bool loadFile(const std::string& path);

    /// Parse waiver YAML from a string (for testing)
    bool loadString(const std::string& yaml_content);

    /// Check if a crossing from source_signal to dest_signal is waived
    bool isWaived(const std::string& source_signal,
                  const std::string& dest_signal) const;

    /// Get the waiver entry that matches, if any
    std::optional<WaiverEntry> findWaiver(const std::string& source_signal,
                                           const std::string& dest_signal) const;

    const std::vector<WaiverEntry>& getWaivers() const { return waivers_; }

private:
    std::vector<WaiverEntry> waivers_;

    /// Parse the YAML content into waiver entries
    void parseYaml(const std::string& content);

    /// Check if a signal matches a glob-style pattern
    static bool matchPattern(const std::string& pattern,
                             const std::string& signal);

    /// Check if a crossing string matches source->dest
    static bool matchCrossing(const std::string& crossing,
                               const std::string& source,
                               const std::string& dest);
};

} // namespace sv_cdccheck
