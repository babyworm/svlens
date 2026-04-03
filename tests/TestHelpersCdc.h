#pragma once

#include "CompilationSession.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace testutils::cdc {

struct InlineCompileResult {
    std::unique_ptr<connect::CompilationSession> session;
    slang::ast::Compilation* compilation = nullptr;
    explicit operator bool() const { return compilation != nullptr; }
};

inline InlineCompileResult compileInlineSV(const std::string& svCode,
                                           const std::string& prefix = "cdc_test") {
    static int counter = 0;
    auto path = std::filesystem::temp_directory_path() /
                (prefix + "_" + std::to_string(counter++) + ".sv");
    std::ofstream(path) << svCode;

    auto session = std::make_unique<connect::CompilationSession>();
    std::vector<std::string> args = {"test", path.string()};
    if (!session->compile(args)) {
        std::filesystem::remove(path);
        return {};
    }

    auto* compilation = &session->compilation();
    std::filesystem::remove(path);
    return {std::move(session), compilation};
}

} // namespace testutils::cdc
