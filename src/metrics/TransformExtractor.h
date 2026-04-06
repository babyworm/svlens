#pragma once

#include "TransformGraph.h"

#include <slang/ast/Compilation.h>
#include <slang/ast/Statement.h>
#include <slang/ast/statements/ConditionalStatements.h>
#include <slang/ast/statements/LoopStatements.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/ast/symbols/MemberSymbols.h>
#include <slang/ast/symbols/BlockSymbols.h>
#include <slang/text/SourceManager.h>

#include <string>

namespace metrics {

class TransformExtractor {
public:
    TransformExtractor(slang::ast::Compilation& compilation,
                       const std::string& topModule,
                       int maxForUnroll = 1024);

    TransformGraph extract();

private:
    void visitScope(const slang::ast::Scope& scope,
                    const std::string& scopePath);

    void processContinuousAssign(const slang::ast::ContinuousAssignSymbol& sym,
                                 const std::string& scopePath);

    void processProceduralBlock(const slang::ast::ProceduralBlockSymbol& block,
                                const std::string& scopePath);

    void processStatement(const slang::ast::Statement& stmt,
                          const std::string& scopePath,
                          bool approximate = false);

    void processCase(const slang::ast::CaseStatement& caseStmt,
                     const std::string& scopePath);

    void processForLoop(const slang::ast::ForLoopStatement& forStmt,
                        const std::string& scopePath);

    void detectFFs(const slang::ast::Statement& stmt,
                   const std::string& scopePath);

    // Decompose an expression into TransformNodes.
    // Returns a ValueRef representing where the expression result lives.
    ValueRef decomposeExpr(const slang::ast::Expression& expr,
                           const std::string& scopePath);

    ValueRef makeNamedRef(const slang::ast::Expression& expr,
                          const std::string& scopePath) const;

    std::string sourceLoc(const slang::ast::Expression& expr) const;

    slang::ast::Compilation& compilation_;
    std::string topModule_;
    TransformGraph graph_;
    uint32_t tempCounter_ = 0;
    int maxForUnroll_ = 1024;

    std::string nextTemp() { return "__t" + std::to_string(tempCounter_++); }
};

} // namespace metrics
