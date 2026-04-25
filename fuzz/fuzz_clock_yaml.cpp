// Fuzz harness for sv_cdccheck::ClockYamlParser::loadString.
//
// ClockYamlParser is a hand-rolled YAML-like parser (not yaml-cpp), exposed
// via the --clock-yaml CLI flag. As an attacker-influenced surface that does
// not benefit from yaml-cpp's defenses, it warrants its own fuzz target.

#include "sv-cdccheck/clock_yaml_parser.h"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0 || size > 1 << 20) {
        return 0;
    }
    std::string content(reinterpret_cast<const char*>(data), size);
    sv_cdccheck::ClockYamlParser parser;
    try {
        (void)parser.loadString(content);
        // Touch the parsed config so getter / accessor paths participate in
        // the coverage signal.
        const auto& cfg = parser.getConfig();
        (void)cfg.clock_sources.size();
        (void)cfg.domain_groups.size();
    } catch (const std::exception&) {
        // Lenient parser is expected to absorb most malformed input; we only
        // care about crashes / sanitizer findings.
    }
    return 0;
}
