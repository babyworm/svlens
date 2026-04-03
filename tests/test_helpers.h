#pragma once
#include "slang/driver/Driver.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include <fstream>
#include <filesystem>
#include <memory>
#include <string>

namespace sv_cdccheck::test {
inline std::unique_ptr<slang::ast::Compilation> compileSV(const std::string& sv_code, const std::string& prefix = "test") {
    static int counter = 0;
    auto path = std::filesystem::temp_directory_path() / (prefix + "_" + std::to_string(counter++) + ".sv");
    std::ofstream(path) << sv_code;
    std::string path_str = path.string();
    slang::driver::Driver driver;
    driver.addStandardArgs();
    const char* args[] = {"test", path_str.c_str()};
    (void)driver.parseCommandLine(2, const_cast<char**>(args));
    (void)driver.processOptions();
    (void)driver.parseAllSources();
    auto compilation = driver.createCompilation();
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
    return compilation;
}
} // namespace sv_cdccheck::test
