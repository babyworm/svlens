#pragma once

#include <slang/ast/Compilation.h>
#include <slang/driver/Driver.h>

#include <filesystem>
#include <memory>
#include <string>

namespace testutils {

struct CompileResult {
    std::unique_ptr<slang::driver::Driver> driver;
    std::unique_ptr<slang::ast::Compilation> compilation;
    explicit operator bool() const { return compilation != nullptr; }
};

std::filesystem::path resolveSvFixturePath(const std::filesystem::path& requestedPath);
CompileResult compileFile(const std::string& path);

} // namespace testutils
