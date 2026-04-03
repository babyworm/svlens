#include "sv-cdccheck/ff_classifier.h"
#include "sv-cdccheck/clock_tree.h"
#include "sv-cdccheck/ast_utils.h"

#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/ast/symbols/AttributeSymbol.h"
#include "slang/ast/TimingControl.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/Expression.h"
#include "slang/ast/Statement.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/SemanticFacts.h"

namespace sv_cdccheck {

FFClassifier::FFClassifier(slang::ast::Compilation& compilation,
                           ClockDatabase& clock_db)
    : compilation_(compilation), clock_db_(clock_db) {}

// Information extracted from one SignalEventControl
struct EventInfo {
    std::string signal_name;
    bool is_posedge = false;
    bool is_negedge = false;
};

// Parse a single SignalEventControl into EventInfo
static EventInfo parseSignalEvent(const slang::ast::SignalEventControl& sec) {
    EventInfo info;
    info.signal_name = extractSignalName(sec.expr);
    info.is_posedge = (sec.edge == slang::ast::EdgeKind::PosEdge);
    info.is_negedge = (sec.edge == slang::ast::EdgeKind::NegEdge);
    return info;
}

// Extract all signal events from a TimingControl (handles both single event and event list)
static std::vector<EventInfo> extractEvents(const slang::ast::TimingControl& timing) {
    std::vector<EventInfo> events;

    if (timing.kind == slang::ast::TimingControlKind::SignalEvent) {
        events.push_back(parseSignalEvent(timing.as<slang::ast::SignalEventControl>()));
    }
    else if (timing.kind == slang::ast::TimingControlKind::EventList) {
        auto& list = timing.as<slang::ast::EventListControl>();
        for (auto* ev : list.events) {
            if (ev && ev->kind == slang::ast::TimingControlKind::SignalEvent) {
                events.push_back(parseSignalEvent(ev->as<slang::ast::SignalEventControl>()));
            }
        }
    }
    return events;
}

// Classify events into clock and reset(s)
struct SensitivityInfo {
    std::string clock_name;
    Edge clock_edge = Edge::Posedge;
    std::string reset_name;
    bool reset_is_async = false;
    ResetSignal::Polarity reset_polarity = ResetSignal::Polarity::ActiveLow;
};

static SensitivityInfo classifyEvents(const std::vector<EventInfo>& events,
                                      bool* multi_clock_error = nullptr) {
    SensitivityInfo info;

    // Count clock-like signals in sensitivity list
    int clock_count = 0;
    for (auto& ev : events) {
        if (ClockTreeAnalyzer::isClockName(ev.signal_name) &&
            !ClockTreeAnalyzer::isResetName(ev.signal_name))
            clock_count++;
    }
    if (multi_clock_error)
        *multi_clock_error = (clock_count >= 2);

    // Heuristic: in always_ff @(posedge clk or negedge rst_n),
    // the clock is the posedge signal with a clock-like name,
    // the reset is the other signal.
    for (auto& ev : events) {
        bool looks_like_clock = ClockTreeAnalyzer::isClockName(ev.signal_name);
        bool looks_like_reset = ClockTreeAnalyzer::isResetName(ev.signal_name);

        if (looks_like_clock && !looks_like_reset) {
            if (info.clock_name.empty()) {
                info.clock_name = ev.signal_name;
                info.clock_edge = ev.is_posedge ? Edge::Posedge : Edge::Negedge;
            }
        }
        else if (looks_like_reset && !looks_like_clock) {
            info.reset_name = ev.signal_name;
            info.reset_is_async = true; // in sensitivity list = async reset
            info.reset_polarity = ev.is_negedge ?
                ResetSignal::Polarity::ActiveLow : ResetSignal::Polarity::ActiveHigh;
        }
    }

    // Fallback: if no clock found by name, use the first posedge signal
    if (info.clock_name.empty()) {
        for (auto& ev : events) {
            if (ev.is_posedge && !ClockTreeAnalyzer::isResetName(ev.signal_name)) {
                info.clock_name = ev.signal_name;
                info.clock_edge = Edge::Posedge;
                break;
            }
        }
    }

    // Fallback: if still no clock, use first event
    if (info.clock_name.empty() && !events.empty()) {
        info.clock_name = events[0].signal_name;
        info.clock_edge = events[0].is_posedge ? Edge::Posedge : Edge::Negedge;
    }

    return info;
}

// Per-variable assignment info for FF classification (LHS variable + RHS fanin)
struct FFAssignInfo {
    std::string lhs_name;
    std::vector<std::string> rhs_signals;
};

// Collect variable names assigned in a statement (the FF registers) with fanin info
static void collectAssignedVars(const slang::ast::Statement& stmt,
                                std::vector<std::string>& vars,
                                std::vector<FFAssignInfo>& assign_infos) {
    switch (stmt.kind) {
        case slang::ast::StatementKind::ExpressionStatement: {
            auto& exprStmt = stmt.as<slang::ast::ExpressionStatement>();
            auto& expr = exprStmt.expr;
            if (expr.kind == slang::ast::ExpressionKind::Assignment) {
                auto& assign = expr.as<slang::ast::AssignmentExpression>();
                std::string name = extractSignalName(assign.left());
                if (!name.empty()) {
                    // Avoid duplicates in vars list
                    if (std::find(vars.begin(), vars.end(), name) == vars.end())
                        vars.push_back(name);

                    // Collect RHS signals for fanin
                    FFAssignInfo info;
                    info.lhs_name = name;
                    collectReferencedSignals(assign.right(), info.rhs_signals);
                    assign_infos.push_back(std::move(info));
                }
            }
            break;
        }
        case slang::ast::StatementKind::Timed: {
            auto& timed = stmt.as<slang::ast::TimedStatement>();
            collectAssignedVars(timed.stmt, vars, assign_infos);
            break;
        }
        case slang::ast::StatementKind::Block: {
            auto& block = stmt.as<slang::ast::BlockStatement>();
            collectAssignedVars(block.body, vars, assign_infos);
            break;
        }
        case slang::ast::StatementKind::List: {
            auto& list = stmt.as<slang::ast::StatementList>();
            for (auto* child : list.list)
                if (child) collectAssignedVars(*child, vars, assign_infos);
            break;
        }
        case slang::ast::StatementKind::Conditional: {
            auto& cond = stmt.as<slang::ast::ConditionalStatement>();
            collectAssignedVars(cond.ifTrue, vars, assign_infos);
            if (cond.ifFalse)
                collectAssignedVars(*cond.ifFalse, vars, assign_infos);
            break;
        }
        default: break;
    }
}

// Forward declarations
static void processInstance(const slang::ast::InstanceSymbol& inst,
                            const std::string& prefix,
                            ClockDatabase& clock_db,
                            std::vector<std::unique_ptr<FFNode>>& ff_nodes,
                            std::vector<LatchWarning>& latch_warnings,
                            std::vector<FFClassificationError>& errors);

static void processMembers(const slang::ast::Scope& scope,
                           const std::string& inst_path,
                           ClockDatabase& clock_db,
                           std::vector<std::unique_ptr<FFNode>>& ff_nodes,
                           std::vector<LatchWarning>& latch_warnings,
                           std::vector<FFClassificationError>& errors);

// Walk an instance and extract FFs from always_ff blocks
static void processInstance(const slang::ast::InstanceSymbol& inst,
                            const std::string& prefix,
                            ClockDatabase& clock_db,
                            std::vector<std::unique_ptr<FFNode>>& ff_nodes,
                            std::vector<LatchWarning>& latch_warnings,
                            std::vector<FFClassificationError>& errors) {
    std::string inst_path = prefix.empty() ?
        std::string(inst.name) : prefix + "." + std::string(inst.name);

    processMembers(inst.body, inst_path, clock_db, ff_nodes, latch_warnings, errors);
}

// Walk members of any scope (InstanceBody, GenerateBlock, etc.)
static void processMembers(const slang::ast::Scope& scope,
                           const std::string& inst_path,
                           ClockDatabase& clock_db,
                           std::vector<std::unique_ptr<FFNode>>& ff_nodes,
                           std::vector<LatchWarning>& latch_warnings,
                           std::vector<FFClassificationError>& errors) {
    for (auto& member : scope.members()) {
        if (member.kind == slang::ast::SymbolKind::ProceduralBlock) {
            auto& block = member.as<slang::ast::ProceduralBlockSymbol>();

            // Flag latches as warnings (spec 4.2.3)
            if (block.procedureKind == slang::ast::ProceduralBlockKind::AlwaysLatch) {
                latch_warnings.push_back({inst_path,
                    "[Ac_conv03] always_latch detected — not a proper FF for CDC analysis"});
                continue;
            }

            if (block.procedureKind != slang::ast::ProceduralBlockKind::AlwaysFF &&
                block.procedureKind != slang::ast::ProceduralBlockKind::Always)
                continue;

            auto& body = block.getBody();

            // The body of always_ff is typically a TimedStatement
            const slang::ast::TimingControl* timing = nullptr;
            const slang::ast::Statement* inner_stmt = nullptr;

            if (body.kind == slang::ast::StatementKind::Timed) {
                auto& timed = body.as<slang::ast::TimedStatement>();
                timing = &timed.timing;
                inner_stmt = &timed.stmt;
            }

            if (!timing) continue;

            // Extract clock and reset from sensitivity list
            auto events = extractEvents(*timing);
            bool multi_clock = false;
            auto sens = classifyEvents(events, &multi_clock);

            if (multi_clock) {
                std::string clock_names;
                for (auto& ev : events) {
                    if (ClockTreeAnalyzer::isClockName(ev.signal_name) &&
                        !ClockTreeAnalyzer::isResetName(ev.signal_name)) {
                        if (!clock_names.empty()) clock_names += ", ";
                        clock_names += ev.signal_name;
                    }
                }
                errors.push_back({inst_path,
                    "[Ac_cdc11] Multiple clock edges in sensitivity list: " + clock_names +
                    ". FF may have ambiguous clocking."});
            }

            if (sens.clock_name.empty()) continue;

            // Find or create the domain for this clock
            ClockDomain* domain = nullptr;

            // 1) Direct match: source name or origin_signal matches the clock name
            for (auto& src : clock_db.sources) {
                if (src->origin_signal == sens.clock_name ||
                    src->name == sens.clock_name) {
                    domain = clock_db.findOrCreateDomain(src.get(), sens.clock_edge);
                    break;
                }
            }

            // 2) Clock net lookup: the clock may have been propagated through
            //    port connections with a different name (e.g., proc_clk <- sys_clk).
            //    Search clock nets by matching instance + clock name patterns.
            if (!domain) {
                std::string inst_leaf = inst_path;
                auto dot_pos = inst_leaf.rfind('.');
                if (dot_pos != std::string::npos)
                    inst_leaf = inst_leaf.substr(dot_pos + 1);
                std::string short_path = inst_leaf + "." + sens.clock_name;
                std::string full_path = inst_path + "." + sens.clock_name;
                for (auto& net : clock_db.nets) {
                    if (net->hier_path == full_path ||
                        net->hier_path == short_path ||
                        net->hier_path == sens.clock_name) {
                        domain = clock_db.findOrCreateDomain(net->source, sens.clock_edge);
                        break;
                    }
                }
            }

            // 3) If clock not found in db, create an auto-detected source
            if (!domain) {
                auto src = std::make_unique<ClockSource>();
                src->id = "auto_ff_" + sens.clock_name;
                src->name = sens.clock_name;
                src->type = ClockSource::Type::AutoDetected;
                src->origin_signal = sens.clock_name;
                auto* src_ptr = clock_db.addSource(std::move(src));
                domain = clock_db.findOrCreateDomain(src_ptr, sens.clock_edge);
            }

            // Create reset signal if present
            ResetSignal* reset_ptr = nullptr;
            if (!sens.reset_name.empty()) {
                auto reset = std::make_unique<ResetSignal>();
                reset->hier_path = inst_path + "." + sens.reset_name;
                reset->is_async = sens.reset_is_async;
                reset->polarity = sens.reset_polarity;
                reset_ptr = reset.get();
                clock_db.resets.push_back(std::move(reset));
            }

            // Collect variables assigned in this always_ff → these are FFs
            // Also collect per-variable fanin signals (RHS references)
            std::vector<std::string> assigned_vars;
            std::vector<FFAssignInfo> assign_infos;
            if (inner_stmt)
                collectAssignedVars(*inner_stmt, assigned_vars, assign_infos);

            if (assigned_vars.empty()) {
                // Fallback: create a single FF node for the entire block
                auto ff = std::make_unique<FFNode>();
                ff->hier_path = inst_path + ".__always_ff_" +
                    std::to_string(ff_nodes.size());
                ff->domain = domain;
                ff->reset = reset_ptr;
                ff_nodes.push_back(std::move(ff));
            } else {
                for (auto& var_name : assigned_vars) {
                    auto ff = std::make_unique<FFNode>();
                    ff->hier_path = inst_path + "." + var_name;
                    ff->domain = domain;
                    ff->reset = reset_ptr;

                    // Populate fanin_signals from all assignments to this variable
                    for (auto& ai : assign_infos) {
                        if (ai.lhs_name == var_name) {
                            for (auto& rhs : ai.rhs_signals) {
                                if (std::find(ff->fanin_signals.begin(),
                                              ff->fanin_signals.end(),
                                              rhs) == ff->fanin_signals.end()) {
                                    ff->fanin_signals.push_back(rhs);
                                }
                            }
                        }
                    }

                    ff_nodes.push_back(std::move(ff));
                }
            }
        }

        // Library cell FF recognition: check instance definition name for FF patterns
        if (member.kind == slang::ast::SymbolKind::Instance) {
            auto& child_inst = member.as<slang::ast::InstanceSymbol>();
            auto& def = child_inst.getDefinition();
            std::string def_name(def.name);
            std::string def_upper = def_name;
            std::transform(def_upper.begin(), def_upper.end(),
                           def_upper.begin(), ::toupper);

            bool is_lib_ff = false;
            for (auto& pat : {"DFF", "SDFF", "DFFR", "FDRE", "FD"}) {
                std::string spat(pat);
                if (def_upper.find(spat) != std::string::npos) {
                    is_lib_ff = true;
                    break;
                }
            }

            // Also check for (* cdc_ff *) attribute
            if (!is_lib_ff) {
                auto attrs = child_inst.getParentScope()->getCompilation().getAttributes(child_inst);
                for (auto* attr : attrs) {
                    if (attr->name == "cdc_ff") {
                        is_lib_ff = true;
                        break;
                    }
                }
            }

            if (is_lib_ff) {
                std::string child_path = inst_path + "." + std::string(child_inst.name);
                // Try to find clock domain from port connections
                ClockDomain* domain = nullptr;
                for (auto* conn : child_inst.getPortConnections()) {
                    if (!conn) continue;
                    std::string port_name(conn->port.name);
                    std::string lower_port = port_name;
                    std::transform(lower_port.begin(), lower_port.end(),
                                   lower_port.begin(), ::tolower);
                    if (ClockTreeAnalyzer::isClockName(lower_port)) {
                        auto* expr = conn->getExpression();
                        if (expr) {
                            std::string clk_sig = extractSignalName(*expr);
                            if (!clk_sig.empty()) {
                                for (auto& src : clock_db.sources) {
                                    if (src->origin_signal == clk_sig ||
                                        src->name == clk_sig) {
                                        domain = clock_db.findOrCreateDomain(
                                            src.get(), Edge::Posedge);
                                        break;
                                    }
                                }
                            }
                        }
                        break;
                    }
                }

                auto ff = std::make_unique<FFNode>();
                ff->hier_path = child_path;
                ff->domain = domain;
                ff_nodes.push_back(std::move(ff));
            } else {
                // Recurse into child instances (non-library cells)
                processInstance(child_inst,
                              inst_path, clock_db, ff_nodes, latch_warnings, errors);
            }
        }

        // Recurse into generate blocks
        if (member.kind == slang::ast::SymbolKind::GenerateBlock) {
            auto& gen = member.as<slang::ast::GenerateBlockSymbol>();
            if (!gen.isUninstantiated) {
                std::string gen_name = gen.getExternalName();
                std::string gen_path = inst_path;
                if (!gen_name.empty())
                    gen_path = inst_path + "." + gen_name;
                processMembers(gen, gen_path, clock_db, ff_nodes, latch_warnings, errors);
            }
        }

        if (member.kind == slang::ast::SymbolKind::GenerateBlockArray) {
            auto& arr = member.as<slang::ast::GenerateBlockArraySymbol>();
            for (auto* entry : arr.entries) {
                if (entry && !entry->isUninstantiated) {
                    std::string entry_name = entry->getExternalName();
                    if (entry_name.empty())
                        entry_name = std::string(arr.name);
                    std::string entry_path = inst_path + "." + entry_name;
                    processMembers(*entry, entry_path, clock_db, ff_nodes,
                                   latch_warnings, errors);
                }
            }
        }
    }
}

void FFClassifier::analyze() {
    auto& root = compilation_.getRoot();

    for (auto& member : root.members()) {
        if (member.kind == slang::ast::SymbolKind::Instance) {
            processInstance(member.as<slang::ast::InstanceSymbol>(),
                          "", clock_db_, ff_nodes_, latch_warnings_, errors_);
        }
    }
}

const std::vector<std::unique_ptr<FFNode>>& FFClassifier::getFFNodes() const {
    return ff_nodes_;
}

std::vector<std::unique_ptr<FFNode>> FFClassifier::releaseFFNodes() {
    return std::move(ff_nodes_);
}

const std::vector<LatchWarning>& FFClassifier::getLatchWarnings() const {
    return latch_warnings_;
}

const std::vector<FFClassificationError>& FFClassifier::getErrors() const {
    return errors_;
}

} // namespace sv_cdccheck
