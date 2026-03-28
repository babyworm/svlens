#pragma once

namespace connect {

inline constexpr const char* HTML_TEMPLATE = R"HTMLTPL(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>slang-connect: {{TOP_MODULE}}</title>
<script src="https://unpkg.com/vis-network@9.1.6/standalone/umd/vis-network.min.js"></script>
<style>
:root {
  --bg-primary: #1a1a2e;
  --bg-secondary: #16213e;
  --bg-card: #1a2744;
  --border: #0f3460;
  --text-primary: #e0e0e0;
  --text-secondary: #999;
  --accent-blue: #3498db;
  --accent-green: #2ecc71;
  --accent-yellow: #f1c40f;
  --accent-red: #e74c3c;
  --accent-orange: #e97c00;
}
* { margin: 0; padding: 0; box-sizing: border-box; }
body { font-family: 'Segoe UI', system-ui, -apple-system, sans-serif; background: var(--bg-primary); color: var(--text-primary); overflow: hidden; height: 100vh; display: flex; flex-direction: column; }

/* --- Header --- */
#header { background: var(--bg-secondary); padding: 10px 24px; display: flex; align-items: center; gap: 16px; flex-wrap: wrap; border-bottom: 1px solid var(--border); flex-shrink: 0; }
#header h1 { font-size: 17px; color: #e94560; white-space: nowrap; letter-spacing: 0.5px; }
#header .top-name { font-size: 14px; color: #a0c4ff; font-weight: 600; }
.badge { display: inline-block; padding: 2px 9px; border-radius: 10px; font-size: 11px; font-weight: 700; margin-left: 4px; }
.badge-err { background: var(--accent-red); color: #fff; }
.badge-warn { background: var(--accent-orange); color: #fff; }
.badge-info { background: var(--accent-blue); color: #fff; }
.badge-conn { background: #2d6a4f; color: #fff; }
.badge-waived { background: #555; color: #ccc; }

/* --- Tabs --- */
#tab-bar { background: var(--bg-secondary); display: flex; gap: 0; border-bottom: 2px solid var(--border); flex-shrink: 0; padding: 0 24px; }
.tab-btn { padding: 9px 22px; background: none; border: none; color: var(--text-secondary); font-size: 13px; font-weight: 600; cursor: pointer; border-bottom: 2px solid transparent; margin-bottom: -2px; transition: color 0.15s, border-color 0.15s; }
.tab-btn:hover { color: var(--text-primary); }
.tab-btn.active { color: #e94560; border-bottom-color: #e94560; }

/* --- Tab content --- */
#tab-content { flex: 1; overflow: hidden; position: relative; }
.tab-pane { display: none; position: absolute; inset: 0; overflow-y: auto; }
.tab-pane.active { display: block; }

/* --- Overview tab --- */
.overview-top { display: flex; gap: 20px; padding: 20px 24px; align-items: flex-start; flex-wrap: wrap; }
.gauge-card { background: var(--bg-card); border: 1px solid var(--border); border-radius: 10px; padding: 18px 24px; text-align: center; min-width: 160px; }
.gauge-card h3 { font-size: 12px; color: var(--text-secondary); text-transform: uppercase; letter-spacing: 1px; margin-bottom: 10px; }
.stats-row { display: flex; gap: 12px; flex-wrap: wrap; align-items: stretch; flex: 1; }
.stat-card { background: var(--bg-card); border: 1px solid var(--border); border-radius: 10px; padding: 14px 18px; flex: 1; min-width: 110px; }
.stat-card .stat-val { font-size: 28px; font-weight: 700; }
.stat-card .stat-label { font-size: 11px; color: var(--text-secondary); text-transform: uppercase; margin-top: 2px; }

.overview-sections { padding: 0 24px 24px; }
.section-title { font-size: 14px; font-weight: 700; color: var(--text-secondary); text-transform: uppercase; letter-spacing: 1px; margin: 20px 0 10px; padding-bottom: 6px; border-bottom: 1px solid var(--border); }

/* Module health bars */
.health-bar-row { display: flex; align-items: center; gap: 10px; margin-bottom: 6px; padding: 6px 10px; background: var(--bg-card); border-radius: 6px; cursor: pointer; transition: background 0.15s; }
.health-bar-row:hover { background: #1f3050; }
.health-bar-name { width: 160px; font-size: 13px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; flex-shrink: 0; }
.health-bar-track { flex: 1; height: 18px; background: #0d1b2a; border-radius: 4px; overflow: hidden; position: relative; }
.health-bar-fill { height: 100%; border-radius: 4px; transition: width 0.4s ease; }
.health-bar-score { width: 50px; font-size: 12px; font-weight: 700; text-align: right; flex-shrink: 0; }
.health-bar-issues { width: 80px; font-size: 11px; color: var(--text-secondary); text-align: right; flex-shrink: 0; }

/* Risk cards */
.risk-card { background: var(--bg-card); border-radius: 8px; padding: 10px 14px; margin-bottom: 6px; border-left: 4px solid #555; }
.risk-card.risk-HIGH { border-left-color: var(--accent-red); }
.risk-card.risk-MEDIUM { border-left-color: var(--accent-orange); }
.risk-card.risk-LOW { border-left-color: var(--text-secondary); }
.risk-header { display: flex; align-items: center; gap: 8px; margin-bottom: 4px; }
.risk-level { font-size: 11px; font-weight: 700; text-transform: uppercase; padding: 1px 7px; border-radius: 4px; }
.risk-HIGH .risk-level { background: var(--accent-red); color: #fff; }
.risk-MEDIUM .risk-level { background: var(--accent-orange); color: #fff; }
.risk-LOW .risk-level { background: #444; color: #bbb; }
.risk-type { font-size: 11px; color: var(--text-secondary); }
.risk-port { font-size: 13px; color: #a0c4ff; }
.risk-reason { font-size: 12px; color: var(--text-secondary); margin-top: 2px; }

/* Coupling table */
.coupling-table { width: 100%; border-collapse: collapse; font-size: 13px; }
.coupling-table th { text-align: left; padding: 6px 10px; color: var(--text-secondary); font-size: 11px; text-transform: uppercase; border-bottom: 1px solid var(--border); }
.coupling-table td { padding: 6px 10px; border-bottom: 1px solid #0d1b2a; }
.coupling-bar-cell { width: 40%; }
.coupling-bar-track { height: 14px; background: #0d1b2a; border-radius: 3px; overflow: hidden; }
.coupling-bar-fill { height: 100%; background: var(--accent-blue); border-radius: 3px; transition: width 0.3s; }

/* --- Graph tab --- */
#graph-container { width: 100%; height: 100%; background: #0f0f23; }
#graph-toolbar { position: absolute; top: 10px; left: 10px; z-index: 10; display: flex; gap: 8px; align-items: center; }
#graph-toolbar input[type="text"] { padding: 5px 10px; border-radius: 5px; border: 1px solid var(--border); background: var(--bg-secondary); color: var(--text-primary); font-size: 12px; width: 180px; outline: none; }
#graph-toolbar input[type="text"]:focus { border-color: var(--accent-blue); }
#graph-toolbar button { padding: 5px 12px; border-radius: 5px; border: 1px solid var(--border); background: var(--bg-secondary); color: var(--text-primary); font-size: 12px; cursor: pointer; transition: background 0.15s; }
#graph-toolbar button:hover { background: #1f3050; }
#graph-legend { position: absolute; bottom: 14px; left: 14px; z-index: 10; background: rgba(22,33,62,0.92); border: 1px solid var(--border); border-radius: 8px; padding: 10px 14px; font-size: 11px; color: var(--text-secondary); pointer-events: none; }
#graph-legend .legend-row { display: flex; align-items: center; gap: 7px; margin-bottom: 4px; }
#graph-legend .legend-swatch { width: 14px; height: 14px; border-radius: 3px; flex-shrink: 0; border: 1px solid rgba(255,255,255,0.1); }
#graph-legend .legend-line { width: 22px; height: 0; border-top: 2px solid; flex-shrink: 0; }
#graph-legend .legend-title { font-weight: 700; color: var(--text-primary); margin-bottom: 4px; font-size: 12px; }
#graph-info-panel { display: none; position: absolute; right: 16px; top: 56px; width: 310px; max-height: calc(100% - 70px); overflow-y: auto; background: rgba(22,33,62,0.96); border: 1px solid var(--border); border-radius: 8px; padding: 16px; z-index: 10; }
#graph-info-panel h3 { font-size: 15px; margin-bottom: 10px; color: #a0c4ff; word-break: break-all; }
#graph-info-panel .info-row { display: flex; justify-content: space-between; font-size: 12px; padding: 3px 0; border-bottom: 1px solid #0d1b2a; }
#graph-info-panel .info-row .info-label { color: var(--text-secondary); }
#graph-info-panel .info-section-title { font-size: 11px; font-weight: 700; color: var(--text-secondary); text-transform: uppercase; margin-top: 10px; margin-bottom: 4px; }
#graph-info-panel .info-conn-item { font-size: 12px; padding: 3px 6px; margin-bottom: 2px; background: #0d1b2a; border-radius: 3px; }
#graph-info-panel .info-issue-item { font-size: 11px; padding: 3px 6px; margin-bottom: 2px; border-radius: 3px; border-left: 3px solid #555; background: #0d1b2a; }
#graph-info-panel .info-issue-item.sev-err { border-left-color: var(--accent-red); }
#graph-info-panel .info-issue-item.sev-warn { border-left-color: var(--accent-orange); }
#graph-info-panel .info-close { position: absolute; top: 8px; right: 10px; background: none; border: none; color: var(--text-secondary); font-size: 16px; cursor: pointer; }
#graph-info-panel .info-close:hover { color: var(--text-primary); }
#graph-info-panel .signal-status { display: inline-block; padding: 1px 5px; border-radius: 3px; font-size: 10px; font-weight: 600; }
#graph-info-panel .signal-status.st-ok { background: #1a3d2e; color: var(--accent-green); }
#graph-info-panel .signal-status.st-err { background: #3d1a1a; color: var(--accent-red); }
#graph-info-panel .signal-status.st-warn { background: #3d3a1a; color: var(--accent-orange); }

/* --- Heatmap tab --- */
.heatmap-wrapper { padding: 24px; overflow: auto; height: 100%; }
.heatmap-title { font-size: 14px; color: var(--text-secondary); margin-bottom: 14px; }
.hm-table { border-collapse: separate; border-spacing: 2px; }
.hm-table td, .hm-table th { padding: 0; vertical-align: middle; }
.hm-table .hm-ylbl { width: 120px; max-width: 120px; text-align: right; padding-right: 10px; font-size: 11px; font-family: monospace; font-weight: normal; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; height: 36px; }
.hm-table .hm-xlbl { font-size: 11px; color: var(--text-secondary); font-weight: normal; writing-mode: vertical-lr; transform: rotate(180deg); height: 100px; text-align: left; padding-bottom: 6px; white-space: nowrap; overflow: hidden; width: 36px; }
.heatmap-cell { width: 36px; height: 36px; border-radius: 3px; cursor: pointer; transition: transform 0.1s, box-shadow 0.15s; text-align: center; font-size: 10px; color: transparent; }
.heatmap-cell:hover { transform: scale(1.15); box-shadow: 0 0 8px rgba(52,152,219,0.5); color: #fff; z-index: 1; position: relative; }
.heatmap-tooltip { position: fixed; background: #111; color: #fff; padding: 6px 10px; border-radius: 5px; font-size: 12px; pointer-events: none; z-index: 100; display: none; border: 1px solid var(--border); }
.heatmap-legend { display: flex; align-items: center; gap: 6px; margin-top: 14px; font-size: 11px; color: var(--text-secondary); }
.heatmap-legend-bar { width: 120px; height: 12px; border-radius: 3px; }

/* --- Details tab --- */
.details-wrapper { padding: 20px 24px; }
.module-card { background: var(--bg-card); border: 1px solid var(--border); border-radius: 8px; margin-bottom: 10px; overflow: hidden; }
.module-card-header { padding: 12px 16px; cursor: pointer; display: flex; align-items: center; gap: 12px; transition: background 0.15s; }
.module-card-header:hover { background: #1f3050; }
.module-card-chevron { font-size: 12px; color: var(--text-secondary); transition: transform 0.2s; width: 16px; text-align: center; flex-shrink: 0; }
.module-card.open .module-card-chevron { transform: rotate(90deg); }
.module-card-name { font-size: 14px; font-weight: 600; flex: 1; }
.module-card-instance { font-size: 11px; color: var(--text-secondary); }
.module-card-score-bar { width: 80px; height: 8px; background: #0d1b2a; border-radius: 4px; overflow: hidden; flex-shrink: 0; }
.module-card-score-fill { height: 100%; border-radius: 4px; }
.module-card-score-text { font-size: 12px; font-weight: 700; width: 40px; text-align: right; flex-shrink: 0; }
.module-card-body { display: none; border-top: 1px solid var(--border); padding: 12px 16px; }
.module-card.open .module-card-body { display: block; }

.port-table { width: 100%; border-collapse: collapse; font-size: 12px; margin-top: 8px; }
.port-table th { text-align: left; padding: 4px 8px; color: var(--text-secondary); font-size: 10px; text-transform: uppercase; border-bottom: 1px solid var(--border); }
.port-table td { padding: 4px 8px; border-bottom: 1px solid #0d1b2a; }
.port-status-icon { display: inline-block; width: 16px; text-align: center; }
.port-ok { color: var(--accent-green); }
.port-err { color: var(--accent-red); }
.port-warn { color: var(--accent-orange); }
.port-unc { color: var(--text-secondary); }

.detail-issues { margin-top: 10px; }
.detail-issues h4 { font-size: 12px; color: var(--text-secondary); margin-bottom: 6px; }
.detail-issue-item { font-size: 12px; padding: 4px 8px; background: #0d1b2a; border-radius: 4px; margin-bottom: 3px; border-left: 3px solid #555; }
.detail-issue-item.sev-ERROR { border-left-color: var(--accent-red); }
.detail-issue-item.sev-WARN { border-left-color: var(--accent-orange); }
.detail-issue-item.sev-INFO { border-left-color: var(--accent-blue); }

/* Scrollbar styling */
::-webkit-scrollbar { width: 8px; height: 8px; }
::-webkit-scrollbar-track { background: var(--bg-primary); }
::-webkit-scrollbar-thumb { background: #334; border-radius: 4px; }
::-webkit-scrollbar-thumb:hover { background: #445; }

/* No-data placeholder */
.no-data { text-align: center; color: #555; padding: 60px 20px; font-size: 14px; }
</style>
</head>
<body>

<!-- Header -->
<div id="header">
  <h1>slang-connect</h1>
  <span class="top-name">{{TOP_MODULE}}</span>
  <span id="badges"></span>
</div>

<!-- Tab bar -->
<div id="tab-bar">
  <button class="tab-btn active" data-tab="overview">Overview</button>
  <button class="tab-btn" data-tab="graph">Graph</button>
  <button class="tab-btn" data-tab="heatmap">Heatmap</button>
  <button class="tab-btn" data-tab="details">Details</button>
</div>

<!-- Tab content -->
<div id="tab-content">
  <div class="tab-pane active" id="pane-overview"></div>
  <div class="tab-pane" id="pane-graph">
    <div id="graph-toolbar">
      <input type="text" id="graph-search" placeholder="Search module..." />
      <button id="graph-reset-btn">Reset View</button>
    </div>
    <div id="graph-container"></div>
    <div id="graph-info-panel">
      <button class="info-close" id="info-close-btn">&times;</button>
      <h3 id="info-title"></h3>
      <div id="info-content"></div>
    </div>
    <div id="graph-legend">
      <div class="legend-title">Legend</div>
      <div class="legend-row"><div class="legend-swatch" style="background:#1a3d2e;border-color:#2ecc71"></div> Health &gt; 80%</div>
      <div class="legend-row"><div class="legend-swatch" style="background:#3d3a1a;border-color:#f1c40f"></div> Health 60-80%</div>
      <div class="legend-row"><div class="legend-swatch" style="background:#3d1a1a;border-color:#e74c3c"></div> Health &lt; 60%</div>
      <div class="legend-row"><div class="legend-line" style="border-color:#3a3a5a"></div> Normal edge</div>
      <div class="legend-row"><div class="legend-line" style="border-color:#e97c00"></div> Warning edge</div>
      <div class="legend-row"><div class="legend-line" style="border-color:#e94560"></div> Error edge</div>
      <div class="legend-row" style="margin-top:6px;color:#777">Click: focus | Dbl-click: expand ports</div>
    </div>
  </div>
  <div class="tab-pane" id="pane-heatmap"></div>
  <div class="tab-pane" id="pane-details"></div>
</div>

<div class="heatmap-tooltip" id="hm-tooltip"></div>

<script>
var DATA = {{JSON_DATA}};
var analysis = DATA.analysis || null;

// ---------- Utility ----------
function scoreColor(s) {
  if (s > 0.8) return 'var(--accent-green)';
  if (s > 0.6) return 'var(--accent-yellow)';
  return 'var(--accent-red)';
}
function pct(v) { return Math.round(v * 100); }
function esc(s) {
  var d = document.createElement('div'); d.textContent = s; return d.innerHTML;
}

// ---------- Badges ----------
(function() {
  var s = DATA.summary;
  var el = document.getElementById('badges');
  var h = '<span class="badge badge-conn">' + s.connections_analyzed + ' connections</span>';
  if (s.errors)   h += '<span class="badge badge-err">' + s.errors + ' errors</span>';
  if (s.warnings) h += '<span class="badge badge-warn">' + s.warnings + ' warnings</span>';
  if (s.info)     h += '<span class="badge badge-info">' + s.info + ' info</span>';
  if (s.waived)   h += '<span class="badge badge-waived">' + s.waived + ' waived</span>';
  el.innerHTML = h;
})();

// ---------- Tab navigation ----------
(function() {
  var btns = document.querySelectorAll('.tab-btn');
  var panes = document.querySelectorAll('.tab-pane');
  var graphInit = false;
  btns.forEach(function(btn) {
    btn.addEventListener('click', function() {
      btns.forEach(function(b) { b.classList.remove('active'); });
      panes.forEach(function(p) { p.classList.remove('active'); });
      btn.classList.add('active');
      document.getElementById('pane-' + btn.dataset.tab).classList.add('active');
      if (btn.dataset.tab === 'graph' && !graphInit) { graphInit = true; initGraph(); }
    });
  });
})();

// =============================================
//   TAB 1: OVERVIEW
// =============================================
(function() {
  var pane = document.getElementById('pane-overview');
  if (!analysis) {
    pane.innerHTML = '<div class="no-data">No analysis data available. Run with analysis enabled.</div>';
    return;
  }

  var h = '';

  // --- Top row: gauge + stats ---
  var score = analysis.overall_score;
  var circ = 314.16;
  var filled = score * circ;
  var remain = circ - filled;
  var sc = scoreColor(score);

  h += '<div class="overview-top">';
  h += '<div class="gauge-card"><h3>Overall Health</h3>';
  h += '<svg width="120" height="120" viewBox="0 0 120 120">';
  h += '<circle cx="60" cy="60" r="50" fill="none" stroke="#222" stroke-width="10"/>';
  h += '<circle cx="60" cy="60" r="50" fill="none" stroke="' + sc + '" stroke-width="10" ';
  h += 'stroke-dasharray="' + filled.toFixed(1) + ' ' + remain.toFixed(1) + '" ';
  h += 'stroke-linecap="round" transform="rotate(-90 60 60)"/>';
  h += '<text x="60" y="60" text-anchor="middle" dominant-baseline="central" fill="white" font-size="26" font-weight="700">' + pct(score) + '%</text>';
  h += '</svg></div>';

  h += '<div class="stats-row">';
  h += statCard(analysis.total_ports, 'Total Ports', 'var(--accent-blue)');
  h += statCard(analysis.total_connections, 'Connections', 'var(--accent-green)');
  h += statCard(DATA.summary.errors, 'Errors', 'var(--accent-red)');
  h += statCard(DATA.summary.warnings, 'Warnings', 'var(--accent-orange)');
  h += statCard(analysis.module_health.length, 'Modules', 'var(--accent-blue)');
  h += '</div></div>';

  // --- Module Health ---
  h += '<div class="overview-sections">';
  h += '<div class="section-title">Module Health</div>';
  var mods = analysis.module_health.slice().sort(function(a, b) { return a.score - b.score; });
  mods.forEach(function(m) {
    var c = scoreColor(m.score);
    var w = Math.max(2, pct(m.score));
    var issues = '';
    if (m.errors) issues += m.errors + 'E ';
    if (m.warnings) issues += m.warnings + 'W';
    if (!issues) issues = '-';
    h += '<div class="health-bar-row" data-instance="' + esc(m.instance) + '" onclick="switchToDetails(\'' + esc(m.instance) + '\')">';
    h += '<div class="health-bar-name" title="' + esc(m.instance) + '">' + esc(m.name) + '</div>';
    h += '<div class="health-bar-track"><div class="health-bar-fill" style="width:' + w + '%;background:' + c + '"></div></div>';
    h += '<div class="health-bar-score" style="color:' + c + '">' + pct(m.score) + '%</div>';
    h += '<div class="health-bar-issues">' + issues.trim() + '</div>';
    h += '</div>';
  });

  // --- Risk Assessment ---
  if (analysis.risks.length > 0) {
    h += '<div class="section-title">Risk Assessment</div>';
    analysis.risks.forEach(function(r) {
      h += '<div class="risk-card risk-' + r.level + '">';
      h += '<div class="risk-header"><span class="risk-level">' + r.level + '</span>';
      h += '<span class="risk-type">' + esc(r.type.replace(/_/g, ' ')) + '</span></div>';
      h += '<div class="risk-port">' + esc(r.port) + '</div>';
      h += '<div class="risk-reason">' + esc(r.reason) + '</div>';
      h += '</div>';
    });
  }

  // --- Coupling Top-N ---
  if (analysis.coupling.length > 0) {
    var topN = analysis.coupling.slice(0, 8);
    var maxConn = topN[0].connections;
    h += '<div class="section-title">Top Coupling</div>';
    h += '<table class="coupling-table"><thead><tr><th>Source</th><th>Dest</th><th>Signals</th><th class="coupling-bar-cell">Relative</th></tr></thead><tbody>';
    topN.forEach(function(c) {
      var bw = Math.max(4, Math.round(c.connections / maxConn * 100));
      h += '<tr><td>' + esc(c.source) + '</td><td>' + esc(c.dest) + '</td><td>' + c.connections + '</td>';
      h += '<td class="coupling-bar-cell"><div class="coupling-bar-track"><div class="coupling-bar-fill" style="width:' + bw + '%"></div></div></td></tr>';
    });
    h += '</tbody></table>';
  }

  h += '</div>'; // overview-sections

  pane.innerHTML = h;

  function statCard(val, label, color) {
    return '<div class="stat-card"><div class="stat-val" style="color:' + color + '">' + val + '</div><div class="stat-label">' + label + '</div></div>';
  }
})();

// ---------- Navigate to details tab and open a module ----------
function switchToDetails(inst) {
  document.querySelectorAll('.tab-btn').forEach(function(b) { b.classList.remove('active'); });
  document.querySelectorAll('.tab-pane').forEach(function(p) { p.classList.remove('active'); });
  document.querySelector('.tab-btn[data-tab="details"]').classList.add('active');
  document.getElementById('pane-details').classList.add('active');
  setTimeout(function() {
    var card = document.querySelector('.module-card[data-instance="' + inst + '"]');
    if (card) {
      card.classList.add('open');
      card.scrollIntoView({ behavior: 'smooth', block: 'start' });
    }
  }, 50);
}

// =============================================
//   TAB 2: GRAPH (vis-network) - Progressive
// =============================================
var graphNetwork = null;
var graphNodes = null;
var graphEdges = null;
var focusedModule = null;
var expandedModules = new Set();
var graphModuleData = {};  // instPath -> { short, health, ports, connections, issues }
var graphEdgeData = {};    // edgeId -> { from, to, signals[] }
var graphOrigNodes = [];   // original module-level nodes for reset
var graphOrigEdges = [];   // original module-level edges for reset

function initGraph() {
  // --- Build module data from connections ---
  var instances = new Map();
  var portsByInst = {};  // instPath -> { portName -> { dir, width, status, connections[] } }
  DATA.connections.forEach(function(c) {
    var sParts = c.source.split('.');
    var dParts = c.dest.split('.');
    var sInst = sParts.slice(0, -1).join('.');
    var dInst = dParts.slice(0, -1).join('.');
    var sShort = sInst.split('.').pop();
    var dShort = dInst.split('.').pop();
    if (!instances.has(sInst)) instances.set(sInst, sShort);
    if (!instances.has(dInst)) instances.set(dInst, dShort);
    var sPort = sParts[sParts.length - 1];
    var dPort = dParts[dParts.length - 1];
    if (!portsByInst[sInst]) portsByInst[sInst] = {};
    if (!portsByInst[sInst][sPort]) portsByInst[sInst][sPort] = { dir: 'output', width: 0, status: 'OK', connections: [] };
    portsByInst[sInst][sPort].connections.push({ dest: dInst, destPort: dPort, status: c.status, raw: c });
    if (c.status !== 'OK') portsByInst[sInst][sPort].status = c.status;
    if (!portsByInst[dInst]) portsByInst[dInst] = {};
    if (!portsByInst[dInst][dPort]) portsByInst[dInst][dPort] = { dir: 'input', width: 0, status: 'OK', connections: [] };
    portsByInst[dInst][dPort].connections.push({ dest: sInst, destPort: sPort, status: c.status, raw: c });
    if (c.status !== 'OK') portsByInst[dInst][dPort].status = c.status;
  });

  // Extract widths from port names like o_data[31:0]
  function parseWidth(name) {
    var m = name.match(/\[(\d+):(\d+)\]/);
    if (m) return Math.abs(parseInt(m[1]) - parseInt(m[2])) + 1;
    m = name.match(/\[(\d+)\]/);
    if (m) return 1;
    return 1;
  }
  Object.keys(portsByInst).forEach(function(inst) {
    Object.keys(portsByInst[inst]).forEach(function(pn) {
      portsByInst[inst][pn].width = parseWidth(pn);
    });
  });

  // Health lookup
  var healthMap = {};
  var healthByName = {};
  if (analysis) {
    analysis.module_health.forEach(function(m) {
      healthMap[m.instance] = m;
      healthByName[m.name] = m;
    });
  }

  // Issue lookup by instance
  var issuesByInst = {};
  DATA.issues.forEach(function(iss) {
    var parts = iss.port.split('.');
    var inst = parts.slice(0, -1).join('.');
    if (!issuesByInst[inst]) issuesByInst[inst] = [];
    issuesByInst[inst].push(iss);
  });

  // Issue severity per port
  var portIssueSev = {};
  DATA.issues.forEach(function(iss) {
    if (iss.source && iss.source.instance && iss.source.port) {
      var k = iss.source.instance + '::' + iss.source.port;
      if (!portIssueSev[k] || iss.severity === 'ERROR') portIssueSev[k] = iss.severity;
    }
    if (iss.dest && iss.dest.instance && iss.dest.port) {
      var k2 = iss.dest.instance + '::' + iss.dest.port;
      if (!portIssueSev[k2] || iss.severity === 'ERROR') portIssueSev[k2] = iss.severity;
    }
  });

  // Build graphModuleData
  instances.forEach(function(shortName, instPath) {
    var h = healthMap[instPath];
    var ports = portsByInst[instPath] || {};
    var portCount = Object.keys(ports).length;
    graphModuleData[instPath] = {
      short: shortName,
      instPath: instPath,
      health: h ? h.score : null,
      totalPorts: h ? h.total_ports : portCount,
      connected: h ? h.connected : portCount,
      errors: h ? h.errors : 0,
      warnings: h ? h.warnings : 0,
      ports: ports,
      issues: issuesByInst[instPath] || []
    };
  });

  // --- Severity for edges ---
  var sevRank = { INFO: 1, WARN: 2, ERROR: 3 };
  var connSev = new Map();
  DATA.issues.forEach(function(iss) {
    if (iss.source && iss.dest) {
      var key = iss.source.instance + '->' + iss.dest.instance;
      var cur = connSev.get(key);
      if (!cur || (sevRank[iss.severity] || 0) > (sevRank[cur] || 0)) {
        connSev.set(key, iss.severity);
      }
    }
  });

  // --- Group edges at module level ---
  var edgeGroups = new Map();
  DATA.connections.forEach(function(c) {
    var sParts = c.source.split('.');
    var dParts = c.dest.split('.');
    var sInst = sParts.slice(0, -1).join('.');
    var dInst = dParts.slice(0, -1).join('.');
    var key = sInst + '->' + dInst;
    if (!edgeGroups.has(key)) edgeGroups.set(key, { from: sInst, to: dInst, signals: [] });
    var g = edgeGroups.get(key);
    var sPort = sParts[sParts.length - 1];
    var dPort = dParts[dParts.length - 1];
    g.signals.push({ sourcePort: sPort, destPort: dPort, status: c.status });
  });
  connSev.forEach(function(sev, key) {
    var g = edgeGroups.get(key);
    if (g) g.severity = sev;
  });

  // --- Build vis nodes (Level 1: module overview) ---
  graphNodes = new vis.DataSet();
  graphEdges = new vis.DataSet();
  var nodeIdCounter = 0;
  var instToNodeId = {};

  function healthColors(score) {
    if (typeof score !== 'number') return { bg: '#16213e', border: '#0f3460' };
    if (score > 0.8) return { bg: '#1a3d2e', border: '#2ecc71' };
    if (score > 0.6) return { bg: '#3d3a1a', border: '#f1c40f' };
    return { bg: '#3d1a1a', border: '#e74c3c' };
  }

  instances.forEach(function(shortName, instPath) {
    var md = graphModuleData[instPath];
    var sc = md.health;
    var hc = healthColors(sc);
    var portCount = md.totalPorts || Object.keys(md.ports).length;
    var baseSize = Math.max(20, Math.min(50, 16 + portCount * 2));
    var nid = 'mod_' + instPath;
    instToNodeId[instPath] = nid;
    var nodeObj = {
      id: nid, label: shortName + '\n' + portCount + ' ports',
      shape: 'box', group: 'module',
      color: { background: hc.bg, border: hc.border,
        highlight: { background: '#1a3a5c', border: '#e94560' },
        hover: { background: '#1a3a5c', border: '#a0c4ff' } },
      font: { color: '#e0e0e0', size: 14, multi: true },
      size: baseSize, borderWidth: 2,
      widthConstraint: { minimum: 80 },
      instPath: instPath
    };
    graphNodes.add(nodeObj);
    graphOrigNodes.push(Object.assign({}, nodeObj));
  });

  var sevColor = { ERROR: '#e94560', WARN: '#e97c00', INFO: '#4a90d9' };
  var edgeIdCounter = 0;
  edgeGroups.forEach(function(g) {
    var fromId = instToNodeId[g.from];
    var toId = instToNodeId[g.to];
    if (!fromId || !toId) return;
    var sev = g.severity;
    var col = sev ? (sevColor[sev] || '#3a3a5a') : '#3a3a5a';
    var count = g.signals.length;
    var w = Math.max(1, Math.min(6, Math.round(Math.log2(count + 1) * 1.5)));
    var eid = 'edge_' + edgeIdCounter++;
    var edgeObj = {
      id: eid, from: fromId, to: toId, arrows: 'to',
      label: count + (count === 1 ? ' signal' : ' signals'),
      font: { color: '#888', size: 10, strokeWidth: 0, background: 'transparent' },
      color: { color: col, highlight: '#e94560', hover: '#a0c4ff' },
      width: w, hoverWidth: 0.5,
      smooth: { type: 'cubicBezier' }
    };
    graphEdges.add(edgeObj);
    graphOrigEdges.push(Object.assign({}, edgeObj));
    graphEdgeData[eid] = { from: g.from, to: g.to, signals: g.signals, severity: sev };
  });

  // --- Create network ---
  var container = document.getElementById('graph-container');
  graphNetwork = new vis.Network(container, { nodes: graphNodes, edges: graphEdges }, {
    layout: { hierarchical: { direction: 'LR', sortMethod: 'directed', levelSeparation: 240, nodeSpacing: 80 } },
    physics: false,
    edges: { smooth: { type: 'cubicBezier' } },
    interaction: { hover: true, tooltipDelay: 150, zoomView: true, dragView: true }
  });

  // --- Click: focus mode (Level 2) ---
  graphNetwork.on('click', function(params) {
    var infoPanel = document.getElementById('graph-info-panel');
    if (params.nodes.length === 1) {
      var nid = params.nodes[0];
      var nodeData = graphNodes.get(nid);
      if (!nodeData || nodeData.group === 'port_in' || nodeData.group === 'port_out') return;
      if (nodeData.group !== 'module') return;
      var instPath = nodeData.instPath;
      if (focusedModule === instPath) return;  // already focused
      focusedModule = instPath;
      applyFocusMode(instPath);
      showModuleInfo(instPath);
    } else if (params.edges.length === 1 && params.nodes.length === 0) {
      var eid = params.edges[0];
      showEdgeInfo(eid);
    } else if (params.nodes.length === 0 && params.edges.length === 0) {
      clearFocusMode();
      infoPanel.style.display = 'none';
    }
  });

  // --- Double-click: expand ports (Level 3) ---
  graphNetwork.on('doubleClick', function(params) {
    if (params.nodes.length !== 1) return;
    var nid = params.nodes[0];
    var nodeData = graphNodes.get(nid);
    if (!nodeData || nodeData.group !== 'module') return;
    var instPath = nodeData.instPath;
    if (expandedModules.has(instPath)) {
      collapseModule(instPath);
    } else {
      expandModule(instPath);
    }
  });

  // --- Hover on edge: tooltip ---
  graphNetwork.on('hoverEdge', function(params) {
    var eid = params.edge;
    var ed = graphEdgeData[eid];
    if (!ed) return;
    var lines = ed.signals.slice(0, 15).map(function(s) {
      return s.sourcePort.replace(/\[.*/, '') + ' -> ' + s.destPort.replace(/\[.*/, '') + (s.status !== 'OK' ? ' [' + s.status + ']' : '');
    });
    if (ed.signals.length > 15) lines.push('... +' + (ed.signals.length - 15) + ' more');
    graphNetwork.canvas.body.container.title = lines.join('\n');
  });
  graphNetwork.on('blurEdge', function() {
    graphNetwork.canvas.body.container.title = '';
  });

  // ---- Focus mode helpers ----
  function applyFocusMode(instPath) {
    var targetId = instToNodeId[instPath];
    if (!targetId) return;
    var connectedEdges = graphNetwork.getConnectedEdges(targetId);
    var neighborNodes = new Set();
    neighborNodes.add(targetId);
    connectedEdges.forEach(function(eid) {
      var e = graphEdges.get(eid);
      if (e) { neighborNodes.add(e.from); neighborNodes.add(e.to); }
    });
    // Also include port nodes of expanded modules in the neighbor set
    expandedModules.forEach(function(expInst) {
      var expId = instToNodeId[expInst];
      if (neighborNodes.has(expId)) {
        graphNodes.forEach(function(n) {
          if (n.parentModule === expInst) neighborNodes.add(n.id);
        });
      }
    });
    // Dim non-neighbors
    var updates = [];
    graphNodes.forEach(function(n) {
      if (neighborNodes.has(n.id)) {
        updates.push({ id: n.id, opacity: 1.0 });
      } else {
        updates.push({ id: n.id, opacity: 0.15 });
      }
    });
    graphNodes.update(updates);
    var edgeUpdates = [];
    var connSet = new Set(connectedEdges);
    graphEdges.forEach(function(e) {
      if (connSet.has(e.id)) {
        edgeUpdates.push({ id: e.id, hidden: false });
      } else {
        edgeUpdates.push({ id: e.id, hidden: true });
      }
    });
    graphEdges.update(edgeUpdates);
  }

  function clearFocusMode() {
    focusedModule = null;
    var updates = [];
    graphNodes.forEach(function(n) {
      updates.push({ id: n.id, opacity: 1.0 });
    });
    graphNodes.update(updates);
    var edgeUpdates = [];
    graphEdges.forEach(function(e) {
      edgeUpdates.push({ id: e.id, hidden: false });
    });
    graphEdges.update(edgeUpdates);
  }

  // ---- Port expansion (Level 3) ----
  function expandModule(instPath) {
    var md = graphModuleData[instPath];
    if (!md) return;
    expandedModules.add(instPath);
    var modNodeId = instToNodeId[instPath];
    // Hide module node
    graphNodes.update({ id: modNodeId, hidden: true });
    // Get module node position for layout
    var pos = graphNetwork.getPositions([modNodeId]);
    var mx = pos[modNodeId] ? pos[modNodeId].x : 0;
    var my = pos[modNodeId] ? pos[modNodeId].y : 0;
    // Split ports into inputs/outputs
    var inputs = [];
    var outputs = [];
    Object.keys(md.ports).forEach(function(pn) {
      var p = md.ports[pn];
      if (p.dir === 'input') inputs.push(pn); else outputs.push(pn);
    });
    inputs.sort();
    outputs.sort();
    var totalPorts = inputs.length + outputs.length;
    var spacing = 40;
    // Add port nodes
    var portNodes = [];
    function portColor(instPath, portName) {
      var pdata = md.ports[portName];
      var pkey = instPath + '::' + portName.replace(/\[.*/, '');
      var sev = portIssueSev[pkey];
      if (sev === 'ERROR') return { bg: '#3d1a1a', border: '#e74c3c' };
      if (sev === 'WARN') return { bg: '#3d3a1a', border: '#e97c00' };
      if (pdata && pdata.connections.length > 0) return { bg: '#1a3d2e', border: '#2ecc71' };
      return { bg: '#1e1e2e', border: '#555' };
    }
    inputs.forEach(function(pn, idx) {
      var pc = portColor(instPath, pn);
      var cleanName = pn.replace(/\[.*/, '');
      var w = parseWidth(pn);
      var pid = 'port_' + instPath + '_' + cleanName;
      var yOff = (idx - (inputs.length - 1) / 2) * spacing;
      graphNodes.add({
        id: pid, label: cleanName + ' [' + w + 'b]',
        shape: 'box', group: 'port_in',
        color: { background: pc.bg, border: pc.border,
          highlight: { background: '#1a3a5c', border: '#e94560' } },
        font: { color: '#c0c0c0', size: 11 },
        size: 12, borderWidth: 1, widthConstraint: { minimum: 60 },
        x: mx - 80, y: my + yOff, fixed: { x: true, y: true },
        parentModule: instPath
      });
      portNodes.push(pid);
    });
    outputs.forEach(function(pn, idx) {
      var pc = portColor(instPath, pn);
      var cleanName = pn.replace(/\[.*/, '');
      var w = parseWidth(pn);
      var pid = 'port_' + instPath + '_' + cleanName;
      var yOff = (idx - (outputs.length - 1) / 2) * spacing;
      graphNodes.add({
        id: pid, label: cleanName + ' [' + w + 'b]',
        shape: 'box', group: 'port_out',
        color: { background: pc.bg, border: pc.border,
          highlight: { background: '#1a3a5c', border: '#e94560' } },
        font: { color: '#c0c0c0', size: 11 },
        size: 12, borderWidth: 1, widthConstraint: { minimum: 60 },
        x: mx + 80, y: my + yOff, fixed: { x: true, y: true },
        parentModule: instPath
      });
      portNodes.push(pid);
    });

    // Replace module-level edges with port-level edges
    var edgesToHide = [];
    graphEdges.forEach(function(e) {
      if (e.from === modNodeId || e.to === modNodeId) {
        edgesToHide.push(e.id);
      }
    });
    edgesToHide.forEach(function(eid) {
      graphEdges.update({ id: eid, hidden: true });
    });

    // Add port-level edges
    DATA.connections.forEach(function(c) {
      var sParts = c.source.split('.');
      var dParts = c.dest.split('.');
      var sInst = sParts.slice(0, -1).join('.');
      var dInst = dParts.slice(0, -1).join('.');
      var sPort = sParts[sParts.length - 1].replace(/\[.*/, '');
      var dPort = dParts[dParts.length - 1].replace(/\[.*/, '');
      if (sInst !== instPath && dInst !== instPath) return;
      var fromNode, toNode;
      if (sInst === instPath) {
        fromNode = 'port_' + instPath + '_' + sPort;
        toNode = expandedModules.has(dInst) ? ('port_' + dInst + '_' + dPort) : instToNodeId[dInst];
      } else {
        fromNode = expandedModules.has(sInst) ? ('port_' + sInst + '_' + sPort) : instToNodeId[sInst];
        toNode = 'port_' + instPath + '_' + dPort;
      }
      if (!fromNode || !toNode) return;
      if (!graphNodes.get(fromNode) || !graphNodes.get(toNode)) return;
      var col = c.status !== 'OK' ? '#e97c00' : '#3a5a5a';
      var eid = 'pexp_' + sInst + '_' + sPort + '_' + dInst + '_' + dPort;
      if (!graphEdges.get(eid)) {
        graphEdges.add({
          id: eid, from: fromNode, to: toNode, arrows: 'to',
          color: { color: col, highlight: '#e94560' },
          width: 1, smooth: { type: 'cubicBezier' },
          parentModule: instPath
        });
        var ed = { from: sInst, to: dInst, signals: [{ sourcePort: sPort, destPort: dPort, status: c.status }] };
        graphEdgeData[eid] = ed;
      }
    });
    if (focusedModule) applyFocusMode(focusedModule);
  }

  function collapseModule(instPath) {
    expandedModules.delete(instPath);
    var modNodeId = instToNodeId[instPath];
    // Remove port nodes
    var toRemove = [];
    graphNodes.forEach(function(n) {
      if (n.parentModule === instPath) toRemove.push(n.id);
    });
    toRemove.forEach(function(nid) { graphNodes.remove(nid); });
    // Remove port-level edges
    var edgesToRemove = [];
    graphEdges.forEach(function(e) {
      if (e.parentModule === instPath) edgesToRemove.push(e.id);
    });
    edgesToRemove.forEach(function(eid) {
      delete graphEdgeData[eid];
      graphEdges.remove(eid);
    });
    // Restore module node
    graphNodes.update({ id: modNodeId, hidden: false });
    // Restore module-level edges
    graphEdges.forEach(function(e) {
      if (e.from === modNodeId || e.to === modNodeId) {
        var otherNode = e.from === modNodeId ? e.to : e.from;
        var otherData = graphNodes.get(otherNode);
        if (otherData && otherData.hidden) return;  // other module is expanded, keep hidden
        graphEdges.update({ id: e.id, hidden: false });
      }
    });
    if (focusedModule) applyFocusMode(focusedModule);
  }

  // ---- Info panel helpers ----
  function showModuleInfo(instPath) {
    var md = graphModuleData[instPath];
    if (!md) return;
    var panel = document.getElementById('graph-info-panel');
    var title = document.getElementById('info-title');
    var content = document.getElementById('info-content');
    title.textContent = md.short;
    var h = '';
    h += '<div class="info-row"><span class="info-label">Instance</span><span style="font-size:11px;word-break:break-all">' + esc(instPath) + '</span></div>';
    h += '<div class="info-row"><span class="info-label">Health</span><span style="color:' + scoreColor(md.health || 0) + ';font-weight:700">' + (md.health !== null ? pct(md.health) + '%' : 'N/A') + '</span></div>';
    h += '<div class="info-row"><span class="info-label">Total Ports</span><span>' + md.totalPorts + '</span></div>';
    h += '<div class="info-row"><span class="info-label">Connected</span><span>' + md.connected + '</span></div>';
    if (md.errors) h += '<div class="info-row"><span class="info-label">Errors</span><span style="color:var(--accent-red)">' + md.errors + '</span></div>';
    if (md.warnings) h += '<div class="info-row"><span class="info-label">Warnings</span><span style="color:var(--accent-orange)">' + md.warnings + '</span></div>';
    // Connected modules
    var neighbors = {};
    edgeGroups.forEach(function(g) {
      if (g.from === instPath) {
        var sn = (graphModuleData[g.to] || {}).short || g.to;
        neighbors[sn] = (neighbors[sn] || 0) + g.signals.length;
      }
      if (g.to === instPath) {
        var sn2 = (graphModuleData[g.from] || {}).short || g.from;
        neighbors[sn2] = (neighbors[sn2] || 0) + g.signals.length;
      }
    });
    var nlist = Object.keys(neighbors);
    if (nlist.length > 0) {
      h += '<div class="info-section-title">Connected Modules</div>';
      nlist.sort(function(a, b) { return neighbors[b] - neighbors[a]; });
      nlist.forEach(function(n) {
        h += '<div class="info-conn-item">' + esc(n) + ' <span style="color:var(--accent-blue)">' + neighbors[n] + ' signal' + (neighbors[n] > 1 ? 's' : '') + '</span></div>';
      });
    }
    // Issues
    if (md.issues.length > 0) {
      h += '<div class="info-section-title">Issues (' + md.issues.length + ')</div>';
      md.issues.slice(0, 10).forEach(function(iss) {
        var cls = iss.severity === 'ERROR' ? 'sev-err' : (iss.severity === 'WARN' ? 'sev-warn' : '');
        h += '<div class="info-issue-item ' + cls + '"><strong>' + iss.severity + '</strong> ' + iss.type.replace(/_/g, ' ') + '</div>';
      });
      if (md.issues.length > 10) h += '<div style="font-size:11px;color:#666;margin-top:4px">... +' + (md.issues.length - 10) + ' more</div>';
    }
    content.innerHTML = h;
    panel.style.display = 'block';
  }

  function showEdgeInfo(eid) {
    var ed = graphEdgeData[eid];
    if (!ed) return;
    var panel = document.getElementById('graph-info-panel');
    var title = document.getElementById('info-title');
    var content = document.getElementById('info-content');
    var fromShort = (graphModuleData[ed.from] || {}).short || ed.from;
    var toShort = (graphModuleData[ed.to] || {}).short || ed.to;
    title.textContent = fromShort + ' \u2192 ' + toShort;
    var h = '';
    h += '<div class="info-row"><span class="info-label">Signals</span><span>' + ed.signals.length + '</span></div>';
    h += '<div class="info-section-title">Signal Details</div>';
    ed.signals.forEach(function(s) {
      var srcClean = s.sourcePort.replace(/\[.*/, '');
      var dstClean = s.destPort.replace(/\[.*/, '');
      var stCls = s.status === 'OK' ? 'st-ok' : (s.status.indexOf('WARN') >= 0 || s.status === 'WIDTH_MISMATCH' ? 'st-warn' : 'st-err');
      h += '<div class="info-conn-item">' + esc(srcClean) + ' \u2192 ' + esc(dstClean);
      h += ' <span class="signal-status ' + stCls + '">' + s.status + '</span></div>';
    });
    content.innerHTML = h;
    panel.style.display = 'block';
  }

  // ---- Search ----
  var searchBox = document.getElementById('graph-search');
  if (searchBox) {
    searchBox.addEventListener('input', function() {
      var q = searchBox.value.trim().toLowerCase();
      if (!q) { clearFocusMode(); return; }
      var found = null;
      Object.keys(graphModuleData).forEach(function(instPath) {
        var md = graphModuleData[instPath];
        if (md.short.toLowerCase().indexOf(q) >= 0 || instPath.toLowerCase().indexOf(q) >= 0) {
          if (!found) found = instPath;
        }
      });
      if (found) {
        focusedModule = found;
        applyFocusMode(found);
        showModuleInfo(found);
        var nid = instToNodeId[found];
        if (nid) graphNetwork.focus(nid, { scale: 1.0, animation: { duration: 400, easingFunction: 'easeInOutQuad' } });
      }
    });
  }

  // ---- Reset button ----
  var resetBtn = document.getElementById('graph-reset-btn');
  if (resetBtn) {
    resetBtn.addEventListener('click', function() {
      // Collapse all expanded modules
      var toCollapse = [];
      expandedModules.forEach(function(inst) { toCollapse.push(inst); });
      toCollapse.forEach(function(inst) { collapseModule(inst); });
      clearFocusMode();
      document.getElementById('graph-info-panel').style.display = 'none';
      if (searchBox) searchBox.value = '';
      graphNetwork.fit({ animation: { duration: 400, easingFunction: 'easeInOutQuad' } });
    });
  }

  // ---- Close info panel button ----
  var closeBtn = document.getElementById('info-close-btn');
  if (closeBtn) {
    closeBtn.addEventListener('click', function() {
      document.getElementById('graph-info-panel').style.display = 'none';
    });
  }
}

// =============================================
//   TAB 3: HEATMAP (Hierarchical)
// =============================================
(function() {
  var pane = document.getElementById('pane-heatmap');
  var tooltip = document.getElementById('hm-tooltip');
  var topMod = DATA.top;

  if (!analysis || !DATA.connections || DATA.connections.length === 0) {
    pane.innerHTML = '<div class="no-data">No coupling data available for heatmap.</div>';
    return;
  }

  // Parse all connections into (srcInst, dstInst) pairs
  var connPairs = [];
  DATA.connections.forEach(function(c) {
    var sp = c.source.replace(/\[.*?\]/g, '').split('.');
    var dp = c.dest.replace(/\[.*?\]/g, '').split('.');
    var sInst = sp.slice(0, -1).join('.');
    var dInst = dp.slice(0, -1).join('.');
    if (sInst && dInst) connPairs.push({s: sInst, d: dInst, src: c.source, dst: c.dest, status: c.status});
  });

  // Build hierarchy tree: parent -> Set of direct children
  var childrenOf = {};
  var allPaths = new Set();
  connPairs.forEach(function(p) { allPaths.add(p.s); allPaths.add(p.d); });
  if (analysis.module_health) analysis.module_health.forEach(function(m) { allPaths.add(m.instance); });

  allPaths.forEach(function(path) {
    var parts = path.split('.');
    for (var i = 1; i < parts.length; i++) {
      var parent = parts.slice(0, i).join('.');
      var child = parts.slice(0, i + 1).join('.');
      if (!childrenOf[parent]) childrenOf[parent] = new Set();
      childrenOf[parent].add(child);
    }
  });

  function getChildren(scope) {
    return childrenOf[scope] ? Array.from(childrenOf[scope]).sort() : [];
  }

  function hasChildren(scope) {
    return childrenOf[scope] && childrenOf[scope].size > 0;
  }

  function isDescendant(path, scope) {
    return path === scope || path.indexOf(scope + '.') === 0;
  }

  function shortName(fullPath) {
    var parts = fullPath.split('.');
    return parts[parts.length - 1];
  }

  // Count connections between two sub-trees
  function countBetween(scopeA, scopeB) {
    var count = 0;
    connPairs.forEach(function(p) {
      if ((isDescendant(p.s, scopeA) && isDescendant(p.d, scopeB)) ||
          (isDescendant(p.s, scopeB) && isDescendant(p.d, scopeA))) count++;
    });
    return count;
  }

  // Build issue lookup by port path for quick detail access
  var issueByPort = {};
  DATA.issues.forEach(function(iss) {
    var key = iss.port;
    if (!issueByPort[key]) issueByPort[key] = [];
    issueByPort[key].push(iss);
  });

  // Find signals between two sub-trees (with issue details)
  function signalsBetween(scopeA, scopeB) {
    var sigs = [];
    connPairs.forEach(function(p) {
      if ((isDescendant(p.s, scopeA) && isDescendant(p.d, scopeB)) ||
          (isDescendant(p.s, scopeB) && isDescendant(p.d, scopeA))) {
        var sp = p.src.replace(/\[.*?\]/g, '').split('.').pop();
        var dp = p.dst.replace(/\[.*?\]/g, '').split('.').pop();
        var detail = '';
        var severity = '';
        // Look up issue detail for this connection
        var srcPath = p.src.replace(/\[.*?\]/g, '');
        var dstPath = p.dst.replace(/\[.*?\]/g, '');
        [srcPath, dstPath].forEach(function(pp) {
          if (issueByPort[pp]) issueByPort[pp].forEach(function(iss) {
            if (!detail && p.status !== 'OK') { detail = iss.detail; severity = iss.severity; }
          });
        });
        sigs.push({src: sp, dst: dp, status: p.status, detail: detail, severity: severity, srcFull: p.src, dstFull: p.dst});
      }
    });
    return sigs;
  }

  // Check if any issues exist between two sub-trees
  function hasIssuesBetween(scopeA, scopeB) {
    var found = {errors: 0, warns: 0};
    connPairs.forEach(function(p) {
      if (p.status === 'OK') return;
      if ((isDescendant(p.s, scopeA) && isDescendant(p.d, scopeB)) ||
          (isDescendant(p.s, scopeB) && isDescendant(p.d, scopeA))) {
        // Check severity from issues
        var srcPath = p.src.replace(/\[.*?\]/g, '');
        [srcPath].forEach(function(pp) {
          if (issueByPort[pp]) issueByPort[pp].forEach(function(iss) {
            if (iss.severity === 'ERROR') found.errors++;
            else if (iss.severity === 'WARN') found.warns++;
          });
        });
        if (found.errors === 0 && found.warns === 0) found.warns++; // status not OK but no specific issue found
      }
    });
    return found;
  }

  // Health score lookup
  var healthMap = {};
  if (analysis.module_health) analysis.module_health.forEach(function(m) { healthMap[m.instance] = m.score; });

  function getHealth(scope) {
    if (healthMap[scope] !== undefined) return healthMap[scope];
    var children = getChildren(scope);
    if (children.length === 0) return 1.0;
    var sum = 0;
    children.forEach(function(c) { sum += getHealth(c); });
    return sum / children.length;
  }

  // Current scope for drill-down
  var currentScope = topMod;

  function renderHeatmap(scope) {
    currentScope = scope;
    var children = getChildren(scope);
    if (children.length === 0) {
      pane.innerHTML = '<div class="no-data">No sub-modules at this level.</div>';
      return;
    }
    var n = children.length;

    // Resolve instance to its direct child of current scope
    var childIdx = {};
    for (var ci2 = 0; ci2 < n; ci2++) childIdx[children[ci2]] = ci2;

    function resolveToChild(instPath) {
      for (var k = 0; k < n; k++) {
        if (isDescendant(instPath, children[k])) return k;
      }
      return -1;
    }

    // Single-pass O(C) matrix build + issue detection
    var matrix = [];
    var issueMatrix = []; // {errors, warns} per cell
    for (var i = 0; i < n; i++) {
      matrix[i] = [];
      issueMatrix[i] = [];
      for (var j = 0; j < n; j++) { matrix[i][j] = 0; issueMatrix[i][j] = {errors:0, warns:0}; }
    }
    connPairs.forEach(function(p) {
      var si = resolveToChild(p.s);
      var di = resolveToChild(p.d);
      if (si < 0 || di < 0) return;
      matrix[si][di]++;
      if (si !== di) matrix[di][si]++;
      if (p.status !== 'OK') {
        var srcPath = p.src.replace(/\[.*?\]/g, '');
        var isErr = false;
        if (issueByPort[srcPath]) issueByPort[srcPath].forEach(function(iss) {
          if (iss.severity === 'ERROR') isErr = true;
        });
        if (isErr) { issueMatrix[si][di].errors++; if (si !== di) issueMatrix[di][si].errors++; }
        else { issueMatrix[si][di].warns++; if (si !== di) issueMatrix[di][si].warns++; }
      }
    });
    // Fix self-connection double counting
    var maxVal = 0;
    for (var i = 0; i < n; i++) {
      matrix[i][i] = Math.floor(matrix[i][i] / 2);
      for (var j = 0; j < n; j++) maxVal = Math.max(maxVal, matrix[i][j]);
    }

    // Breadcrumb
    var crumbs = [];
    var parts = scope.split('.');
    for (var bi = 0; bi < parts.length; bi++) {
      crumbs.push({name: parts[bi], path: parts.slice(0, bi + 1).join('.')});
    }

    var h = '<div class="heatmap-wrapper">';
    h += '<div style="display:flex;align-items:center;gap:8px;margin-bottom:14px;flex-wrap:wrap">';
    h += '<span style="font-size:12px;color:var(--text-secondary)">Scope:</span>';
    crumbs.forEach(function(cr, idx) {
      if (idx > 0) h += '<span style="color:var(--text-secondary);font-size:11px">\u25B6</span>';
      var isLast = idx === crumbs.length - 1;
      if (isLast) {
        h += '<span style="font-size:13px;font-weight:600;color:var(--accent-blue)">' + esc(cr.name) + '</span>';
      } else {
        h += '<span class="hm-crumb" data-scope="' + esc(cr.path) + '" style="font-size:13px;color:var(--accent-blue);cursor:pointer;text-decoration:underline">' + esc(cr.name) + '</span>';
      }
    });
    h += '</div>';

    h += '<div class="heatmap-title">Double-click a module label to drill down \u00B7 Click a cell for connection details</div>';
    h += '<table class="hm-table">';

    // Header row
    h += '<tr><th class="hm-ylbl"></th>';
    children.forEach(function(c) {
      var sn = shortName(c);
      var drillIcon = hasChildren(c) ? ' \u25BC' : '';
      h += '<th class="hm-xlbl" title="' + esc(c) + '">' + esc(sn) + drillIcon + '</th>';
    });
    h += '</tr>';

    // Data rows
    for (var ri = 0; ri < n; ri++) {
      h += '<tr>';
      var sn = shortName(children[ri]);
      var drillIcon = hasChildren(children[ri]) ? ' \u25B6' : '';
      var hlth = getHealth(children[ri]);
      var lblColor = hlth > 0.8 ? 'var(--accent-green)' : hlth > 0.6 ? 'var(--accent-yellow)' : 'var(--accent-red)';
      h += '<th class="hm-ylbl hm-ylabel" data-path="' + esc(children[ri]) + '" title="' + esc(children[ri]) + ' (score: ' + Math.round(hlth * 100) + '%)" style="color:' + lblColor + ';cursor:' + (hasChildren(children[ri]) ? 'pointer' : 'default') + '">' + esc(sn) + drillIcon + '</th>';

      for (var ci = 0; ci < n; ci++) {
        var val = matrix[ri][ci];
        var isSelf = (ri === ci);
        var opacity = maxVal > 0 ? (val / maxVal) : 0;
        var bg;
        if (val === 0) {
          bg = isSelf ? '#1a1a2e' : '#0d1b2a';
        } else if (isSelf) {
          var pr = Math.round(80 + opacity * 75);
          var pg = Math.round(40 + opacity * 60);
          var pb = Math.round(120 + opacity * 80);
          bg = 'rgb(' + pr + ',' + pg + ',' + pb + ')';
        } else {
          var cr = Math.round(52 + opacity * 52);
          var cg = Math.round(30 + opacity * 122);
          var cb = Math.round(60 + opacity * 159);
          bg = 'rgb(' + cr + ',' + cg + ',' + cb + ')';
        }
        var border = isSelf ? 'border:1px solid rgba(155,89,182,0.4)' : '';
        var issueInfo = (val > 0 && !isSelf) ? issueMatrix[ri][ci] : {errors:0, warns:0};
        var cellIcon = '';
        if (issueInfo.errors > 0) cellIcon = '<span style="position:absolute;top:1px;right:2px;font-size:8px;color:#e74c3c">\u25CF</span>';
        else if (issueInfo.warns > 0) cellIcon = '<span style="position:absolute;top:1px;right:2px;font-size:8px;color:#f39c12">\u25CF</span>';
        var cellStyle = 'background:' + bg + ';' + border + (cellIcon ? ';position:relative' : '');
        h += '<td class="heatmap-cell" data-src="' + esc(children[ri]) + '" data-dst="' + esc(children[ci]) + '" data-val="' + val + '" data-self="' + (isSelf ? '1' : '0') + '" style="' + cellStyle + '">' + (val > 0 ? val : '') + cellIcon + '</td>';
      }
      h += '</tr>';
    }
    h += '</table>';

    // Legend
    h += '<div style="display:flex;gap:24px;margin-top:14px;flex-wrap:wrap">';
    h += '<div class="heatmap-legend"><span>0</span>';
    h += '<div class="heatmap-legend-bar" style="background:linear-gradient(to right, #0d1b2a, #3498db)"></div>';
    h += '<span>' + maxVal + '</span> cross-module</div>';
    h += '<div class="heatmap-legend"><span>0</span>';
    h += '<div class="heatmap-legend-bar" style="background:linear-gradient(to right, #1a1a2e, #9b59b6)"></div>';
    h += '<span>' + maxVal + '</span> internal (self)</div>';
    h += '</div>';

    h += '</div>';
    pane.innerHTML = h;

    // Breadcrumb click
    pane.querySelectorAll('.hm-crumb').forEach(function(el) {
      el.addEventListener('click', function() { renderHeatmap(el.dataset.scope); });
    });

    // Y-label double-click for drill-down
    pane.querySelectorAll('.hm-ylabel').forEach(function(el) {
      el.addEventListener('dblclick', function() {
        var path = el.dataset.path;
        if (hasChildren(path)) renderHeatmap(path);
      });
    });

    // Cell tooltip
    pane.querySelectorAll('.heatmap-cell').forEach(function(cell) {
      cell.addEventListener('mouseenter', function(e) {
        var val = parseInt(cell.dataset.val);
        if (val === 0) { tooltip.style.display = 'none'; return; }
        var isSelf = cell.dataset.self === '1';
        var label = isSelf ? '<em>internal connections</em>' : esc(shortName(cell.dataset.src)) + ' \u2194 ' + esc(shortName(cell.dataset.dst));
        tooltip.innerHTML = '<strong>' + label + '</strong><br>' + val + ' connection' + (val > 1 ? 's' : '');
        tooltip.style.display = 'block';
        tooltip.style.left = (e.clientX + 12) + 'px';
        tooltip.style.top = (e.clientY - 10) + 'px';
      });
      cell.addEventListener('mousemove', function(e) {
        tooltip.style.left = (e.clientX + 12) + 'px';
        tooltip.style.top = (e.clientY - 10) + 'px';
      });
      cell.addEventListener('mouseleave', function() { tooltip.style.display = 'none'; });

      // Cell click: show signal details
      cell.addEventListener('click', function() {
        var srcScope = cell.dataset.src;
        var dstScope = cell.dataset.dst;
        var val = parseInt(cell.dataset.val);
        if (val === 0) return;
        var isSelf = cell.dataset.self === '1';
        var signals = signalsBetween(srcScope, dstScope);
        var title = isSelf ? esc(shortName(srcScope)) + ' internal' : esc(shortName(srcScope)) + ' \u2194 ' + esc(shortName(dstScope));

        var mh = '<div style="position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,0.6);z-index:200;display:flex;align-items:center;justify-content:center" onclick="this.remove()">';
        mh += '<div style="background:var(--bg-secondary);border:1px solid var(--border);border-radius:10px;padding:24px;max-width:550px;width:90%;max-height:70vh;overflow-y:auto" onclick="event.stopPropagation()">';
        mh += '<div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:16px">';
        mh += '<h3 style="margin:0;font-size:16px">' + title + '</h3>';
        mh += '<span style="cursor:pointer;font-size:20px;color:var(--text-secondary)" onclick="this.closest(\'div[style*=fixed]\').remove()">\u2715</span></div>';
        if (isSelf) mh += '<div style="font-size:12px;color:#9b59b6;margin-bottom:8px">\u25C6 Internal connections within sub-hierarchy</div>';
        mh += '<div style="font-size:12px;color:var(--text-secondary);margin-bottom:12px">' + signals.length + ' signal(s)</div>';
        mh += '<table style="width:100%;border-collapse:collapse;font-size:13px">';
        mh += '<tr style="border-bottom:1px solid var(--border)"><th style="text-align:left;padding:6px;color:var(--accent-blue)">Source</th><th style="text-align:left;padding:6px;color:var(--accent-blue)">Dest</th><th style="text-align:left;padding:6px;color:var(--accent-blue)">Status</th></tr>';
        signals.forEach(function(s, idx) {
          var sColor = s.status === 'OK' ? 'var(--accent-green)' : 'var(--accent-red)';
          var rowCursor = (s.status !== 'OK' && s.detail) ? 'cursor:pointer' : '';
          var detailId = 'hm-detail-' + idx;
          mh += '<tr class="hm-sig-row" style="border-bottom:1px solid rgba(255,255,255,0.05);' + rowCursor + '">';
          mh += '<td style="padding:6px">' + esc(s.src) + '</td>';
          mh += '<td style="padding:6px">' + esc(s.dst) + '</td>';
          mh += '<td style="padding:6px;color:' + sColor + '">' + esc(s.status);
          if (s.status !== 'OK') mh += ' <span style="font-size:10px;opacity:0.6">\u25BC</span>';
          mh += '</td></tr>';
          if (s.detail) {
            mh += '<tr id="' + detailId + '" style="display:none"><td colspan="3" style="padding:8px 6px 12px 16px;font-size:12px">';
            mh += '<div style="background:var(--bg-primary);border-left:3px solid ' + sColor + ';padding:8px 12px;border-radius:0 4px 4px 0">';
            mh += '<div style="color:' + sColor + ';font-weight:600;margin-bottom:4px">' + esc(s.status) + '</div>';
            mh += '<div style="color:var(--text-secondary)">' + esc(s.detail) + '</div>';
            mh += '<div style="color:var(--text-secondary);margin-top:4px;font-size:11px;opacity:0.7">' + esc(s.srcFull || '') + ' \u2192 ' + esc(s.dstFull || '') + '</div>';
            mh += '</div></td></tr>';
          }
        });
        mh += '</table></div></div>';
        var ov = document.createElement('div');
        ov.innerHTML = mh;
        var modalEl = ov.firstChild;
        document.body.appendChild(modalEl);
        // Wire up click-to-expand for issue rows (after DOM insertion)
        modalEl.querySelectorAll('.hm-sig-row').forEach(function(row, i) {
          row.addEventListener('click', function() {
            var d = modalEl.querySelector('#hm-detail-' + i);
            if (d) d.style.display = d.style.display === 'none' ? 'table-row' : 'none';
          });
        });
        // Escape key to close modal
        var onEsc = function(e) {
          if (e.key === 'Escape') { modalEl.remove(); document.removeEventListener('keydown', onEsc); }
        };
        document.addEventListener('keydown', onEsc);
      });
    });
  }

  renderHeatmap(topMod);
})();

// =============================================
//   TAB 4: DETAILS
// =============================================
(function() {
  var pane = document.getElementById('pane-details');
  if (!analysis || analysis.module_health.length === 0) {
    pane.innerHTML = '<div class="no-data">No module details available.</div>';
    return;
  }

  // Build port info per instance from connections
  var portMap = {}; // instance -> { portName -> { connected: bool, issues: [] } }
  DATA.connections.forEach(function(c) {
    addPort(c.source, true, c.status);
    addPort(c.dest, true, c.status);
  });
  DATA.issues.forEach(function(iss) {
    // Map issues to their instance
    var parts = iss.port.split('.');
    var inst = parts.slice(0, -1).join('.');
    var pn = parts[parts.length - 1];
    if (!portMap[inst]) portMap[inst] = {};
    if (!portMap[inst][pn]) portMap[inst][pn] = { connected: false, issues: [] };
    portMap[inst][pn].issues.push(iss);
    // Also try source/dest instances
    if (iss.source) addIssueToInst(iss.source.instance, iss);
    if (iss.dest) addIssueToInst(iss.dest.instance, iss);
  });

  function addPort(fullPath, connected, status) {
    var parts = fullPath.replace(/\[.*/, '').split('.');
    var inst = parts.slice(0, -1).join('.');
    var pn = parts[parts.length - 1];
    if (!portMap[inst]) portMap[inst] = {};
    if (!portMap[inst][pn]) portMap[inst][pn] = { connected: false, issues: [] };
    if (connected) portMap[inst][pn].connected = true;
  }
  function addIssueToInst(inst, iss) {
    if (!portMap[inst]) portMap[inst] = {};
  }

  // Build issue lookup per instance
  var issuesByInstance = {};
  DATA.issues.forEach(function(iss) {
    var parts = iss.port.split('.');
    var inst = parts.slice(0, -1).join('.');
    if (!issuesByInstance[inst]) issuesByInstance[inst] = [];
    issuesByInstance[inst].push(iss);
  });

  var sorted = analysis.module_health.slice().sort(function(a, b) { return a.score - b.score; });
  var h = '<div class="details-wrapper">';

  sorted.forEach(function(m) {
    var sc = scoreColor(m.score);
    var inst = m.instance;
    h += '<div class="module-card" data-instance="' + esc(inst) + '">';
    h += '<div class="module-card-header">';
    h += '<div class="module-card-chevron">&#9654;</div>';
    h += '<div class="module-card-name">' + esc(m.name) + '</div>';
    h += '<div class="module-card-instance">' + esc(inst) + '</div>';
    h += '<div class="module-card-score-bar"><div class="module-card-score-fill" style="width:' + pct(m.score) + '%;background:' + sc + '"></div></div>';
    h += '<div class="module-card-score-text" style="color:' + sc + '">' + pct(m.score) + '%</div>';
    h += '</div>'; // header

    h += '<div class="module-card-body">';
    h += '<div style="display:flex;gap:20px;flex-wrap:wrap;margin-bottom:8px;">';
    h += '<span style="font-size:12px;color:var(--text-secondary)">Ports: <strong style="color:var(--text-primary)">' + m.total_ports + '</strong></span>';
    h += '<span style="font-size:12px;color:var(--text-secondary)">Connected: <strong style="color:var(--accent-green)">' + m.connected + '</strong></span>';
    if (m.errors) h += '<span style="font-size:12px;color:var(--accent-red)">Errors: <strong>' + m.errors + '</strong></span>';
    if (m.warnings) h += '<span style="font-size:12px;color:var(--accent-orange)">Warnings: <strong>' + m.warnings + '</strong></span>';
    h += '</div>';

    // Port table
    var ports = portMap[inst];
    if (ports && Object.keys(ports).length > 0) {
      h += '<table class="port-table"><thead><tr><th></th><th>Port</th><th>Status</th></tr></thead><tbody>';
      Object.keys(ports).sort().forEach(function(pn) {
        var p = ports[pn];
        var icon, cls, statusText;
        if (p.issues.length > 0) {
          var hasErr = p.issues.some(function(i) { return i.severity === 'ERROR'; });
          if (hasErr) { icon = '&#10007;'; cls = 'port-err'; statusText = 'Error'; }
          else { icon = '&#9888;'; cls = 'port-warn'; statusText = 'Warning'; }
        } else if (p.connected) {
          icon = '&#10003;'; cls = 'port-ok'; statusText = 'Connected';
        } else {
          icon = '&#8212;'; cls = 'port-unc'; statusText = 'Unconnected';
        }
        h += '<tr><td><span class="port-status-icon ' + cls + '">' + icon + '</span></td>';
        h += '<td>' + esc(pn) + '</td><td style="color:var(--text-secondary)">' + statusText + '</td></tr>';
      });
      h += '</tbody></table>';
    }

    // Issues for this module
    var modIssues = issuesByInstance[inst];
    if (modIssues && modIssues.length > 0) {
      h += '<div class="detail-issues"><h4>Issues (' + modIssues.length + ')</h4>';
      modIssues.forEach(function(iss) {
        h += '<div class="detail-issue-item sev-' + iss.severity + '">';
        h += '<strong>' + iss.severity + '</strong> ' + iss.type.replace(/_/g, ' ') + ' &mdash; ' + esc(iss.detail);
        h += '</div>';
      });
      h += '</div>';
    }

    h += '</div>'; // body
    h += '</div>'; // module-card
  });

  h += '</div>';
  pane.innerHTML = h;

  // Toggle expand/collapse
  pane.querySelectorAll('.module-card-header').forEach(function(hdr) {
    hdr.addEventListener('click', function() {
      hdr.parentElement.classList.toggle('open');
    });
  });
})();

// Init graph if it's the active tab on load (it's not, but just in case)
if (document.querySelector('.tab-btn.active').dataset.tab === 'graph') { initGraph(); }
</script>
</body>
</html>
)HTMLTPL";

} // namespace connect
