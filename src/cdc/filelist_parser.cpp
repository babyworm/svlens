#include "sv-cdccheck/filelist_parser.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace sv_cdccheck {

namespace fs = std::filesystem;

FilelistResult FilelistParser::parse(const fs::path& filelist_path) {
    std::ifstream file(filelist_path);
    if (!file.is_open()) {
        return {};
    }

    auto base_dir = fs::absolute(filelist_path).parent_path();

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    FilelistResult result;
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        parseLine(line, base_dir, result, 0);
    }
    return result;
}

FilelistResult FilelistParser::parseString(const std::string& content,
                                            const fs::path& base_dir) {
    FilelistResult result;
    auto abs_base = fs::absolute(base_dir);

    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        parseLine(line, abs_base, result, 0);
    }
    return result;
}

std::string FilelistParser::trimWhitespace(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r");
    return s.substr(start, end - start + 1);
}

void FilelistParser::parseLine(const std::string& raw_line,
                                const fs::path& base_dir,
                                FilelistResult& result,
                                int depth) {
    std::string line = trimWhitespace(raw_line);

    // Skip empty lines
    if (line.empty()) return;

    // Skip comments: // or #
    if (line.starts_with("//") || line.starts_with("#")) return;

    // Strip inline comments (// only, not # which could appear in defines)
    auto comment_pos = line.find("//");
    if (comment_pos != std::string::npos) {
        line = trimWhitespace(line.substr(0, comment_pos));
        if (line.empty()) return;
    }

    // +incdir+<path>[+<path2>...]
    if (line.starts_with("+incdir+")) {
        auto paths_str = line.substr(8); // skip "+incdir+"
        std::string token;
        std::istringstream ss(paths_str);
        while (std::getline(ss, token, '+')) {
            token = trimWhitespace(token);
            if (token.empty()) continue;
            fs::path p(token);
            if (p.is_relative())
                p = base_dir / p;
            result.include_dirs.push_back(p.lexically_normal().string());
        }
        return;
    }

    // +define+<macro>[=<value>]
    if (line.starts_with("+define+")) {
        auto def = line.substr(8); // skip "+define+"
        def = trimWhitespace(def);
        if (!def.empty())
            result.defines.push_back(def);
        return;
    }

    // +libext+<ext>[+<ext2>...]
    if (line.starts_with("+libext+")) {
        auto exts_str = line.substr(8); // skip "+libext+"
        std::string token;
        std::istringstream ss(exts_str);
        while (std::getline(ss, token, '+')) {
            token = trimWhitespace(token);
            if (token.empty()) continue;
            result.lib_extensions.push_back(token);
        }
        return;
    }

    // -f <path> — recursive filelist, paths relative to CWD
    if (line.starts_with("-f ") || line.starts_with("-f\t")) {
        if (depth >= MAX_RECURSION) return;
        auto path_str = trimWhitespace(line.substr(2));
        if (path_str.empty()) return;

        fs::path p(path_str);
        if (p.is_relative())
            p = fs::current_path() / p;

        std::ifstream sub_file(p);
        if (!sub_file.is_open()) return;

        auto sub_base = fs::current_path();
        std::string sub_line;
        while (std::getline(sub_file, sub_line)) {
            parseLine(sub_line, sub_base, result, depth + 1);
        }
        return;
    }

    // -F <path> — recursive filelist, paths relative to filelist location
    if (line.starts_with("-F ") || line.starts_with("-F\t")) {
        if (depth >= MAX_RECURSION) return;
        auto path_str = trimWhitespace(line.substr(2));
        if (path_str.empty()) return;

        fs::path p(path_str);
        if (p.is_relative())
            p = base_dir / p;

        std::ifstream sub_file(p);
        if (!sub_file.is_open()) return;

        auto sub_base = fs::absolute(p).parent_path();
        std::string sub_line;
        while (std::getline(sub_file, sub_line)) {
            parseLine(sub_line, sub_base, result, depth + 1);
        }
        return;
    }

    // -y <path> — library directory
    if (line.starts_with("-y ") || line.starts_with("-y\t")) {
        auto path_str = trimWhitespace(line.substr(2));
        if (path_str.empty()) return;
        fs::path p(path_str);
        if (p.is_relative())
            p = base_dir / p;
        result.library_dirs.push_back(p.lexically_normal().string());
        return;
    }

    // -v <path> — library file
    if (line.starts_with("-v ") || line.starts_with("-v\t")) {
        auto path_str = trimWhitespace(line.substr(2));
        if (path_str.empty()) return;
        fs::path p(path_str);
        if (p.is_relative())
            p = base_dir / p;
        result.library_files.push_back(p.lexically_normal().string());
        return;
    }

    // Everything else is a source file path
    fs::path p(line);
    if (p.is_relative())
        p = base_dir / p;
    result.source_files.push_back(p.lexically_normal().string());
}

} // namespace sv_cdccheck
