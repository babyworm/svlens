#include "sv-cdccheck/clock_yaml_parser.h"
#include "sv-cdccheck/types.h"

#include <fstream>
#include <sstream>

namespace sv_cdccheck {

bool ClockYamlParser::loadFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    parseYaml(content);
    return true;
}

bool ClockYamlParser::loadString(const std::string& yaml_content) {
    config_ = ClockYamlConfig{};
    parseYaml(yaml_content);
    return !config_.clock_sources.empty() || !config_.domain_groups.empty();
}

static std::string trimmed(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::string unquote(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'')
        return s.substr(1, s.size() - 2);
    return s;
}

/// Parse a bracketed list like [sys_clk, pixel_clk]
static std::vector<std::string> parseBracketList(const std::string& s) {
    std::vector<std::string> result;
    auto open = s.find('[');
    auto close = s.find(']');
    if (open == std::string::npos || close == std::string::npos || close <= open)
        return result;

    std::string inner = s.substr(open + 1, close - open - 1);
    std::istringstream ss(inner);
    std::string token;
    while (std::getline(ss, token, ',')) {
        auto t = trimmed(token);
        if (!t.empty()) result.push_back(t);
    }
    return result;
}

/// Get indentation level (number of leading spaces)
static int indentLevel(const std::string& line) {
    int count = 0;
    for (char c : line) {
        if (c == ' ') count++;
        else if (c == '\t') count += 2;
        else break;
    }
    return count;
}

void ClockYamlParser::parseYaml(const std::string& content) {
    std::istringstream stream(content);
    std::string line;

    enum class Section { None, ClockSources, DomainGroups } section = Section::None;
    enum class SubSection { None, Outputs } subsection = SubSection::None;
    std::string current_domain_type; // "async" or "related"

    while (std::getline(stream, line)) {
        std::string tl = trimmed(line);

        // Skip comments and empty lines
        if (tl.empty() || tl[0] == '#') continue;

        // Top-level section detection
        if (tl == "clock_sources:" || tl.starts_with("clock_sources:")) {
            section = Section::ClockSources;
            subsection = SubSection::None;
            continue;
        }
        if (tl == "domain_groups:" || tl.starts_with("domain_groups:")) {
            section = Section::DomainGroups;
            subsection = SubSection::None;
            continue;
        }

        int indent = indentLevel(line);

        if (section == Section::ClockSources) {
            // New clock source entry: "- name: pll0"
            if (tl.starts_with("- name:")) {
                YamlClockSource src;
                src.name = unquote(trimmed(tl.substr(7)));
                config_.clock_sources.push_back(std::move(src));
                subsection = SubSection::None;
                continue;
            }

            // outputs: subsection
            if (tl == "outputs:" || tl.starts_with("outputs:")) {
                subsection = SubSection::Outputs;
                continue;
            }

            if (subsection == SubSection::Outputs && !config_.clock_sources.empty()) {
                auto& current_src = config_.clock_sources.back();

                // New output entry: "- signal: sys_clk"
                if (tl.starts_with("- signal:")) {
                    YamlClockOutput out;
                    out.signal = unquote(trimmed(tl.substr(9)));
                    current_src.outputs.push_back(std::move(out));
                    continue;
                }

                // Continuation fields within an output entry
                if (!current_src.outputs.empty()) {
                    auto colon = tl.find(':');
                    if (colon != std::string::npos) {
                        std::string key = trimmed(tl.substr(0, colon));
                        std::string val = unquote(trimmed(tl.substr(colon + 1)));

                        auto& out = current_src.outputs.back();
                        if (key == "signal") out.signal = val;
                        else if (key == "frequency") out.frequency = val;
                        else if (key == "relationship") out.relationship = val;
                    }
                }
            }

            // name field as continuation (not after dash)
            if (tl.starts_with("name:") && indent > 0 && !config_.clock_sources.empty()) {
                config_.clock_sources.back().name = unquote(trimmed(tl.substr(5)));
                continue;
            }
        }

        if (section == Section::DomainGroups) {
            // Subsection type: "async:" or "related:"
            auto colon = tl.find(':');
            if (colon != std::string::npos && indent >= 2 && !tl.starts_with("- ")) {
                std::string key = trimmed(tl.substr(0, colon));
                std::string val = trimmed(tl.substr(colon + 1));
                if (val.empty()) {
                    // This is a domain group type like "async:" or "related:"
                    current_domain_type = key;

                    // Find or create the domain group
                    bool found = false;
                    for (auto& dg : config_.domain_groups) {
                        if (dg.type == current_domain_type) { found = true; break; }
                    }
                    if (!found) {
                        YamlDomainGroup dg;
                        dg.type = current_domain_type;
                        config_.domain_groups.push_back(std::move(dg));
                    }
                    continue;
                }
            }

            // List entries under a domain group type: "- [sys_clk, pixel_clk]"
            if (tl.starts_with("- [") && !current_domain_type.empty()) {
                auto clocks = parseBracketList(tl.substr(2));
                if (!clocks.empty()) {
                    for (auto& dg : config_.domain_groups) {
                        if (dg.type == current_domain_type) {
                            dg.groups.push_back(clocks);
                            break;
                        }
                    }
                }
            }
        }
    }
}

void ClockYamlParser::applyTo(ClockDatabase& clock_db) const {
    // Register clock sources
    for (auto& yaml_src : config_.clock_sources) {
        for (auto& out : yaml_src.outputs) {
            // Check if source already exists
            ClockSource* existing = nullptr;
            for (auto& s : clock_db.sources) {
                if (s->name == out.signal) { existing = s.get(); break; }
            }

            if (!existing) {
                auto src = std::make_unique<ClockSource>();
                src->name = out.signal;
                src->id = yaml_src.name + "_" + out.signal;
                src->origin_signal = out.signal;
                src->type = ClockSource::Type::Primary;

                // Parse frequency to period
                if (!out.frequency.empty()) {
                    double freq_mhz = 0;
                    std::string freq_str = out.frequency;
                    if (freq_str.ends_with("MHz") || freq_str.ends_with("Mhz") ||
                        freq_str.ends_with("mhz")) {
                        freq_mhz = std::stod(freq_str.substr(0, freq_str.size() - 3));
                    } else if (freq_str.ends_with("GHz") || freq_str.ends_with("Ghz") ||
                               freq_str.ends_with("ghz")) {
                        freq_mhz = std::stod(freq_str.substr(0, freq_str.size() - 3)) * 1000.0;
                    } else if (freq_str.ends_with("KHz") || freq_str.ends_with("Khz") ||
                               freq_str.ends_with("khz")) {
                        freq_mhz = std::stod(freq_str.substr(0, freq_str.size() - 3)) / 1000.0;
                    }
                    if (freq_mhz > 0) {
                        src->period_ns = 1000.0 / freq_mhz;
                    }
                }

                clock_db.addSource(std::move(src));
            }
        }
    }

    // Apply domain group relationships
    for (auto& dg : config_.domain_groups) {
        DomainRelationship::Type rel_type;
        if (dg.type == "async")
            rel_type = DomainRelationship::Type::Asynchronous;
        else if (dg.type == "related")
            rel_type = DomainRelationship::Type::SameSource;
        else
            continue;

        for (auto& group : dg.groups) {
            // For each pair in the group, add a relationship
            for (size_t i = 0; i < group.size(); ++i) {
                for (size_t j = i + 1; j < group.size(); ++j) {
                    ClockSource* src_a = nullptr;
                    ClockSource* src_b = nullptr;
                    for (auto& s : clock_db.sources) {
                        if (s->name == group[i]) src_a = s.get();
                        if (s->name == group[j]) src_b = s.get();
                    }
                    if (src_a && src_b) {
                        clock_db.relationships.push_back({src_a, src_b, rel_type});
                    }
                }
            }
        }
    }
}

} // namespace sv_cdccheck
