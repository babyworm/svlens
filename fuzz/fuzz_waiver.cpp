// Fuzz harness for sv_cdccheck::WaiverManager::loadString (YAML waiver parser).

#include "sv-cdccheck/waiver.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace {

// Extract up to two newline-separated tokens from the fuzzer input, capped
// at `max_len` each. Used to feed adversarial signal names into the
// lookup path so glob / pattern branches inside WaiverManager participate
// in coverage instead of always seeing the same two constant strings.
std::array<std::string, 2> deriveSignals(const std::string& content, size_t max_len = 128) {
    std::array<std::string, 2> out{"top.u_a.q", "top.u_b.d"};  // fallback
    size_t pos = 0;
    for (size_t i = 0; i < out.size() && pos < content.size(); ++i) {
        size_t nl = content.find('\n', pos);
        if (nl == std::string::npos) {
            nl = content.size();
        }
        size_t len = std::min(nl - pos, max_len);
        if (len > 0) {
            out[i] = content.substr(pos, len);
        }
        pos = nl + 1;
    }
    return out;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0 || size > 1 << 20) {
        return 0;
    }
    std::string content(reinterpret_cast<const char*>(data), size);
    sv_cdccheck::WaiverManager mgr;
    try {
        (void)mgr.loadString(content);
        // Derive lookup signals from the input so the pattern-matching path
        // is exercised with adversarial strings rather than constants.
        auto signals = deriveSignals(content);
        (void)mgr.isWaived(signals[0], signals[1]);
        (void)mgr.findWaiver(signals[0], signals[1]);
    } catch (const std::exception&) {
        // Lenient parser is expected to swallow most malformed input; we
        // are only hunting for crashes / sanitizer findings.
    }
    return 0;
}
