#include <catch2/catch_test_macros.hpp>
#include "TraceEngine.h"

using namespace connect;
using slang::ast::ArgumentDirection;

static PortInfo makePort(const std::string& inst, const std::string& name,
                         ArgumentDirection dir, uint32_t width = 1) {
    PortInfo p;
    p.instancePath = inst;
    p.portName = name;
    p.direction = dir;
    p.width = width;
    return p;
}

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
