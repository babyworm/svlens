#include "TestUtils.h"

#include <vector>

namespace testutils {

std::filesystem::path resolveSvFixturePath(const std::filesystem::path& requestedPath) {
    namespace fs = std::filesystem;

    if (requestedPath.is_absolute())
        return requestedPath;

    const std::string generic = requestedPath.generic_string();
    if (generic == "sv" || generic.rfind("sv/", 0) == 0) {
        return fs::path(TEST_SV_DIR) / requestedPath.lexically_relative("sv");
    }

    return requestedPath;
}

CompileResult compileFile(const std::string& path) {
    auto session = std::make_unique<connect::CompilationSession>();
    auto resolvedPath = resolveSvFixturePath(path).string();
    std::vector<std::string> args = {"test", resolvedPath};
    if (!session->compile(args))
        return {};
    auto* compilation = &session->compilation();
    return {std::move(session), compilation};
}

} // namespace testutils
