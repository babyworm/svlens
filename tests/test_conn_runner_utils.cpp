#include <catch2/catch_test_macros.hpp>
#include "ConnRunnerUtils.h"
#include "ConnCli.h"
#include "TestUtils.h"

using testutils::compileFile;

TEST_CASE("buildConnectionGraph returns graph for simple design") {
    auto result = compileFile("sv/cru_simple.sv");
    REQUIRE(result);

    connect::ConnCliOptions opts;
    opts.topModule = "cru_top";
    connect::ConnectionGraph graph;
    auto ok = connect::buildConnectionGraph(*result.compilation, opts, graph);
    CHECK(ok);
    CHECK(graph.allPorts.size() >= 2);
}

TEST_CASE("runConnCheckers returns issues for width mismatch") {
    auto result = compileFile("sv/cru_width.sv");
    REQUIRE(result);

    connect::ConnCliOptions opts;
    opts.topModule = "cru_wide_top";
    opts.checkWidth = true;
    connect::ConnectionGraph graph;
    connect::buildConnectionGraph(*result.compilation, opts, graph);

    auto issues = connect::runConnCheckers(opts, graph);
    bool has_width = false;
    for (auto& iss : issues) {
        if (iss.type == connect::Issue::Type::WIDTH_MISMATCH) has_width = true;
    }
    CHECK(has_width);
}
