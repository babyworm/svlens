// Fuzz harness for sv_cdccheck::FilelistParser::parseString.
//
// Goal: discover crashes / asan/ubsan violations in the filelist tokenizer
// and recursive include handler. The string variant is used so the fuzzer
// does not need to manage temp files.

#include "sv-cdccheck/filelist_parser.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0) {
        return 0;
    }
    std::string content(reinterpret_cast<const char*>(data), size);
    try {
        // Use /tmp as base_dir; we are not actually opening files here, only
        // exercising the line/token parsing. Recursive `-f` includes will
        // attempt file IO and may throw, which is allowed.
        (void)sv_cdccheck::FilelistParser::parseString(content, std::filesystem::path("/tmp"));
    } catch (const std::exception&) {
        // Parser is allowed to throw on malformed inputs; we only care about
        // crashes / sanitizer violations.
    }
    return 0;
}
