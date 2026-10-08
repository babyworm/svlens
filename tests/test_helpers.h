#pragma once
#include "slang/driver/Driver.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include <atomic>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <memory>
#include <string>
#include <unistd.h>

namespace sv_cdccheck::test {
struct CompiledSV {
    // Compilation refers to source text and symbols owned by Driver. Keep the
    // driver alive until after Compilation has been destroyed.
    std::unique_ptr<slang::driver::Driver> driver;
    std::unique_ptr<slang::ast::Compilation> compilation;

    slang::ast::Compilation& operator*() { return *compilation; }
    const slang::ast::Compilation& operator*() const { return *compilation; }
    slang::ast::Compilation* operator->() { return compilation.get(); }
    const slang::ast::Compilation* operator->() const { return compilation.get(); }
    explicit operator bool() const { return compilation != nullptr; }
};

inline CompiledSV compileSVFile(const std::string& path) {
    auto driver = std::make_unique<slang::driver::Driver>();
    driver->addStandardArgs();
    const char* args[] = {"test", path.c_str()};
    (void)driver->parseCommandLine(2, const_cast<char**>(args));
    (void)driver->processOptions();
    (void)driver->parseAllSources();
    auto compilation = driver->createCompilation();
    auto& root = compilation->getRoot();
    for (auto& member : root.members()) {
        if (member.kind == slang::ast::SymbolKind::Instance) {
            auto& inst = member.as<slang::ast::InstanceSymbol>();
            for (auto& bm : inst.body.members()) {
                if (bm.kind == slang::ast::SymbolKind::ProceduralBlock)
                    (void)bm.as<slang::ast::ProceduralBlockSymbol>().getBody();
            }
        }
    }
    compilation->getAllDiagnostics();
    return {std::move(driver), std::move(compilation)};
}

inline CompiledSV compileSV(const std::string& sv_code, const std::string& prefix = "test") {
    static std::atomic<uint64_t> counter{0};
    const auto unique_id =
        std::to_string(static_cast<long long>(::getpid())) + "_" +
        std::to_string(static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count())) +
        "_" + std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
    auto path = std::filesystem::temp_directory_path() / (prefix + "_" + unique_id + ".sv");
    std::ofstream(path) << sv_code;
    return compileSVFile(path.string());
}
} // namespace sv_cdccheck::test
