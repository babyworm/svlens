#include "sv-cdccheck/connectivity.h"
#include "sv-cdccheck/ast_utils.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/TimingControl.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/Expression.h"
#include "slang/ast/Statement.h"

#include <algorithm>

namespace sv_cdccheck {

ConnectivityBuilder::ConnectivityBuilder(slang::ast::Compilation& compilation,
                                         const std::vector<std::unique_ptr<FFNode>>& ff_nodes)
    : compilation_(compilation), ff_nodes_(ff_nodes) {}

std::unordered_map<std::string, FFNode*> ConnectivityBuilder::buildFFOutputMap() const {
    std::unordered_map<std::string, FFNode*> map;
    for (auto& ff : ff_nodes_) {
        // Primary key: full hierarchical path (unique, no collisions)
        map[ff->hier_path] = ff.get();
    }
    return map;
}

// Extract signal name from a NamedValueExpression
static std::string extractExprSignalName(const slang::ast::Expression& expr) {
    if (expr.kind == slang::ast::ExpressionKind::NamedValue) {
        return std::string(expr.as<slang::ast::NamedValueExpression>().symbol.name);
    }
    return "";
}

// Build port-to-actual-signal map for an instance: port_name -> actual_signal_name
static std::unordered_map<std::string, std::string> buildPortMap(
    const slang::ast::InstanceSymbol& inst)
{
    std::unordered_map<std::string, std::string> port_map;
    for (auto* conn : inst.getPortConnections()) {
        if (!conn) continue;
        auto* expr = conn->getExpression();
        if (!expr) continue;
        std::string actual = extractExprSignalName(*expr);
        if (!actual.empty()) {
            port_map[std::string(conn->port.name)] = actual;
        }
    }
    return port_map;
}

// Extract signal name from an expression, handling Assignment expressions
// (slang models output port connections as assignments: wire = port_internal)
static std::string extractWireNameFromExpr(const slang::ast::Expression& expr) {
    // Simple named value
    if (expr.kind == slang::ast::ExpressionKind::NamedValue) {
        return std::string(expr.as<slang::ast::NamedValueExpression>().symbol.name);
    }
    // Output port: Assignment expression where LHS is the wire
    if (expr.kind == slang::ast::ExpressionKind::Assignment) {
        auto& assign = expr.as<slang::ast::AssignmentExpression>();
        if (assign.left().kind == slang::ast::ExpressionKind::NamedValue) {
            return std::string(
                assign.left().as<slang::ast::NamedValueExpression>().symbol.name);
        }
    }
    return "";
}

// Build a map: wire/signal name -> FFNode* for output ports of child instances
// E.g., if child u_a has output q connected to wire_ab, map wire_ab -> u_a.q FF
static void buildWireToFFMap(
    const slang::ast::InstanceSymbol& inst,
    const std::string& inst_path,
    const std::unordered_map<std::string, FFNode*>& output_map,
    std::unordered_map<std::string, FFNode*>& wire_map)
{
    for (auto& member : inst.body.members()) {
        if (member.kind != slang::ast::SymbolKind::Instance) continue;
        auto& child = member.as<slang::ast::InstanceSymbol>();
        std::string child_path = inst_path + "." + std::string(child.name);

        for (auto* conn : child.getPortConnections()) {
            if (!conn) continue;
            // Only care about output ports
            if (conn->port.kind != slang::ast::SymbolKind::Port) continue;
            auto& port_sym = conn->port.as<slang::ast::PortSymbol>();
            if (port_sym.direction != slang::ast::ArgumentDirection::Out) continue;
            auto* expr = conn->getExpression();
            if (!expr) continue;
            std::string wire_name = extractWireNameFromExpr(*expr);
            if (wire_name.empty()) continue;

            // Check if the port corresponds to an FF output
            std::string ff_path = child_path + "." + std::string(conn->port.name);
            auto it = output_map.find(ff_path);
            if (it != output_map.end()) {
                wire_map[wire_name] = it->second;
            }
        }
    }
}

// Find an FFNode by signal name within a given instance scope.
// Uses port_map to resolve port names to actual parent signals,
// and wire_map to resolve parent wires to FF outputs.
static FFNode* findFFByName(
    const std::string& sig_name,
    const std::string& inst_path,
    const std::unordered_map<std::string, FFNode*>& output_map,
    const std::unordered_map<std::string, std::string>& port_map,
    const std::unordered_map<std::string, FFNode*>& wire_map)
{
    // Try full scoped path first (same instance)
    std::string full_name = inst_path + "." + sig_name;
    auto it = output_map.find(full_name);
    if (it != output_map.end())
        return it->second;

    // Try just the signal name as a full path
    it = output_map.find(sig_name);
    if (it != output_map.end())
        return it->second;

    // Try direct wire_map lookup: sig_name is a local wire driven by a child FF output
    // (e.g., top-level always_ff reads wire_ab which is connected to u_a.q's output)
    auto wit = wire_map.find(sig_name);
    if (wit != wire_map.end())
        return wit->second;

    // Resolve through port connection: sig_name is a port, trace to actual signal
    auto pit = port_map.find(sig_name);
    if (pit != port_map.end()) {
        const std::string& actual = pit->second;

        // Check if the actual signal is a wire connected to a child FF output
        auto wit = wire_map.find(actual);
        if (wit != wire_map.end())
            return wit->second;

        // Try the actual signal in parent scope
        std::string parent_path;
        auto last_dot = inst_path.rfind('.');
        if (last_dot != std::string::npos) {
            parent_path = inst_path.substr(0, last_dot);
            std::string parent_full = parent_path + "." + actual;
            it = output_map.find(parent_full);
            if (it != output_map.end())
                return it->second;
        }
    }

    // Try matching by suffix in the same parent scope
    std::string parent_path;
    auto last_dot = inst_path.rfind('.');
    if (last_dot != std::string::npos) {
        parent_path = inst_path.substr(0, last_dot);
        for (auto& [path, ff] : output_map) {
            if (path.starts_with(parent_path + ".") &&
                path.ends_with("." + sig_name)) {
                return ff;
            }
        }
    }

    return nullptr;
}

// Collect continuous assign statements: wire_name -> RHS signal names
static void collectContinuousAssigns(
    const slang::ast::InstanceSymbol& inst,
    std::unordered_map<std::string, std::vector<std::string>>& cont_assigns)
{
    for (auto& member : inst.body.members()) {
        if (member.kind != slang::ast::SymbolKind::ContinuousAssign) continue;

        // ContinuousAssignSymbol has getAssignment() that returns an Assignment expression
        auto& ca = member.as<slang::ast::ContinuousAssignSymbol>();
        auto& assign_expr = ca.getAssignment().as<slang::ast::AssignmentExpression>();
        std::string lhs_name;
        if (assign_expr.left().kind == slang::ast::ExpressionKind::NamedValue) {
            lhs_name = std::string(
                assign_expr.left().as<slang::ast::NamedValueExpression>().symbol.name);
        }
        if (lhs_name.empty()) continue;

        std::vector<std::string> rhs_signals;
        collectReferencedSignals(assign_expr.right(), rhs_signals);
        cont_assigns[lhs_name] = std::move(rhs_signals);
    }
}

// Resolve a signal name through continuous assigns to find underlying FF sources.
// Returns all FF sources reachable through combinational logic.
// Sets has_comb to true if resolution went through a continuous assign.
static void resolveToFFs(
    const std::string& sig_name,
    const std::string& inst_path,
    const std::unordered_map<std::string, FFNode*>& output_map,
    const std::unordered_map<std::string, std::string>& port_map,
    const std::unordered_map<std::string, FFNode*>& wire_map,
    const std::unordered_map<std::string, std::vector<std::string>>& cont_assigns,
    std::vector<FFNode*>& result,
    bool& has_comb,
    int depth = 0)
{
    if (depth > 10) return; // prevent infinite recursion

    // Try direct FF lookup first
    FFNode* ff = findFFByName(sig_name, inst_path, output_map, port_map, wire_map);
    if (ff) {
        if (std::find(result.begin(), result.end(), ff) == result.end())
            result.push_back(ff);
        return;
    }

    // Try resolving through continuous assigns
    auto it = cont_assigns.find(sig_name);
    if (it != cont_assigns.end()) {
        has_comb = true;
        for (auto& rhs : it->second) {
            resolveToFFs(rhs, inst_path, output_map, port_map, wire_map,
                        cont_assigns, result, has_comb, depth + 1);
        }
    }
}

// Recursive: process an instance and its children for FF-to-FF edges
static void processInstanceEdges(
    const slang::ast::InstanceSymbol& inst,
    const std::string& inst_path,
    const std::unordered_map<std::string, FFNode*>& output_map,
    const std::unordered_map<std::string, FFNode*>& parent_wire_map,
    std::vector<FFEdge>& edges)
{
    // Build port map for this instance (port_name -> actual_signal in parent)
    auto port_map = buildPortMap(inst);

    // Build wire-to-FF map for wires in this instance's scope
    std::unordered_map<std::string, FFNode*> wire_map;
    buildWireToFFMap(inst, inst_path, output_map, wire_map);

    // Merge parent wire map for port resolution
    // (parent_wire_map is used when resolving port signals to wires in grandparent)
    std::unordered_map<std::string, FFNode*> combined_wire_map = parent_wire_map;
    for (auto& [k, v] : wire_map)
        combined_wire_map[k] = v;

    // Collect continuous assigns for combinational path resolution
    std::unordered_map<std::string, std::vector<std::string>> cont_assigns;
    collectContinuousAssigns(inst, cont_assigns);

    for (auto& body_member : inst.body.members()) {
        if (body_member.kind == slang::ast::SymbolKind::ProceduralBlock) {
            auto& block = body_member.as<slang::ast::ProceduralBlockSymbol>();
            if (block.procedureKind != slang::ast::ProceduralBlockKind::AlwaysFF &&
                block.procedureKind != slang::ast::ProceduralBlockKind::Always)
                continue;

            auto& body = block.getBody();
            std::vector<AssignInfo> assignments;
            collectAssignments(body, assignments);

            for (auto& assign : assignments) {
                FFNode* dest = findFFByName(assign.lhs_name, inst_path,
                                            output_map, port_map, combined_wire_map);
                if (!dest) continue;

                for (auto& rhs_sig : assign.rhs_signals) {
                    // First try direct FF lookup
                    FFNode* source = findFFByName(rhs_sig, inst_path,
                                                  output_map, port_map, combined_wire_map);

                    if (source && source != dest) {
                        FFEdge edge;
                        edge.source = source;
                        edge.dest = dest;
                        edge.comb_path = assign.rhs_signals;
                        edges.push_back(edge);
                    } else if (!source) {
                        // Try resolving through continuous assigns
                        std::vector<FFNode*> resolved;
                        bool has_comb = false;
                        resolveToFFs(rhs_sig, inst_path, output_map, port_map,
                                    combined_wire_map, cont_assigns, resolved,
                                    has_comb);
                        for (auto* src : resolved) {
                            if (src && src != dest) {
                                FFEdge edge;
                                edge.source = src;
                                edge.dest = dest;
                                edge.comb_path = assign.rhs_signals;
                                edge.has_comb_logic = has_comb;
                                edges.push_back(edge);
                            }
                        }
                    }
                }
            }
        }

        // Recurse into child instances
        if (body_member.kind == slang::ast::SymbolKind::Instance) {
            auto& child = body_member.as<slang::ast::InstanceSymbol>();
            std::string child_path = inst_path + "." + std::string(child.name);
            processInstanceEdges(child, child_path, output_map, wire_map, edges);
        }
    }
}

void ConnectivityBuilder::findFFtoFFEdges(
    const std::unordered_map<std::string, FFNode*>& output_map)
{
    auto& root = compilation_.getRoot();
    std::unordered_map<std::string, FFNode*> empty_wire_map;

    for (auto& member : root.members()) {
        if (member.kind != slang::ast::SymbolKind::Instance) continue;
        auto& inst = member.as<slang::ast::InstanceSymbol>();
        std::string inst_path(inst.name);
        processInstanceEdges(inst, inst_path, output_map, empty_wire_map, edges_);
    }
}

void ConnectivityBuilder::analyze() {
    auto output_map = buildFFOutputMap();
    findFFtoFFEdges(output_map);
}

const std::vector<FFEdge>& ConnectivityBuilder::getEdges() const {
    return edges_;
}

} // namespace sv_cdccheck
