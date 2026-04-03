#include <catch2/catch_test_macros.hpp>
#include "sv-cdccheck/filelist_parser.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace sv_cdccheck;

TEST_CASE("CDC FilelistParser: parses sources include dirs and defines", "[cdc][filelist]") {
    auto base = fs::temp_directory_path();
    auto result = FilelistParser::parseString(
        "rtl/top.sv\n"
        "+incdir+rtl/include+rtl/common\n"
        "+define+SYNTHESIS\n"
        "+define+DATA_WIDTH=32\n",
        base);

    REQUIRE(result.source_files.size() == 1);
    CHECK(result.source_files[0] == (base / "rtl/top.sv").lexically_normal().string());
    REQUIRE(result.include_dirs.size() == 2);
    CHECK(result.include_dirs[0] == (base / "rtl/include").lexically_normal().string());
    CHECK(result.include_dirs[1] == (base / "rtl/common").lexically_normal().string());
    REQUIRE(result.defines.size() == 2);
    CHECK(result.defines[0] == "SYNTHESIS");
    CHECK(result.defines[1] == "DATA_WIDTH=32");
}

TEST_CASE("CDC FilelistParser: recursive -F resolves relative to filelist location", "[cdc][filelist]") {
    const auto root = fs::temp_directory_path() / "svlens_filelist_test";
    fs::remove_all(root);
    fs::create_directories(root / "sub");

    const auto inner = root / "sub" / "inner.f";
    {
        std::ofstream ofs(inner);
        REQUIRE(ofs.good());
        ofs << "sub_module.sv\n";
    }

    const auto outer = root / "outer.f";
    {
        std::ofstream ofs(outer);
        REQUIRE(ofs.good());
        ofs << "-F sub/inner.f\n";
    }

    auto result = FilelistParser::parse(outer);
    REQUIRE(result.source_files.size() == 1);
    CHECK(result.source_files[0] == (root / "sub" / "sub_module.sv").lexically_normal().string());

    fs::remove_all(root);
}

TEST_CASE("CDC FilelistParser: missing filelist returns empty result", "[cdc][filelist]") {
    auto result = FilelistParser::parse("/nonexistent/path/to/file.f");
    CHECK(result.source_files.empty());
    CHECK(result.include_dirs.empty());
    CHECK(result.defines.empty());
}
