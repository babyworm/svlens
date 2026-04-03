#include <catch2/catch_test_macros.hpp>
#include "CompilationSession.h"
#include "TestUtils.h"

#include <filesystem>
#include <fstream>

using namespace connect;

TEST_CASE("CompilationSession: elaborates a valid fixture and finds top") {
    CompilationSession session;
    std::vector<std::string> args = {
        "test",
        testutils::resolveSvFixturePath("sv/clean_design.sv").string()
    };

    std::string error;
    REQUIRE(session.compile(args, &error));
    CHECK(error.empty());

    auto* top = session.findTopInstance("clean_top");
    REQUIRE(top != nullptr);
    CHECK(std::string(top->name) == "clean_top");
}

TEST_CASE("CompilationSession: top lookup returns null for missing top") {
    CompilationSession session;
    std::vector<std::string> args = {
        "test",
        testutils::resolveSvFixturePath("sv/clean_design.sv").string()
    };

    REQUIRE(session.compile(args));
    CHECK(session.findTopInstance("does_not_exist") == nullptr);
}

TEST_CASE("CompilationSession: forwards slang args such as defines") {
    CompilationSession session;
    std::vector<std::string> args = {
        "test",
        "-DSELECT_REAL_TOP",
        testutils::resolveSvFixturePath("sv/conditional_top.sv").string()
    };

    std::string error;
    REQUIRE(session.compile(args, &error));
    CHECK(session.findTopInstance("real_top") != nullptr);
    CHECK(session.findTopInstance("fallback_top") == nullptr);
}

TEST_CASE("CompilationSession: reports failure on missing source") {
    CompilationSession session;
    std::vector<std::string> args = {"test", "sv/definitely_missing_file.sv"};

    std::string error;
    CHECK_FALSE(session.compile(args, &error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("CompilationSession: filelist expansion preserves library metadata", "[compilation]") {
    namespace fs = std::filesystem;

    const auto root = fs::temp_directory_path() / "svlens_compilation_filelist";
    fs::remove_all(root);
    fs::create_directories(root / "rtl");
    fs::create_directories(root / "lib");

    {
        std::ofstream sv(root / "rtl" / "top.sv");
        REQUIRE(sv.good());
        sv << "module top; endmodule\n";
    }

    const auto filelist = root / "design.f";
    {
        std::ofstream fl(filelist);
        REQUIRE(fl.good());
        fl << "rtl/top.sv\n";
        fl << "-y lib\n";
        fl << "+libext+.v+.sv\n";
    }

    CompilationSession session;
    std::vector<std::string> args = {"test", "-F", filelist.string()};
    REQUIRE(session.compile(args));

    const auto& expanded = session.expandedArgs();
    bool foundLibDir = false;
    bool foundLibExt = false;
    for (size_t i = 0; i < expanded.size(); ++i) {
        if (expanded[i] == "-y" && i + 1 < expanded.size() &&
            expanded[i + 1] == (root / "lib").lexically_normal().string()) {
            foundLibDir = true;
        }
        if (expanded[i] == "+libext+.v+.sv")
            foundLibExt = true;
    }

    CHECK(foundLibDir);
    CHECK(foundLibExt);

    fs::remove_all(root);
}

TEST_CASE("CompilationSession: expands filelist library dirs and libexts into args") {
    namespace fs = std::filesystem;

    const auto root = fs::temp_directory_path() / "svlens_compilation_session_filelist";
    fs::remove_all(root);
    fs::create_directories(root / "lib");

    const auto filelist = root / "test.f";
    {
        std::ofstream ofs(filelist);
        REQUIRE(ofs.good());
        ofs << "-y lib\n";
        ofs << "+libext+.sv+.v\n";
        ofs << "top.sv\n";
    }
    {
        std::ofstream ofs(root / "top.sv");
        REQUIRE(ofs.good());
        ofs << "module top; endmodule\n";
    }

    CompilationSession session;
    std::vector<std::string> args = {"test", "-F", filelist.string()};
    REQUIRE(session.compile(args));

    const auto& expanded = session.expandedArgs();
    bool hasLibraryDir = false;
    bool hasLibext = false;
    for (size_t i = 0; i < expanded.size(); ++i) {
        if (expanded[i] == "-y" && i + 1 < expanded.size() &&
            expanded[i + 1] == (root / "lib").lexically_normal().string()) {
            hasLibraryDir = true;
        }
        if (expanded[i] == "+libext+.sv+.v")
            hasLibext = true;
    }

    CHECK(hasLibraryDir);
    CHECK(hasLibext);

    fs::remove_all(root);
}
