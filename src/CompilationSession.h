#pragma once

#include <slang/ast/Compilation.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/driver/Driver.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace svlens {

class CompilationSession {
public:
    CompilationSession() = default;

    bool compile(const std::vector<std::string>& args,
                 std::string* errorMessage = nullptr);

    bool hasCompilation() const { return compilation_ != nullptr; }

    slang::ast::Compilation& compilation();
    const slang::ast::Compilation& compilation() const;
    const std::vector<std::string>& expandedArgs() const { return expandedArgs_; }

    const slang::ast::InstanceSymbol* findTopInstance(std::string_view topName) const;

private:
    static bool fail(std::string_view message, std::string* errorMessage);

    std::unique_ptr<slang::driver::Driver> driver_;
    std::unique_ptr<slang::ast::Compilation> compilation_;
    std::vector<std::string> expandedArgs_;
};

} // namespace svlens

namespace connect { using CompilationSession = svlens::CompilationSession; }
