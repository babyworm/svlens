#include <catch2/catch_test_macros.hpp>
#include "CommonCli.h"

#include <unordered_set>

TEST_CASE("CommonCli: optionTakesValue recognizes configured value options") {
    const std::unordered_set<std::string> opts = {"--format", "--top"};
    CHECK(commoncli::optionTakesValue("--format", opts));
    CHECK(commoncli::optionTakesValue("--top", opts));
    CHECK_FALSE(commoncli::optionTakesValue("--strict", opts));
}

TEST_CASE("CommonCli: both-mode routing splits prefixed options and preserves common args") {
    const char* argv[] = {
        "svlens", "both",
        "--top", "soc_top",
        "--conn-format", "json",
        "--cdc-format", "md",
        "--output", "reports",
        "rtl/top.sv"
    };

    const std::unordered_set<std::string> connValue = {"--format", "--top"};
    const std::unordered_set<std::string> cdcValue = {"--format", "--top"};

    auto routed = commoncli::routeBothModeArgs(11, const_cast<char**>(argv), 2,
                                               connValue, cdcValue);

    CHECK(routed.explicitOutput);
    CHECK(routed.outputBase == "reports");

    CHECK(routed.connArgs[0] == "sv-conncheck");
    CHECK(routed.cdcArgs[0] == "sv-cdccheck");

    bool connHasJson = false;
    bool cdcHasMd = false;
    bool bothHaveTop = false;
    for (size_t i = 0; i + 1 < routed.connArgs.size(); ++i) {
        if (routed.connArgs[i] == "--format" && routed.connArgs[i + 1] == "json")
            connHasJson = true;
        if (routed.connArgs[i] == "--top" && routed.connArgs[i + 1] == "soc_top")
            bothHaveTop = true;
    }
    for (size_t i = 0; i + 1 < routed.cdcArgs.size(); ++i) {
        if (routed.cdcArgs[i] == "--format" && routed.cdcArgs[i + 1] == "md")
            cdcHasMd = true;
        if (routed.cdcArgs[i] == "--top" && routed.cdcArgs[i + 1] == "soc_top")
            bothHaveTop = bothHaveTop && true;
    }

    CHECK(connHasJson);
    CHECK(cdcHasMd);
    CHECK(bothHaveTop);
    CHECK(routed.connArgs.back() == "rtl/top.sv");
    CHECK(routed.cdcArgs.back() == "rtl/top.sv");
}
