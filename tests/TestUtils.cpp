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
    auto driver = std::make_unique<slang::driver::Driver>();
    driver->addStandardArgs();

    auto resolvedPath = resolveSvFixturePath(path).string();
    std::vector<const char*> args = {"test", resolvedPath.c_str()};
    if (!driver->parseCommandLine(static_cast<int>(args.size()), args.data()))
        return {};
    if (!driver->processOptions())
        return {};
    if (!driver->parseAllSources())
        return {};

    auto compilation = driver->createCompilation();
    return {std::move(driver), std::move(compilation)};
}

} // namespace testutils
