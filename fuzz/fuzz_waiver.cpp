// Fuzz harness for sv_cdccheck::WaiverManager::loadString (YAML waiver parser).

#include "sv-cdccheck/waiver.h"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0 || size > 1 << 20) {
        return 0;
    }
    std::string content(reinterpret_cast<const char*>(data), size);
    sv_cdccheck::WaiverManager mgr;
    try {
        (void)mgr.loadString(content);
        // Exercise the lookup path with a few synthetic signals.
        (void)mgr.isWaived("top.u_a.q", "top.u_b.d");
        (void)mgr.findWaiver("top.u_a.q", "top.u_b.d");
    } catch (const std::exception&) {
        // Lenient parser is expected to swallow most malformed input; we
        // are only hunting for crashes / sanitizer findings.
    }
    return 0;
}
