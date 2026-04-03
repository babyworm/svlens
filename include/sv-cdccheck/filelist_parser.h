#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace sv_cdccheck {

/// Result of parsing a Verilog/EDA filelist (.f file)
struct FilelistResult {
    std::vector<std::string> source_files;
    std::vector<std::string> include_dirs;    // from +incdir+
    std::vector<std::string> defines;         // from +define+
    std::vector<std::string> library_dirs;    // from -y
    std::vector<std::string> library_files;   // from -v
    std::vector<std::string> lib_extensions;  // from +libext+
};

/// Parser for Verilog/EDA filelist files (.f format).
/// Supports source files, +incdir+, +define+, -f/-F recursive includes,
/// -y library dirs, -v library files, and +libext+ extensions.
class FilelistParser {
public:
    /// Parse a filelist file, resolving relative paths from its directory
    static FilelistResult parse(const std::filesystem::path& filelist_path);

    /// Parse from string content (for testing)
    static FilelistResult parseString(const std::string& content,
                                      const std::filesystem::path& base_dir = ".");

private:
    static void parseLine(const std::string& line,
                          const std::filesystem::path& base_dir,
                          FilelistResult& result,
                          int depth = 0);

    static std::string trimWhitespace(const std::string& s);

    static constexpr int MAX_RECURSION = 10;
};

} // namespace sv_cdccheck
