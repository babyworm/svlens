#pragma once

#include "CompilationSession.h"

#include <slang/ast/Compilation.h>

#include <filesystem>
#include <memory>
#include <string>

namespace testutils {

struct CompileResult {
    std::unique_ptr<connect::CompilationSession> session;
    slang::ast::Compilation* compilation = nullptr;
    explicit operator bool() const { return compilation != nullptr; }
};

std::filesystem::path resolveSvFixturePath(const std::filesystem::path& requestedPath);
CompileResult compileFile(const std::string& path);

} // namespace testutils
