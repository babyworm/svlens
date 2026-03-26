#include <catch2/catch_test_macros.hpp>
#include "ConnectionExtractor.h"
#include "slang/driver/Driver.h"

using namespace connect;

// Holds both the Driver and Compilation so source text stays alive.
// The Driver owns the SourceManager whose buffers back all string_views
// in the AST (symbol names, etc.), so it must outlive the Compilation.
struct CompileResult {
    std::unique_ptr<slang::driver::Driver> driver;
    std::unique_ptr<slang::ast::Compilation> compilation;
    explicit operator bool() const { return compilation != nullptr; }
};

// Helper to compile a single SV file.
// slang auto-detects top modules (modules not instantiated by others).
static CompileResult compileFile(const std::string& path) {
    auto driver = std::make_unique<slang::driver::Driver>();
    driver->addStandardArgs();
    std::vector<const char*> args = {"test", path.c_str()};
    if (!driver->parseCommandLine(static_cast<int>(args.size()), args.data()))
        return {};
    if (!driver->processOptions())
        return {};
    if (!driver->parseAllSources())
        return {};
    auto compilation = driver->createCompilation();
    return {std::move(driver), std::move(compilation)};
}

TEST_CASE("Extractor: clean design has connections and all ports") {
    auto result = compileFile("sv/clean_design.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "clean_top");
    auto graph = extractor.extract();
    CHECK(graph.topModule == "clean_top");
    CHECK(graph.connections.size() >= 2);  // o_data->i_data, o_valid->i_valid
    CHECK(graph.allPorts.size() >= 4);     // 2 outputs + 2 inputs
}

TEST_CASE("Extractor: width mismatch captures port widths") {
    auto result = compileFile("sv/width_mismatch.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "width_mismatch_top");
    auto graph = extractor.extract();
    CHECK(!graph.allPorts.empty());
    bool found32 = false;
    for (auto& port : graph.allPorts) {
        if (port.portName == "o_data" && port.width == 32) found32 = true;
    }
    CHECK(found32);
}

TEST_CASE("Extractor: dangling output port is in allPorts") {
    auto result = compileFile("sv/dangling_output.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "dangling_top");
    auto graph = extractor.extract();
    bool foundDebug = false;
    for (auto& port : graph.allPorts) {
        if (port.portName == "o_debug") foundDebug = true;
    }
    CHECK(foundDebug);
}

TEST_CASE("Extractor: undriven input port is in allPorts") {
    auto result = compileFile("sv/undriven_input.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "undriven_top");
    auto graph = extractor.extract();
    bool foundConfig = false;
    for (auto& port : graph.allPorts) {
        if (port.portName == "i_config") foundConfig = true;
    }
    CHECK(foundConfig);
}
