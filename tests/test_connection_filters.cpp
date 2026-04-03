#include <catch2/catch_test_macros.hpp>
#include "ConnectionFilters.h"
#include "TestUtils.h"

using namespace connect;
using slang::ast::ArgumentDirection;
using testutils::makePort;

TEST_CASE("ConnectionFilters: detects common no-connect port names") {
    CHECK(isLikelyNoConnectPort("o_nc"));
    CHECK(isLikelyNoConnectPort("i_unused"));
    CHECK(isLikelyNoConnectPort("debug_unconnected"));
    CHECK_FALSE(isLikelyNoConnectPort("i_sync"));
    CHECK_FALSE(isLikelyNoConnectPort("o_data"));
}

TEST_CASE("ConnectionFilters: ignore-nc removes no-connect ports and their edges") {
    ConnectionGraph graph;
    graph.topModule = "top";

    auto src = makePort("top.u_src", "o_data", ArgumentDirection::Out, 8);
    auto dst = makePort("top.u_dst", "i_data", ArgumentDirection::In, 8);
    auto nc = makePort("top.u_dst", "i_nc", ArgumentDirection::In, 1);

    graph.allPorts = {src, dst, nc};
    graph.connectedPorts.insert(dst.fullPath());
    graph.connections.push_back({src, dst});
    graph.connections.push_back({src, nc});

    GraphFilterOptions options;
    options.ignoreNc = true;
    auto filtered = applyGraphFilters(graph, options);

    REQUIRE(filtered.allPorts.size() == 2);
    CHECK(filtered.connections.size() == 1);
    CHECK(filtered.connections.front().dest.fullPath() == dst.fullPath());
}

TEST_CASE("ConnectionFilters: ignore-tie-off removes constant-tied ports") {
    ConnectionGraph graph;
    graph.topModule = "top";

    auto data = makePort("top.u_dst", "i_data", ArgumentDirection::In, 8);
    auto tie = makePort("top.u_dst", "i_tie", ArgumentDirection::In, 1);

    graph.allPorts = {data, tie};
    graph.connectedPorts.insert(data.fullPath());
    graph.connectedPorts.insert(tie.fullPath());
    graph.tieOffPorts.insert(tie.fullPath());

    GraphFilterOptions options;
    options.ignoreTieOff = true;
    auto filtered = applyGraphFilters(graph, options);

    REQUIRE(filtered.allPorts.size() == 1);
    CHECK(filtered.allPorts.front().fullPath() == data.fullPath());
    CHECK(filtered.connectedPorts.contains(data.fullPath()));
    CHECK_FALSE(filtered.connectedPorts.contains(tie.fullPath()));
    CHECK(filtered.tieOffPorts.empty());
}
