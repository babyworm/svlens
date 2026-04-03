#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <filesystem>
#include "sv-cdccheck/filelist_parser.h"

namespace fs = std::filesystem;
using namespace sv_cdccheck;

// Helper: write a temp file and return its path
static fs::path writeTempFile(const std::string& content, const std::string& suffix = ".f") {
    static int counter = 0;
    auto path = fs::temp_directory_path() / ("test_filelist_" + std::to_string(counter++) + suffix);
    std::ofstream(path) << content;
    return path;
}

TEST_CASE("Filelist parser: basic source files", "[filelist]") {
    auto base = fs::temp_directory_path();
    auto result = FilelistParser::parseString(
        "rtl/top.sv\n"
        "rtl/core.sv\n",
        base);

    REQUIRE(result.source_files.size() == 2);
    CHECK(result.source_files[0] == (base / "rtl/top.sv").lexically_normal().string());
    CHECK(result.source_files[1] == (base / "rtl/core.sv").lexically_normal().string());
}

TEST_CASE("Filelist parser: +incdir+ single path", "[filelist]") {
    auto base = fs::temp_directory_path();
    auto result = FilelistParser::parseString(
        "+incdir+rtl/include\n",
        base);

    REQUIRE(result.include_dirs.size() == 1);
    CHECK(result.include_dirs[0] == (base / "rtl/include").lexically_normal().string());
}

TEST_CASE("Filelist parser: +incdir+ multiple paths", "[filelist]") {
    auto base = fs::temp_directory_path();
    auto result = FilelistParser::parseString(
        "+incdir+rtl/include+rtl/common\n",
        base);

    REQUIRE(result.include_dirs.size() == 2);
    CHECK(result.include_dirs[0] == (base / "rtl/include").lexically_normal().string());
    CHECK(result.include_dirs[1] == (base / "rtl/common").lexically_normal().string());
}

TEST_CASE("Filelist parser: +define+ without value", "[filelist]") {
    auto result = FilelistParser::parseString(
        "+define+SYNTHESIS\n");

    REQUIRE(result.defines.size() == 1);
    CHECK(result.defines[0] == "SYNTHESIS");
}

TEST_CASE("Filelist parser: +define+ with value", "[filelist]") {
    auto result = FilelistParser::parseString(
        "+define+DATA_WIDTH=32\n");

    REQUIRE(result.defines.size() == 1);
    CHECK(result.defines[0] == "DATA_WIDTH=32");
}

TEST_CASE("Filelist parser: comments and blank lines ignored", "[filelist]") {
    auto base = fs::temp_directory_path();
    auto result = FilelistParser::parseString(
        "// This is a comment\n"
        "# This is also a comment\n"
        "\n"
        "   \n"
        "rtl/top.sv\n"
        "// Another comment\n",
        base);

    REQUIRE(result.source_files.size() == 1);
    CHECK(result.source_files[0] == (base / "rtl/top.sv").lexically_normal().string());
}

TEST_CASE("Filelist parser: inline comments stripped", "[filelist]") {
    auto base = fs::temp_directory_path();
    auto result = FilelistParser::parseString(
        "rtl/top.sv // main top module\n",
        base);

    REQUIRE(result.source_files.size() == 1);
    CHECK(result.source_files[0] == (base / "rtl/top.sv").lexically_normal().string());
}

TEST_CASE("Filelist parser: -y library directory", "[filelist]") {
    auto base = fs::temp_directory_path();
    auto result = FilelistParser::parseString(
        "-y lib/\n",
        base);

    REQUIRE(result.library_dirs.size() == 1);
    CHECK(result.library_dirs[0] == (base / "lib/").lexically_normal().string());
}

TEST_CASE("Filelist parser: -v library file", "[filelist]") {
    auto base = fs::temp_directory_path();
    auto result = FilelistParser::parseString(
        "-v lib/cells.v\n",
        base);

    REQUIRE(result.library_files.size() == 1);
    CHECK(result.library_files[0] == (base / "lib/cells.v").lexically_normal().string());
}

TEST_CASE("Filelist parser: +libext+ parsing", "[filelist]") {
    auto result = FilelistParser::parseString(
        "+libext+.v+.sv\n");

    REQUIRE(result.lib_extensions.size() == 2);
    CHECK(result.lib_extensions[0] == ".v");
    CHECK(result.lib_extensions[1] == ".sv");
}

TEST_CASE("Filelist parser: recursive -f", "[filelist]") {
    // Create inner filelist
    auto inner = writeTempFile("inner_file.sv\n");
    // Create outer filelist referencing inner via -f (CWD-relative)
    auto rel_inner = fs::relative(inner, fs::current_path());
    auto outer = writeTempFile("-f " + rel_inner.string() + "\nouter_file.sv\n");

    auto result = FilelistParser::parse(outer);
    fs::remove(inner);
    fs::remove(outer);

    // inner_file.sv resolved relative to CWD, outer_file.sv relative to outer's dir
    REQUIRE(result.source_files.size() == 2);

    bool has_inner = false, has_outer = false;
    for (auto& f : result.source_files) {
        if (f.find("inner_file.sv") != std::string::npos) has_inner = true;
        if (f.find("outer_file.sv") != std::string::npos) has_outer = true;
    }
    CHECK(has_inner);
    CHECK(has_outer);
}

TEST_CASE("Filelist parser: -F relative to filelist location", "[filelist]") {
    // Create a subdirectory for inner filelist
    auto sub_dir = fs::temp_directory_path() / "filelist_test_sub";
    fs::create_directories(sub_dir);

    auto inner = sub_dir / "inner.f";
    std::ofstream(inner) << "sub_module.sv\n";

    // Outer filelist uses -F with path relative to outer's location
    auto outer = fs::temp_directory_path() / "filelist_test_outer.f";
    std::ofstream(outer) << "-F filelist_test_sub/inner.f\n";

    auto result = FilelistParser::parse(outer);
    fs::remove(inner);
    fs::remove(outer);
    fs::remove(sub_dir);

    // sub_module.sv should be resolved relative to inner.f's directory
    REQUIRE(result.source_files.size() == 1);
    CHECK(result.source_files[0] == (sub_dir / "sub_module.sv").lexically_normal().string());
}

TEST_CASE("Filelist parser: non-existent filelist returns empty", "[filelist]") {
    auto result = FilelistParser::parse("/nonexistent/path/to/file.f");

    CHECK(result.source_files.empty());
    CHECK(result.include_dirs.empty());
    CHECK(result.defines.empty());
}

TEST_CASE("Filelist parser: recursion depth limit", "[filelist]") {
    // Create a filelist that references itself (circular)
    auto path = fs::temp_directory_path() / "filelist_circular.f";
    auto rel_path = fs::relative(path, fs::current_path());
    std::ofstream(path) << "-f " + rel_path.string() + "\nsome_file.sv\n";

    // Should not hang — depth limit prevents infinite recursion
    auto result = FilelistParser::parse(path);
    fs::remove(path);

    // some_file.sv should appear at least once (from first parse level)
    bool has_file = false;
    for (auto& f : result.source_files) {
        if (f.find("some_file.sv") != std::string::npos) has_file = true;
    }
    CHECK(has_file);
    // Should not have exploded in size
    CHECK(result.source_files.size() <= 20);
}

TEST_CASE("Filelist parser: mixed content", "[filelist]") {
    auto base = fs::temp_directory_path();
    auto result = FilelistParser::parseString(
        "// Project filelist\n"
        "rtl/top.sv\n"
        "rtl/core.sv\n"
        "\n"
        "+incdir+rtl/include\n"
        "+define+SYNTHESIS\n"
        "+define+DATA_WIDTH=32\n"
        "-y lib/\n"
        "-v lib/cells.v\n"
        "+libext+.v+.sv\n",
        base);

    CHECK(result.source_files.size() == 2);
    CHECK(result.include_dirs.size() == 1);
    CHECK(result.defines.size() == 2);
    CHECK(result.library_dirs.size() == 1);
    CHECK(result.library_files.size() == 1);
    CHECK(result.lib_extensions.size() == 2);
}

TEST_CASE("Filelist parser: parse from file", "[filelist]") {
    auto path = writeTempFile(
        "rtl/top.sv\n"
        "+incdir+rtl/include\n"
        "+define+SYNTHESIS\n");

    auto result = FilelistParser::parse(path);
    auto base = fs::absolute(path).parent_path();
    fs::remove(path);

    REQUIRE(result.source_files.size() == 1);
    CHECK(result.source_files[0] == (base / "rtl/top.sv").lexically_normal().string());
    REQUIRE(result.include_dirs.size() == 1);
    CHECK(result.include_dirs[0] == (base / "rtl/include").lexically_normal().string());
    REQUIRE(result.defines.size() == 1);
    CHECK(result.defines[0] == "SYNTHESIS");
}
