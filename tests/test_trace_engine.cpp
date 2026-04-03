#include <catch2/catch_test_macros.hpp>
#include "TraceEngine.h"
#include "TestUtils.h"

#include <set>

using namespace connect;
using slang::ast::ArgumentDirection;
using testutils::makePort;

static ConnectionGraph makeTraceTestGraph() {
    ConnectionGraph graph;
    graph.topModule = "soc_top";

    // Hop 0: cpu.o_addr [32b] -> bus.i_addr [32b]
    PortInfo cpu_out = makePort("soc_top.u_cpu", "o_addr", ArgumentDirection::Out, 32);
    PortInfo bus_in  = makePort("soc_top.u_bus", "i_addr", ArgumentDirection::In, 32);

    // Hop 1: bus.o_mem_addr [24b] -> mem.i_addr [24b]
    PortInfo bus_out = makePort("soc_top.u_bus", "o_mem_addr", ArgumentDirection::Out, 24);
    PortInfo mem_in  = makePort("soc_top.u_mem", "i_addr", ArgumentDirection::In, 24);

    graph.connections.push_back({cpu_out, bus_in});
    graph.connections.push_back({bus_out, mem_in});
    graph.allPorts = {cpu_out, bus_in, bus_out, mem_in};
    return graph;
}

TEST_CASE("TraceEngine fan-out traces through chain", "[TraceEngine]") {
    auto graph = makeTraceTestGraph();
    TraceEngine engine(graph);

    auto hops = engine.traceFanOut("soc_top.u_cpu.o_addr");

    REQUIRE(hops.size() == 2);

    // Depth 0: cpu -> bus
    CHECK(hops[0].depth == 0);
    CHECK(hops[0].connection.source.fullPath() == "soc_top.u_cpu.o_addr");
    CHECK(hops[0].connection.dest.fullPath() == "soc_top.u_bus.i_addr");

    // Depth 1: bus -> mem
    CHECK(hops[1].depth == 1);
    CHECK(hops[1].connection.source.fullPath() == "soc_top.u_bus.o_mem_addr");
    CHECK(hops[1].connection.dest.fullPath() == "soc_top.u_mem.i_addr");
}

TEST_CASE("TraceEngine fan-in traces back to source", "[TraceEngine]") {
    auto graph = makeTraceTestGraph();
    TraceEngine engine(graph);

    auto hops = engine.traceFanIn("soc_top.u_mem.i_addr");

    REQUIRE(hops.size() == 2);

    // Depth 0: bus -> mem (the connection reaching mem)
    CHECK(hops[0].depth == 0);
    CHECK(hops[0].connection.dest.fullPath() == "soc_top.u_mem.i_addr");
    CHECK(hops[0].connection.source.fullPath() == "soc_top.u_bus.o_mem_addr");

    // Depth 1: cpu -> bus (the connection reaching bus)
    CHECK(hops[1].depth == 1);
    CHECK(hops[1].connection.dest.fullPath() == "soc_top.u_bus.i_addr");
    CHECK(hops[1].connection.source.fullPath() == "soc_top.u_cpu.o_addr");
}

TEST_CASE("TraceEngine glob pattern matching works", "[TraceEngine]") {
    auto graph = makeTraceTestGraph();
    TraceEngine engine(graph);

    // Wildcard prefix
    auto hops = engine.traceFanOut("*.u_cpu.o_addr");
    REQUIRE(hops.size() == 2);
    CHECK(hops[0].connection.source.fullPath() == "soc_top.u_cpu.o_addr");

    // Wildcard suffix — all ports of u_cpu (only o_addr is a source)
    auto hops2 = engine.traceFanOut("soc_top.u_cpu.*");
    REQUIRE(hops2.size() == 2);
    CHECK(hops2[0].connection.source.fullPath() == "soc_top.u_cpu.o_addr");
}

TEST_CASE("TraceEngine nonexistent signal returns empty", "[TraceEngine]") {
    auto graph = makeTraceTestGraph();
    TraceEngine engine(graph);

    auto hops = engine.traceFanOut("soc_top.u_nonexistent.o_data");
    CHECK(hops.empty());

    auto hops2 = engine.traceFanIn("soc_top.u_nonexistent.i_data");
    CHECK(hops2.empty());
}

TEST_CASE("TraceEngine maxDepth limits traversal", "[TraceEngine]") {
    auto graph = makeTraceTestGraph();
    TraceEngine engine(graph);

    // With maxDepth=0, only the initial matches (depth 0)
    auto hops = engine.traceFanOut("soc_top.u_cpu.o_addr", 0);
    REQUIRE(hops.size() == 1);
    CHECK(hops[0].depth == 0);
}

TEST_CASE("TraceEngine cycle detection prevents infinite loops", "[TraceEngine]") {
    ConnectionGraph graph;
    graph.topModule = "top";

    // Create a cycle: a -> b -> a
    PortInfo a_out = makePort("top.a", "o_data", ArgumentDirection::Out, 8);
    PortInfo b_in  = makePort("top.b", "i_data", ArgumentDirection::In, 8);
    PortInfo b_out = makePort("top.b", "o_data", ArgumentDirection::Out, 8);
    PortInfo a_in  = makePort("top.a", "i_data", ArgumentDirection::In, 8);

    graph.connections.push_back({a_out, b_in});
    graph.connections.push_back({b_out, a_in});
    graph.allPorts = {a_out, b_in, b_out, a_in};

    TraceEngine engine(graph);

    auto hops = engine.traceFanOut("top.a.o_data");
    // Should find a->b and b->a, but not loop forever
    REQUIRE(hops.size() == 2);
    CHECK(hops[0].connection.source.fullPath() == "top.a.o_data");
    CHECK(hops[1].connection.source.fullPath() == "top.b.o_data");
}

TEST_CASE("TraceEngine fan-out branching diamond graph", "[TraceEngine]") {
    ConnectionGraph graph;
    graph.topModule = "top";

    // A -> B
    PortInfo a_out = makePort("top.a", "o_data", ArgumentDirection::Out, 8);
    PortInfo b_in  = makePort("top.b", "i_data", ArgumentDirection::In, 8);
    // B -> C
    PortInfo b_out1 = makePort("top.b", "o_left", ArgumentDirection::Out, 8);
    PortInfo c_in   = makePort("top.c", "i_data", ArgumentDirection::In, 8);
    // B -> D
    PortInfo b_out2 = makePort("top.b", "o_right", ArgumentDirection::Out, 8);
    PortInfo d_in   = makePort("top.d", "i_data", ArgumentDirection::In, 8);

    graph.connections.push_back({a_out, b_in});
    graph.connections.push_back({b_out1, c_in});
    graph.connections.push_back({b_out2, d_in});
    graph.allPorts = {a_out, b_in, b_out1, c_in, b_out2, d_in};

    TraceEngine engine(graph);
    auto hops = engine.traceFanOut("top.a.o_data");

    REQUIRE(hops.size() == 3);

    // Depth 0: A -> B
    CHECK(hops[0].depth == 0);
    CHECK(hops[0].connection.source.fullPath() == "top.a.o_data");
    CHECK(hops[0].connection.dest.fullPath() == "top.b.i_data");

    // Depth 1: B -> C and B -> D (order may vary, check both present)
    CHECK(hops[1].depth == 1);
    CHECK(hops[2].depth == 1);

    std::set<std::string> depth1Dests;
    depth1Dests.insert(hops[1].connection.dest.fullPath());
    depth1Dests.insert(hops[2].connection.dest.fullPath());
    CHECK(depth1Dests.count("top.c.i_data") == 1);
    CHECK(depth1Dests.count("top.d.i_data") == 1);
}

TEST_CASE("TraceEngine formatTrace fan-in direction", "[TraceEngine]") {
    auto graph = makeTraceTestGraph();
    TraceEngine engine(graph);

    auto hops = engine.traceFanIn("soc_top.u_mem.i_addr");
    std::string output = TraceEngine::formatTrace(hops, "soc_top.u_mem.i_addr", false);

    CHECK(output.find("Fan-In Trace") != std::string::npos);
    CHECK(output.find("<-") != std::string::npos);
    // Fan-in should NOT use "->" arrows
    // (the "->" substring can appear in width mismatch annotations like "32b→16b",
    //  but the plain ASCII " -> " with spaces should not)
    CHECK(output.find(" -> ") == std::string::npos);
}

TEST_CASE("TraceEngine formatTrace empty hops", "[TraceEngine]") {
    std::vector<TraceHop> empty;
    std::string output = TraceEngine::formatTrace(empty, "nonexistent", true);

    CHECK(output.find("no connections found") != std::string::npos);
}

TEST_CASE("TraceEngine formatTrace width mismatch annotation", "[TraceEngine]") {
    // Create a single hop where source width != dest width
    ConnectionGraph graph;
    graph.topModule = "top";

    PortInfo src = makePort("top.a", "o_data", ArgumentDirection::Out, 32);
    PortInfo dst = makePort("top.b", "i_data", ArgumentDirection::In, 16);

    graph.connections.push_back({src, dst});
    graph.allPorts = {src, dst};

    TraceEngine engine(graph);
    auto hops = engine.traceFanOut("top.a.o_data");
    REQUIRE(hops.size() == 1);

    std::string output = TraceEngine::formatTrace(hops, "top.a.o_data", true);

    // Warning symbol (UTF-8 for U+26A0)
    CHECK(output.find("\xe2\x9a\xa0") != std::string::npos);
    // Width info
    CHECK(output.find("32b") != std::string::npos);
    CHECK(output.find("16b") != std::string::npos);
}

TEST_CASE("TraceEngine glob wildcard matches all output ports", "[TraceEngine]") {
    ConnectionGraph graph;
    graph.topModule = "soc_top";

    // cpu has two output ports going to different destinations
    PortInfo cpu_out1 = makePort("soc_top.u_cpu", "o_addr", ArgumentDirection::Out, 32);
    PortInfo bus_in1  = makePort("soc_top.u_bus", "i_addr", ArgumentDirection::In, 32);
    PortInfo cpu_out2 = makePort("soc_top.u_cpu", "o_data", ArgumentDirection::Out, 16);
    PortInfo mem_in   = makePort("soc_top.u_mem", "i_data", ArgumentDirection::In, 16);

    graph.connections.push_back({cpu_out1, bus_in1});
    graph.connections.push_back({cpu_out2, mem_in});
    graph.allPorts = {cpu_out1, bus_in1, cpu_out2, mem_in};

    TraceEngine engine(graph);
    auto hops = engine.traceFanOut("soc_top.u_cpu.*");

    // Should match both output ports of u_cpu as seeds
    REQUIRE(hops.size() >= 2);

    std::set<std::string> sources;
    for (const auto& h : hops) {
        if (h.depth == 0) sources.insert(h.connection.source.fullPath());
    }
    CHECK(sources.count("soc_top.u_cpu.o_addr") == 1);
    CHECK(sources.count("soc_top.u_cpu.o_data") == 1);
}

TEST_CASE("TraceEngine formatTrace produces correct output", "[TraceEngine]") {
    auto graph = makeTraceTestGraph();
    TraceEngine engine(graph);

    auto hops = engine.traceFanOut("soc_top.u_cpu.o_addr");
    std::string output = TraceEngine::formatTrace(hops, "soc_top.u_cpu.o_addr", true);

    // Check header
    CHECK(output.find("Fan-Out Trace") != std::string::npos);
    CHECK(output.find("soc_top.u_cpu.o_addr") != std::string::npos);

    // Check arrow direction (fan-out uses ->)
    CHECK(output.find("->") != std::string::npos);

    // Check width mismatch warning (32b source through 24b dest chain)
    CHECK(output.find("32b") != std::string::npos);
    CHECK(output.find("24b") != std::string::npos);
}
