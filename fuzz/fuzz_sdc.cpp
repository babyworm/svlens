// Fuzz harness for sv_cdccheck::SdcParser::parse.
//
// SdcParser only accepts a filesystem path, so we materialize each input as
// a temp file. ASAN/UBSAN/MSAN are layered on top via libFuzzer flags.

#include "sv-cdccheck/sdc_parser.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {

class TempFile {
public:
    explicit TempFile(const std::string& content) {
        char tmpl[] = "/tmp/svlens_fuzz_sdc_XXXXXX.sdc";
        int fd = mkstemps(tmpl, 4);
        if (fd < 0) {
            return;
        }
        path_ = tmpl;
        std::ofstream(path_) << content;
        ::close(fd);
    }
    ~TempFile() {
        if (!path_.empty()) {
            std::error_code ec;
            std::filesystem::remove(path_, ec);
        }
    }
    const std::string& path() const { return path_; }

private:
    std::string path_;
};

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0 || size > 1 << 20) {  // skip empty and pathologically large inputs
        return 0;
    }
    std::string content(reinterpret_cast<const char*>(data), size);
    TempFile tmp(content);
    if (tmp.path().empty()) {
        return 0;
    }
    try {
        (void)sv_cdccheck::SdcParser::parse(tmp.path());
    } catch (const std::exception&) {
        // Malformed SDC may throw; we are only hunting for crashes.
    }
    return 0;
}
