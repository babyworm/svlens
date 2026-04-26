#include "ConventionChecker.h"
#include <fmt/core.h>
#include <yaml-cpp/yaml.h>

#include <optional>
#include <regex>
#include <stdexcept>
#include <set>

namespace connect {

namespace {

std::optional<std::string> getString(const YAML::Node& node, const char* key) {
    if (node && node[key])
        return node[key].as<std::string>();
    return std::nullopt;
}

} // namespace

ConventionRules loadConventionRules(const std::string& yamlPath) {
    YAML::Node root = YAML::LoadFile(yamlPath);
    if (!root || !root.IsMap()) {
        throw std::runtime_error(fmt::format(
            "invalid convention file '{}': expected a YAML mapping at the root", yamlPath));
    }

    ConventionRules rules;

    if (auto value = getString(root, "input_prefix"); value)
        rules.inputPrefix = *value;
    else if (auto value = getString(root, "inputPrefix"); value)
        rules.inputPrefix = *value;
    else if (root["input"] && root["input"]["prefix"])
        rules.inputPrefix = root["input"]["prefix"].as<std::string>();

    if (auto value = getString(root, "output_prefix"); value)
        rules.outputPrefix = *value;
    else if (auto value = getString(root, "outputPrefix"); value)
        rules.outputPrefix = *value;
    else if (root["output"] && root["output"]["prefix"])
        rules.outputPrefix = root["output"]["prefix"].as<std::string>();

    if (auto value = getString(root, "instance_prefix"); value)
        rules.instancePrefix = *value;
    else if (auto value = getString(root, "instancePrefix"); value)
        rules.instancePrefix = *value;
    else if (root["instance"] && root["instance"]["prefix"])
        rules.instancePrefix = root["instance"]["prefix"].as<std::string>();

    // Round 36 lowRISC-style fields. All optional; missing keys leave
    // the field empty/false, which disables the corresponding check.
    if (auto value = getString(root, "clock_pattern"); value)
        rules.clockPattern = *value;
    if (auto value = getString(root, "reset_pattern"); value)
        rules.resetPattern = *value;
    if (auto value = getString(root, "active_low_suffix"); value)
        rules.activeLowSuffix = *value;
    if (root["lowercase_port_names"] &&
        root["lowercase_port_names"].IsScalar())
        rules.lowercasePortNames =
            root["lowercase_port_names"].as<bool>();
    if (auto value = getString(root, "parameter_case_pattern"); value)
        rules.parameterCasePattern = *value;
    if (auto value = getString(root, "typedef_suffix_pattern"); value)
        rules.typedefSuffixPattern = *value;

    // Round 37 lowRISC suffix mode + digit-tail rejection. Each
    // suffix field accepts a comma-separated list of alternates
    // ("_o,_po,_no") so differential-pair (`_p`/`_n`) and other
    // compound markers can be enumerated. Single-string entries
    // are equivalent to a 1-element list.
    auto split_csv = [](const std::string& s) {
        std::vector<std::string> out;
        size_t start = 0;
        while (start < s.size()) {
            size_t end = s.find(',', start);
            if (end == std::string::npos)
                end = s.size();
            // Trim whitespace
            size_t b = start, e = end;
            while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
            while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
            if (b < e)
                out.emplace_back(s, b, e - b);
            start = end + 1;
        }
        return out;
    };
    if (auto value = getString(root, "input_suffix"); value)
        rules.inputSuffixes = split_csv(*value);
    if (auto value = getString(root, "output_suffix"); value)
        rules.outputSuffixes = split_csv(*value);
    if (auto value = getString(root, "inout_suffix"); value)
        rules.inoutSuffixes = split_csv(*value);
    if (auto value = getString(root, "reg_output_suffix"); value)
        rules.regOutputSuffix = *value;
    if (auto value = getString(root, "comb_input_suffix"); value)
        rules.combInputSuffix = *value;
    if (root["reject_digit_only_suffix"] &&
        root["reject_digit_only_suffix"].IsScalar())
        rules.rejectDigitOnlySuffix =
            root["reject_digit_only_suffix"].as<bool>();

    // US-39E source-text style checks.
    if (root["max_line_length"] && root["max_line_length"].IsScalar())
        rules.maxLineLength = root["max_line_length"].as<int>();
    if (root["prohibit_hard_tabs"] && root["prohibit_hard_tabs"].IsScalar())
        rules.prohibitHardTabs = root["prohibit_hard_tabs"].as<bool>();
    if (root["prohibit_trailing_whitespace"] &&
        root["prohibit_trailing_whitespace"].IsScalar())
        rules.prohibitTrailingWhitespace =
            root["prohibit_trailing_whitespace"].as<bool>();

    // US-39F file/module naming checks.
    if (root["prohibit_multiple_modules_per_file"] &&
        root["prohibit_multiple_modules_per_file"].IsScalar())
        rules.prohibitMultipleModulesPerFile =
            root["prohibit_multiple_modules_per_file"].as<bool>();
    if (root["enforce_file_module_match"] &&
        root["enforce_file_module_match"].IsScalar())
        rules.enforceFileModuleMatch =
            root["enforce_file_module_match"].as<bool>();

    return rules;
}

ConventionChecker::ConventionChecker(const ConventionRules& rules) : rules_(rules) {}

std::vector<Issue> ConventionChecker::check(const ConnectionGraph& graph) const {
    std::vector<Issue> issues;

    // Round 36 lowRISC-style: pre-compile regexes once. Invalid
    // patterns are silently ignored (the check is skipped) so a
    // malformed YAML never aborts conn analysis. A separate
    // diagnostic is emitted as a single CONVENTION INFO entry.
    auto tryCompile = [&issues](const std::string& pattern,
                                const char* label) -> std::optional<std::regex> {
        if (pattern.empty())
            return std::nullopt;
        try {
            return std::regex(pattern);
        } catch (const std::regex_error& e) {
            Issue issue;
            issue.type = Issue::Type::CONVENTION;
            issue.severity = Issue::Severity::INFO;
            issue.detail = fmt::format(
                "convention rule '{}' has invalid regex '{}': {}",
                label, pattern, e.what());
            issues.push_back(std::move(issue));
            return std::nullopt;
        }
    };
    auto clockRe = tryCompile(rules_.clockPattern, "clock_pattern");
    auto resetRe = tryCompile(rules_.resetPattern, "reset_pattern");

    // Check port naming conventions
    for (auto& port : graph.allPorts) {
        // Round 36: lowRISC-style auxiliary checks fire BEFORE the
        // direction-prefix check so a name that matches a clock or
        // reset pattern is excused from the i_/o_ requirement.
        bool isClockLike = clockRe && std::regex_match(port.portName, *clockRe);
        bool isResetLike = resetRe && std::regex_match(port.portName, *resetRe);

        if (rules_.lowercasePortNames) {
            for (char c : port.portName) {
                if (c >= 'A' && c <= 'Z') {
                    Issue issue;
                    issue.type = Issue::Type::CONVENTION;
                    issue.severity = Issue::Severity::INFO;
                    issue.port = port;
                    issue.detail = fmt::format(
                        "port '{}' is not lowercase (lowRISC-style requires "
                        "snake_case)", port.portName);
                    issues.push_back(std::move(issue));
                    break;
                }
            }
        }

        // Active-low suffix: a port whose name ends with the suffix
        // should be a reset OR an explicit active-low signal.
        // Heuristic: just verify the suffix appears on at least the
        // declared reset; we cannot detect intent without semantic
        // info, so emit only when activeLowSuffix is configured AND
        // the port is in `In` direction yet does NOT match either
        // the clock or reset patterns -- a defensive informational
        // hint.
        if (!rules_.activeLowSuffix.empty() &&
            port.direction == slang::ast::ArgumentDirection::In &&
            port.portName.ends_with(rules_.activeLowSuffix) &&
            !isResetLike && !isClockLike) {
            Issue issue;
            issue.type = Issue::Type::CONVENTION;
            issue.severity = Issue::Severity::INFO;
            issue.port = port;
            issue.detail = fmt::format(
                "input port '{}' has active-low suffix '{}' but does not "
                "match the configured reset/clock pattern -- verify intent",
                port.portName, rules_.activeLowSuffix);
            issues.push_back(std::move(issue));
        }

        // Round 37 reject_digit_only_suffix: lowRISC prohibits names
        // ending with `_<digit>+` (e.g., `foo_1`, `bar_2`). Pipeline
        // stages should use `q2`, `q3` (joined to `_q`) instead.
        if (rules_.rejectDigitOnlySuffix) {
            // Match `_[0-9]+$` -- underscore followed by digits to EOL.
            const auto& nm = port.portName;
            auto pos = nm.rfind('_');
            if (pos != std::string::npos && pos + 1 < nm.size()) {
                bool all_digits = true;
                for (size_t i = pos + 1; i < nm.size(); ++i) {
                    if (!std::isdigit(static_cast<unsigned char>(nm[i]))) {
                        all_digits = false;
                        break;
                    }
                }
                if (all_digits) {
                    Issue issue;
                    issue.type = Issue::Type::CONVENTION;
                    issue.severity = Issue::Severity::INFO;
                    issue.port = port;
                    issue.detail = fmt::format(
                        "port '{}' has `_<digit>` suffix (lowRISC prohibits "
                        "this; use `_q2`/`_q3` for pipeline stages)",
                        port.portName);
                    issues.push_back(std::move(issue));
                }
            }
        }

        // Round 37 lowRISC suffix mode: a port matches the direction
        // contract when the prefix OR ANY suffix is satisfied. All
        // empty disables the per-direction check entirely.
        auto matches_direction =
            [&](const std::string& prefix,
                const std::vector<std::string>& suffixes) {
                if (prefix.empty() && suffixes.empty())
                    return true;  // no rule -> accept anything
                if (!prefix.empty() && port.portName.starts_with(prefix))
                    return true;
                for (const auto& sfx : suffixes) {
                    if (!sfx.empty() && port.portName.ends_with(sfx))
                        return true;
                }
                return false;
            };
        auto direction_msg =
            [](const std::string& prefix,
               const std::vector<std::string>& suffixes,
               std::string& out) {
                std::string sfx_label;
                for (size_t i = 0; i < suffixes.size(); ++i) {
                    if (suffixes[i].empty()) continue;
                    if (!sfx_label.empty()) sfx_label += " | ";
                    sfx_label += "'" + suffixes[i] + "'";
                }
                if (!prefix.empty() && !sfx_label.empty()) {
                    out = "expected prefix '" + prefix + "' or suffix " +
                          sfx_label;
                } else if (!prefix.empty()) {
                    out = "expected prefix '" + prefix + "'";
                } else {
                    out = "expected suffix " + sfx_label;
                }
            };

        // Direction-prefix/suffix: skip when the port is a recognized
        // clock or reset (those have their own naming pattern).
        if (port.direction == slang::ast::ArgumentDirection::In) {
            if (isClockLike || isResetLike)
                continue;
            if (!matches_direction(rules_.inputPrefix, rules_.inputSuffixes)) {
                std::string contract;
                direction_msg(rules_.inputPrefix, rules_.inputSuffixes, contract);
                Issue issue;
                issue.type = Issue::Type::CONVENTION;
                issue.severity = Issue::Severity::INFO;
                issue.port = port;
                issue.detail = fmt::format(
                    "input port '{}' does not follow naming convention ({})",
                    port.portName, contract);
                issues.push_back(std::move(issue));
            }
        } else if (port.direction == slang::ast::ArgumentDirection::Out) {
            if (!matches_direction(rules_.outputPrefix, rules_.outputSuffixes)) {
                std::string contract;
                direction_msg(rules_.outputPrefix, rules_.outputSuffixes, contract);
                Issue issue;
                issue.type = Issue::Type::CONVENTION;
                issue.severity = Issue::Severity::INFO;
                issue.port = port;
                issue.detail = fmt::format(
                    "output port '{}' does not follow naming convention ({})",
                    port.portName, contract);
                issues.push_back(std::move(issue));
            }
        } else if (port.direction == slang::ast::ArgumentDirection::InOut) {
            // Round 37: explicit `_io` suffix list for bidirectional
            // ports. No prefix counterpart; only check when configured.
            if (!rules_.inoutSuffixes.empty()) {
                bool ok = false;
                for (const auto& sfx : rules_.inoutSuffixes) {
                    if (!sfx.empty() && port.portName.ends_with(sfx)) {
                        ok = true; break;
                    }
                }
                if (!ok) {
                    std::string sfx_label;
                    for (size_t i = 0; i < rules_.inoutSuffixes.size(); ++i) {
                        if (rules_.inoutSuffixes[i].empty()) continue;
                        if (!sfx_label.empty()) sfx_label += " | ";
                        sfx_label += "'" + rules_.inoutSuffixes[i] + "'";
                    }
                    Issue issue;
                    issue.type = Issue::Type::CONVENTION;
                    issue.severity = Issue::Severity::INFO;
                    issue.port = port;
                    issue.detail = fmt::format(
                        "inout port '{}' does not follow naming convention "
                        "(expected suffix {})",
                        port.portName, sfx_label);
                    issues.push_back(std::move(issue));
                }
            }
        }
    }

    // Check instance naming conventions. Round 37: skip the top
    // module path because the top is the user-named DUT, not an
    // instantiation site subject to the `u_` prefix rule.
    std::set<std::string> checkedInstances;
    for (auto& port : graph.allPorts) {
        const auto& path = port.instancePath;
        if (checkedInstances.contains(path))
            continue;
        checkedInstances.insert(path);
        if (path == graph.topModule)
            continue;

        // Extract the last component of the instance path (after last '.')
        std::string instName = path;
        auto dotPos = path.rfind('.');
        if (dotPos != std::string::npos) {
            instName = path.substr(dotPos + 1);
        }

        if (!rules_.instancePrefix.empty() &&
            !instName.starts_with(rules_.instancePrefix)) {
            Issue issue;
            issue.type = Issue::Type::CONVENTION;
            issue.severity = Issue::Severity::INFO;
            issue.port = port; // Use first port of this instance as reference
            issue.detail = fmt::format(
                "instance '{}' does not follow naming convention (expected prefix '{}')",
                instName, rules_.instancePrefix);
            issues.push_back(std::move(issue));
        }
    }

    // Round 38 US-38D: parameter case pattern check. Uses the same
    // tryCompile guard added at the top of this function for malformed
    // patterns.
    auto paramRe = tryCompile(rules_.parameterCasePattern, "parameter_case_pattern");
    if (paramRe) {
        for (const auto& cap : graph.parameters) {
            if (cap.name.empty()) continue;
            if (!std::regex_match(cap.name, *paramRe)) {
                Issue issue;
                issue.type = Issue::Type::CONVENTION;
                issue.severity = Issue::Severity::INFO;
                PortInfo p;
                p.instancePath = cap.scopePath;
                p.portName = cap.name;
                p.location = cap.location;
                issue.port = std::move(p);
                issue.detail = fmt::format(
                    "parameter '{}' does not follow naming convention "
                    "(expected pattern '{}')",
                    cap.name, rules_.parameterCasePattern);
                issues.push_back(std::move(issue));
            }
        }
    }

    // Round 38 US-38E: typedef suffix pattern check.
    auto typedefRe = tryCompile(rules_.typedefSuffixPattern, "typedef_suffix_pattern");
    if (typedefRe) {
        for (const auto& cap : graph.typedefs) {
            if (cap.name.empty()) continue;
            if (!std::regex_search(cap.name, *typedefRe)) {
                Issue issue;
                issue.type = Issue::Type::CONVENTION;
                issue.severity = Issue::Severity::INFO;
                PortInfo p;
                p.instancePath = cap.scopePath;
                p.portName = cap.name;
                p.location = cap.location;
                issue.port = std::move(p);
                issue.detail = fmt::format(
                    "typedef '{}' does not follow naming convention "
                    "(expected suffix pattern '{}')",
                    cap.name, rules_.typedefSuffixPattern);
                issues.push_back(std::move(issue));
            }
        }
    }

    // Round 38: surface ConnectionExtractor-collected style
    // observations (legacy always block, anonymous enum, unnamed
    // generate block, parameter / typedef name violations). Each
    // observation already carries the message body in `detail`.
    for (const auto& obs : graph.styleObservations) {
        Issue issue;
        issue.type = Issue::Type::CONVENTION;
        issue.severity = Issue::Severity::INFO;
        // Synthesize a port-info shell carrying the scope path so the
        // existing JSON renderer surfaces a stable location.
        PortInfo p;
        p.instancePath = obs.scopePath;
        p.portName = obs.name.empty() ? std::string("<no-name>") : obs.name;
        p.location = obs.location;
        issue.port = std::move(p);
        issue.detail = obs.detail;
        issues.push_back(std::move(issue));
    }

    return issues;
}

} // namespace connect
