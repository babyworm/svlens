#include "CompilationSession.h"

#include "sv-cdccheck/filelist_parser.h"

#include <slang/ast/symbols/CompilationUnitSymbols.h>

#include <stdexcept>

namespace svlens {

namespace {

std::vector<std::string> expandFilelists(const std::vector<std::string>& args) {
    std::vector<std::string> expanded;
    if (args.empty())
        return expanded;

    expanded.push_back(args.front());
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if ((arg == "-f" || arg == "-F") && i + 1 < args.size()) {
            auto fl = sv_cdccheck::FilelistParser::parse(args[++i]);
            for (auto& src : fl.source_files)
                expanded.push_back(std::move(src));
            for (auto& inc : fl.include_dirs) {
                expanded.push_back("-I");
                expanded.push_back(std::move(inc));
            }
            for (auto& def : fl.defines) {
                expanded.push_back("-D");
                expanded.push_back(std::move(def));
            }
            for (auto& dir : fl.library_dirs) {
                expanded.push_back("-y");
                expanded.push_back(std::move(dir));
            }
            for (auto& lib : fl.library_files)
                expanded.push_back(std::move(lib));
            if (!fl.lib_extensions.empty()) {
                std::string libext = "+libext+";
                for (size_t extIdx = 0; extIdx < fl.lib_extensions.size(); ++extIdx) {
                    libext += fl.lib_extensions[extIdx];
                    if (extIdx + 1 < fl.lib_extensions.size())
                        libext += "+";
                }
                expanded.push_back(std::move(libext));
            }
            continue;
        }

        expanded.push_back(arg);
    }

    return expanded;
}

} // namespace

bool CompilationSession::fail(std::string_view message, std::string* errorMessage) {
    if (errorMessage)
        *errorMessage = std::string(message);
    return false;
}

bool CompilationSession::compile(const std::vector<std::string>& args,
                                 std::string* errorMessage) {
    driver_ = std::make_unique<slang::driver::Driver>();
    compilation_.reset();
    expandedArgs_.clear();
    driver_->addStandardArgs();

    expandedArgs_ = expandFilelists(args);
    std::vector<const char*> argv;
    argv.reserve(expandedArgs_.size());
    for (const auto& arg : expandedArgs_)
        argv.push_back(arg.c_str());

    if (!driver_->parseCommandLine(static_cast<int>(argv.size()), argv.data()))
        return fail("failed to parse command line", errorMessage);

    if (!driver_->processOptions())
        return fail("failed to process options", errorMessage);

    if (!driver_->parseAllSources())
        return fail("failed to parse sources", errorMessage);

    compilation_ = driver_->createCompilation();
    if (!compilation_)
        return fail("failed to create compilation", errorMessage);

    if (errorMessage)
        errorMessage->clear();
    return true;
}

slang::ast::Compilation& CompilationSession::compilation() {
    if (!compilation_)
        throw std::logic_error("CompilationSession::compilation called before compile()");
    return *compilation_;
}

const slang::ast::Compilation& CompilationSession::compilation() const {
    if (!compilation_)
        throw std::logic_error("CompilationSession::compilation called before compile()");
    return *compilation_;
}

const slang::ast::InstanceSymbol* CompilationSession::findTopInstance(
    std::string_view topName) const {
    if (!compilation_)
        return nullptr;

    const auto& root = compilation_->getRoot();
    for (auto* inst : root.topInstances) {
        if (inst && inst->name == topName)
            return inst;
    }
    return nullptr;
}

} // namespace svlens
