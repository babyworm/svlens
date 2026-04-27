#include "ConventionChecker.h"
#include <cctype>
#include <fmt/core.h>
#include <yaml-cpp/yaml.h>

#include <optional>
#include <regex>
#include <set>
#include <stdexcept>
#include <vector>

namespace connect {

namespace {

std::optional<std::string> getString(const YAML::Node& node, const char* key) {
    if (node && node[key]) {
        try {
            return node[key].as<std::string>();
        } catch (const YAML::Exception&) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

// Codex Round 2 cross-review: per-field YAML extraction. Previously the
// whole extraction block was wrapped in a single try/catch; a single
// bad scalar (`max_line_length: nope`) aborted the rest of the parse,
// silently disabling later valid keys depending on user key order.
// This helper catches conversion errors per-field so a typo in one
// field no longer skips later valid ones.
template <typename T> void tryAssign(const YAML::Node& node, const char* key, T& dst, const std::string& yamlPath) {
    if (!node[key])
        return;
    try {
        dst = node[key].as<T>();
    } catch (const YAML::Exception& e) {
        fmt::print(stderr,
                   "Warning: convention file '{}' has malformed scalar "
                   "for '{}': {} -- keeping default value\n",
                   yamlPath, key, e.what());
    }
}

} // namespace

ConventionRules loadConventionRules(const std::string& yamlPath) {
    // Round 39 review: yaml-cpp throws on malformed/adversarial YAML
    // (deep nesting, anchor cycles, EOF mid-document).  Wrap the
    // load so a hostile convention file no longer aborts the whole
    // conn run; we surface the parse error as a runtime_error the
    // caller can catch and report.
    YAML::Node root;
    try {
        root = YAML::LoadFile(yamlPath);
    } catch (const YAML::Exception& e) {
        throw std::runtime_error(fmt::format("invalid convention file '{}': YAML parse error: {}", yamlPath, e.what()));
    }
    if (!root || !root.IsMap()) {
        throw std::runtime_error(fmt::format(
            "invalid convention file '{}': expected a YAML mapping at the root", yamlPath));
    }

    ConventionRules rules;

    // Codex Round 2 cross-review: replaced the previous single outer
    // try/catch with per-field tryAssign helpers. The old behavior
    // aborted on the first YAML::BadConversion, silently dropping any
    // later valid keys (e.g. `max_line_length: nope` would disable
    // `prohibit_hard_tabs: true` if the bool came after the int in
    // the user's YAML). Now each field independently catches its own
    // conversion error and the remaining fields still apply.

    // Round 36+: input_prefix has alternate keys (camelCase, nested).
    // The string-typed primary path uses tryAssign for consistency;
    // the alternate paths remain raw because they're already guarded
    // by node existence checks and string conversion of YAML scalars
    // is not a typical failure mode (it accepts any scalar repr).
    if (auto value = getString(root, "input_prefix"); value)
        rules.inputPrefix = *value;
    else if (auto value = getString(root, "inputPrefix"); value)
        rules.inputPrefix = *value;
    else {
        // Codex Round 4 cross-review: guard the parent-shape check too.
        // If `input` is a scalar (e.g. `input: scalar_not_a_map`) then
        // `root["input"]["prefix"]` throws YAML::BadSubscript before the
        // inner try fires.  Wrap the entire parent+child access so a
        // non-map parent node is silently ignored.
        try {
            if (auto in = root["input"]; in && in.IsMap()) {
                if (auto p = in["prefix"]; p && p.IsScalar()) {
                    try {
                        rules.inputPrefix = p.as<std::string>();
                    } catch (const YAML::Exception&) {}
                }
            }
        } catch (const YAML::Exception&) {}
    }

    if (auto value = getString(root, "output_prefix"); value)
        rules.outputPrefix = *value;
    else if (auto value = getString(root, "outputPrefix"); value)
        rules.outputPrefix = *value;
    else {
        try {
            if (auto out = root["output"]; out && out.IsMap()) {
                if (auto p = out["prefix"]; p && p.IsScalar()) {
                    try {
                        rules.outputPrefix = p.as<std::string>();
                    } catch (const YAML::Exception&) {}
                }
            }
        } catch (const YAML::Exception&) {}
    }

    if (auto value = getString(root, "instance_prefix"); value)
        rules.instancePrefix = *value;
    else if (auto value = getString(root, "instancePrefix"); value)
        rules.instancePrefix = *value;
    else {
        try {
            if (auto inst = root["instance"]; inst && inst.IsMap()) {
                if (auto p = inst["prefix"]; p && p.IsScalar()) {
                    try {
                        rules.instancePrefix = p.as<std::string>();
                    } catch (const YAML::Exception&) {}
                }
            }
        } catch (const YAML::Exception&) {}
    }

    // Round 36 lowRISC-style fields. All optional; missing keys leave
    // the field empty/false, which disables the corresponding check.
    if (auto value = getString(root, "clock_pattern"); value)
        rules.clockPattern = *value;
    if (auto value = getString(root, "reset_pattern"); value)
        rules.resetPattern = *value;
    if (auto value = getString(root, "active_low_suffix"); value)
        rules.activeLowSuffix = *value;
    tryAssign(root, "lowercase_port_names", rules.lowercasePortNames, yamlPath);
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
    tryAssign(root, "reject_digit_only_suffix", rules.rejectDigitOnlySuffix, yamlPath);

    // US-39E source-text style checks.
    tryAssign(root, "max_line_length", rules.maxLineLength, yamlPath);
    tryAssign(root, "prohibit_hard_tabs", rules.prohibitHardTabs, yamlPath);
    tryAssign(root, "prohibit_trailing_whitespace", rules.prohibitTrailingWhitespace, yamlPath);

    // US-39F file/module naming checks.
    tryAssign(root, "prohibit_multiple_modules_per_file", rules.prohibitMultipleModulesPerFile, yamlPath);
    tryAssign(root, "enforce_file_module_match", rules.enforceFileModuleMatch, yamlPath);

    return rules;
}

ConventionChecker::ConventionChecker(const ConventionRules& rules) : rules_(rules) {}

std::vector<Issue> ConventionChecker::check(const ConnectionGraph& graph) const {
    std::vector<Issue> issues;

    // Round 36 lowRISC-style: pre-compile regexes once and cache.
    // Invalid patterns are silently ignored (the check is skipped) so
    // a malformed YAML never aborts conn analysis.  A diagnostic is
    // emitted as exactly one CONVENTION INFO entry per pattern thanks
    // to the `attempted` flag on the cache.
    //
    // Round 39 review (R4 H1): defensive ReDoS cap.  Regex patterns
    // longer than 256 chars or containing nested-quantifier markers
    // are rejected.  These are the canonical signature of
    // catastrophic-backtracking patterns; an adversarial
    // convention.yaml could otherwise stall the entire run on a
    // pattern like `(a+)+$`.
    auto looksLikeRedos = [](const std::string& p) {
        // Round 39 review (R4 H1): defensive ReDoS heuristic.  We
        // target only the canonical catastrophic-backtracking
        // patterns — alternation inside a group that itself has a
        // quantifier outside, e.g. `(a+|b+)+`, `(.*|x)+`, `(a|a)*`.
        // Plain repetitions like `(_[a-z]+)*` are NOT ReDoS-prone
        // when the inner class does not include the literal sep,
        // and they appear in routine reset/clock patterns; flagging
        // them would break valid configs.
        try {
            // group containing a `|` AND a `+`/`*`, followed by
            // an outer `+`/`*` quantifier on the group itself.
            static const std::regex altQuant(R"(\([^)]*\|[^)]*[+*][^)]*\)\s*[+*])");
            if (std::regex_search(p, altQuant))
                return true;
        } catch (const std::regex_error&) {
            // If our own ReDoS detector regex fails to compile we
            // skip the heuristic; the length cap below still applies.
        }
        // Three+ consecutive quantifier characters (`+++`, `***`)
        // are never well-formed ECMAScript and serve as a cheap
        // syntactic ReDoS canary independent of the regex engine.
        int run = 0;
        for (char c : p) {
            if (c == '+' || c == '*') {
                if (++run >= 3)
                    return true;
            } else {
                run = 0;
            }
        }
        return false;
    };

    auto tryCompileCached = [&issues, &looksLikeRedos](const std::string& pattern, const char* label,
                                                       CachedRegex& cache) -> const std::regex* {
        if (pattern.empty())
            return nullptr;
        if (cache.attempted)
            return cache.re ? &*cache.re : nullptr;
        cache.attempted = true;
        if (pattern.size() > 256 || looksLikeRedos(pattern)) {
            Issue issue;
            issue.type = Issue::Type::CONVENTION;
            issue.severity = Issue::Severity::INFO;
            issue.detail = fmt::format("convention rule '{}' rejected (length {} or "
                                       "nested-quantifier ReDoS signature) -- skipping",
                                       label, pattern.size());
            issues.push_back(std::move(issue));
            return nullptr;
        }
        try {
            cache.re.emplace(pattern);
            return &*cache.re;
        } catch (const std::regex_error& e) {
            Issue issue;
            issue.type = Issue::Type::CONVENTION;
            issue.severity = Issue::Severity::INFO;
            issue.detail = fmt::format(
                "convention rule '{}' has invalid regex '{}': {}",
                label, pattern, e.what());
            issues.push_back(std::move(issue));
            return nullptr;
        }
    };

    const std::regex* clockRe = tryCompileCached(rules_.clockPattern, "clock_pattern", cachedClock_);
    const std::regex* resetRe = tryCompileCached(rules_.resetPattern, "reset_pattern", cachedReset_);

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
    // tryCompileCached guard added at the top of this function for
    // malformed patterns.
    const std::regex* paramRe = tryCompileCached(rules_.parameterCasePattern, "parameter_case_pattern", cachedParam_);
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
    const std::regex* typedefRe =
        tryCompileCached(rules_.typedefSuffixPattern, "typedef_suffix_pattern", cachedTypedef_);
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
        // Codex cross-review: propagate line/column so JSON consumers
        // get structured fields instead of having to regex-parse detail.
        issue.lineNumber = obs.lineNumber;
        issue.columnNumber = obs.columnNumber;
        issues.push_back(std::move(issue));
    }

    return issues;
}

} // namespace connect
