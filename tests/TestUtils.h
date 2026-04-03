#pragma once

#include "CompilationSession.h"
#include "ConnectionGraph.h"

#include <slang/ast/Compilation.h>

#include <filesystem>
#include <memory>
#include <string>

namespace testutils {

inline connect::PortInfo makePort(const std::string& inst,
                                  const std::string& port,
                                  slang::ast::ArgumentDirection dir,
                                  uint32_t width = 1,
                                  bool isSigned = false) {
    connect::PortInfo p;
    p.instancePath = inst;
    p.portName = port;
    p.direction = dir;
    p.width = width;
    p.isSigned = isSigned;
    return p;
}

struct CompileResult {
    std::unique_ptr<connect::CompilationSession> session;
    slang::ast::Compilation* compilation = nullptr;
    explicit operator bool() const { return compilation != nullptr; }
};

std::filesystem::path resolveSvFixturePath(const std::filesystem::path& requestedPath);
CompileResult compileFile(const std::string& path);

} // namespace testutils
