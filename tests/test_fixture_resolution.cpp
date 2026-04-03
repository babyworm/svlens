#include <catch2/catch_test_macros.hpp>
#include "ConnectionExtractor.h"
#include "TestUtils.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

class ScopedCurrentPath {
public:
    explicit ScopedCurrentPath(const fs::path& next) : previous_(fs::current_path()) {
        fs::current_path(next);
    }

    ~ScopedCurrentPath() {
        fs::current_path(previous_);
    }

private:
    fs::path previous_;
};

} // namespace

TEST_CASE("TestUtils: sv fixture paths prefer source fixtures over cwd copies") {
    const fs::path tempRoot = fs::temp_directory_path() / "sv-conncheck-fixture-resolution";
    fs::remove_all(tempRoot);
    fs::create_directories(tempRoot / "sv");

    {
        std::ofstream ofs(tempRoot / "sv" / "member_access_and_concat.sv");
        REQUIRE(ofs.good());
        ofs << "module stale_fixture; endmodule\n";
    }

    ScopedCurrentPath scoped(tempRoot);
    const fs::path resolved = testutils::resolveSvFixturePath("sv/member_access_and_concat.sv");

    CHECK(resolved == fs::path(TEST_SV_DIR) / "member_access_and_concat.sv");

    fs::remove_all(tempRoot);
}

TEST_CASE("TestUtils: compileFile ignores stale cwd sv copies") {
    using namespace connect;

    const fs::path tempRoot = fs::temp_directory_path() / "sv-conncheck-compile-resolution";
    fs::remove_all(tempRoot);
    fs::create_directories(tempRoot / "sv");

    {
        std::ofstream ofs(tempRoot / "sv" / "member_access_and_concat.sv");
        REQUIRE(ofs.good());
        ofs << "module member_concat_top; endmodule\n";
    }

    ScopedCurrentPath scoped(tempRoot);
    auto result = testutils::compileFile("sv/member_access_and_concat.sv");
    REQUIRE(result);

    ConnectionExtractor extractor(*result.compilation, "member_concat_top");
    const auto graph = extractor.extract();

    CHECK(graph.connections.size() == 3);

    fs::remove_all(tempRoot);
}
