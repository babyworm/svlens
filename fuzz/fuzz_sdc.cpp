// Fuzz harness for sv_cdccheck::SdcParser::parse.
//
// SdcParser only accepts a filesystem path, so we materialize each input as
// a temp file. ASAN/UBSAN/MSAN are layered on top via libFuzzer flags.
//
// Important: write through the file descriptor returned by mkstemps so the
// original fd carries the bytes (no second open(), no TOCTOU window). On
// sanitizer findings libFuzzer can _Exit() without running destructors, so
// orphaned files are inevitable -- mitigate by best-effort cleaning the
// /tmp/svlens_fuzz_sdc_*.sdc set at startup so the disk does not bloat
// across millions of executions in CI.

#include "sv-cdccheck/sdc_parser.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace {

class TempFile {
public:
    explicit TempFile(const char* data, size_t size) {
        char tmpl[] = "/tmp/svlens_fuzz_sdc_XXXXXX.sdc";
        int fd = mkstemps(tmpl, 4);  // 4 = ".sdc" suffix length
        if (fd < 0) {
            return;
        }
        path_ = tmpl;
        // Write the entire payload through the original fd. Loop in case the
        // kernel returns a partial write.
        size_t written = 0;
        while (written < size) {
            ssize_t n = ::write(fd, data + written, size - written);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                ::close(fd);
                std::error_code ec;
                std::filesystem::remove(path_, ec);
                path_.clear();
                return;
            }
            written += static_cast<size_t>(n);
        }
        ::close(fd);
    }
    ~TempFile() {
        if (!path_.empty()) {
            std::error_code ec;
            std::filesystem::remove(path_, ec);
        }
    }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    const std::string& path() const { return path_; }

private:
    std::string path_;
};

// Best-effort cleanup of leftover fuzz temp files from a previous _Exit().
// Runs once at process start.
struct StartupSweeper {
    StartupSweeper() {
        std::error_code ec;
        for (auto& entry : std::filesystem::directory_iterator("/tmp", ec)) {
            if (ec) {
                return;
            }
            const auto& name = entry.path().filename().string();
            if (name.rfind("svlens_fuzz_sdc_", 0) == 0) {
                std::error_code rm_ec;
                std::filesystem::remove(entry.path(), rm_ec);
            }
        }
    }
};
StartupSweeper g_sweeper;

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0 || size > 1 << 20) {  // skip empty and pathologically large inputs
        return 0;
    }
    TempFile tmp(reinterpret_cast<const char*>(data), size);
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
