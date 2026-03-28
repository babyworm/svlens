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

/* --- Heatmap tab --- */
.heatmap-wrapper { padding: 24px; overflow: auto; height: 100%; }
.heatmap-title { font-size: 14px; color: var(--text-secondary); margin-bottom: 14px; }
.heatmap-grid-container { display: inline-block; }
.heatmap-row { display: flex; }
.heatmap-label-y { width: 120px; text-align: right; padding-right: 8px; font-size: 11px; color: var(--text-secondary); display: flex; align-items: center; justify-content: flex-end; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; flex-shrink: 0; }
.heatmap-label-x { font-size: 11px; color: var(--text-secondary); text-align: center; writing-mode: vertical-lr; transform: rotate(180deg); padding-bottom: 6px; height: 100px; display: flex; align-items: center; justify-content: flex-end; overflow: hidden; }
.heatmap-cell { width: 36px; height: 36px; margin: 1px; border-radius: 3px; cursor: pointer; transition: transform 0.1s, box-shadow 0.15s; display: flex; align-items: center; justify-content: center; font-size: 10px; color: transparent; flex-shrink: 0; }
.heatmap-cell:hover { transform: scale(1.15); box-shadow: 0 0 8px rgba(52,152,219,0.5); color: #fff; z-index: 1; }
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
  <div class="tab-pane" id="pane-graph"><div id="graph-container"></div></div>
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
//   TAB 2: GRAPH (vis-network)
// =============================================
function initGraph() {
  var instances = new Map();
  DATA.connections.forEach(function(c) {
    var sParts = c.source.split('.');
    var dParts = c.dest.split('.');
    var sInst = sParts.slice(0, -1).join('.');
    var dInst = dParts.slice(0, -1).join('.');
    var sShort = sInst.split('.').pop();
    var dShort = dInst.split('.').pop();
    if (!instances.has(sInst)) instances.set(sInst, sShort);
    if (!instances.has(dInst)) instances.set(dInst, dShort);
  });

  // Build health lookup
  var healthMap = {};
  if (analysis) {
    analysis.module_health.forEach(function(m) { healthMap[m.instance] = m.score; });
  }

  var nodes = new vis.DataSet();
  var nodeId = 0;
  var instToId = new Map();
  instances.forEach(function(label, path) {
    instToId.set(path, nodeId);
    var s = healthMap[path];
    var bgCol = '#16213e';
    var borderCol = '#0f3460';
    if (typeof s === 'number') {
      if (s > 0.8) { bgCol = '#1a3d2e'; borderCol = '#2ecc71'; }
      else if (s > 0.6) { bgCol = '#3d3a1a'; borderCol = '#f1c40f'; }
      else { bgCol = '#3d1a1a'; borderCol = '#e74c3c'; }
    }
    nodes.add({ id: nodeId, label: label, shape: 'box',
      color: { background: bgCol, border: borderCol, highlight: { background: '#1a3a5c', border: '#e94560' } },
      font: { color: '#e0e0e0', size: 14 } });
    nodeId++;
  });

  // Build severity map
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

  // Group edges
  var edgeGroups = new Map();
  DATA.connections.forEach(function(c) {
    var sParts = c.source.split('.');
    var dParts = c.dest.split('.');
    var sInst = sParts.slice(0, -1).join('.');
    var dInst = dParts.slice(0, -1).join('.');
    var key = sInst + '->' + dInst;
    if (!edgeGroups.has(key)) edgeGroups.set(key, { from: sInst, to: dInst, labels: [], status: 'OK' });
    var g = edgeGroups.get(key);
    var sPort = sParts[sParts.length - 1].replace(/\[.*/, '');
    var dPort = dParts[dParts.length - 1].replace(/\[.*/, '');
    g.labels.push(sPort + ' -> ' + dPort + (c.status !== 'OK' ? ' [' + c.status + ']' : ''));
    if (c.status !== 'OK') g.status = c.status;
  });

  connSev.forEach(function(sev, key) {
    var g = edgeGroups.get(key);
    if (g) g.severity = sev;
  });

  var sevColor = { ERROR: '#e94560', WARN: '#e97c00', INFO: '#4a90d9' };
  var edges = new vis.DataSet();
  edgeGroups.forEach(function(g) {
    var fromId = instToId.get(g.from);
    var toId = instToId.get(g.to);
    if (fromId === undefined || toId === undefined) return;
    var sev = g.severity;
    var col = sev ? (sevColor[sev] || '#3a3a5a') : '#3a3a5a';
    edges.add({ from: fromId, to: toId, arrows: 'to',
      label: g.labels.join('\n'), font: { color: '#888', size: 10, multi: 'html' },
      color: { color: col, highlight: '#e94560' }, width: sev === 'ERROR' ? 2 : 1 });
  });

  var container = document.getElementById('graph-container');
  new vis.Network(container, { nodes: nodes, edges: edges }, {
    layout: { hierarchical: { direction: 'LR', sortMethod: 'directed', levelSeparation: 220, nodeSpacing: 100 } },
    physics: false,
    edges: { smooth: { type: 'cubicBezier' } },
    interaction: { hover: true, tooltipDelay: 100 }
  });
}

// =============================================
//   TAB 3: HEATMAP
// =============================================
(function() {
  var pane = document.getElementById('pane-heatmap');
  if (!analysis || !analysis.coupling || analysis.coupling.length === 0) {
    pane.innerHTML = '<div class="no-data">No coupling data available for heatmap.</div>';
    return;
  }

  // Collect unique module names
  var nameSet = {};
  analysis.module_health.forEach(function(m) { nameSet[m.name] = true; });
  analysis.coupling.forEach(function(c) { nameSet[c.source] = true; nameSet[c.dest] = true; });
  var modules = Object.keys(nameSet).sort();
  var n = modules.length;
  var modIdx = {};
  modules.forEach(function(m, i) { modIdx[m] = i; });

  // Build matrix
  var matrix = [];
  for (var i = 0; i < n; i++) { matrix[i] = []; for (var j = 0; j < n; j++) matrix[i][j] = 0; }
  var maxVal = 0;
  analysis.coupling.forEach(function(c) {
    var si = modIdx[c.source];
    var di = modIdx[c.dest];
    if (si !== undefined && di !== undefined) {
      matrix[si][di] += c.connections;
      matrix[di][si] += c.connections;
      maxVal = Math.max(maxVal, matrix[si][di], matrix[di][si]);
    }
  });

  var h = '<div class="heatmap-wrapper">';
  h += '<div class="heatmap-title">Module-to-Module Connection Heatmap (hover for count, click for details)</div>';
  h += '<div class="heatmap-grid-container">';

  // Header row with X labels
  h += '<div class="heatmap-row"><div class="heatmap-label-y"></div>';
  modules.forEach(function(m) {
    h += '<div class="heatmap-label-x" title="' + esc(m) + '">' + esc(m) + '</div>';
  });
  h += '</div>';

  // Data rows
  for (var ri = 0; ri < n; ri++) {
    h += '<div class="heatmap-row">';
    h += '<div class="heatmap-label-y" title="' + esc(modules[ri]) + '">' + esc(modules[ri]) + '</div>';
    for (var ci = 0; ci < n; ci++) {
      var val = matrix[ri][ci];
      var opacity = maxVal > 0 ? (val / maxVal) : 0;
      var bg;
      if (val === 0) {
        bg = '#0d1b2a';
      } else {
        var r = Math.round(52 + opacity * (52));
        var g = Math.round(30 + opacity * (122));
        var b = Math.round(60 + opacity * (159));
        bg = 'rgb(' + r + ',' + g + ',' + b + ')';
      }
      h += '<div class="heatmap-cell" data-src="' + esc(modules[ri]) + '" data-dst="' + esc(modules[ci]) + '" data-val="' + val + '" style="background:' + bg + '">' + (val > 0 ? val : '') + '</div>';
    }
    h += '</div>';
  }

  h += '</div>'; // grid-container

  // Legend
  h += '<div class="heatmap-legend"><span>0</span>';
  h += '<div class="heatmap-legend-bar" style="background:linear-gradient(to right, #0d1b2a, #3498db)"></div>';
  h += '<span>' + maxVal + '</span> connections</div>';

  h += '</div>'; // heatmap-wrapper
  pane.innerHTML = h;

  // Tooltip
  var tooltip = document.getElementById('hm-tooltip');
  pane.querySelectorAll('.heatmap-cell').forEach(function(cell) {
    cell.addEventListener('mouseenter', function(e) {
      var val = cell.dataset.val;
      if (parseInt(val) === 0) { tooltip.style.display = 'none'; return; }
      tooltip.innerHTML = '<strong>' + esc(cell.dataset.src) + ' &harr; ' + esc(cell.dataset.dst) + '</strong><br>' + val + ' connections';
      tooltip.style.display = 'block';
      tooltip.style.left = (e.clientX + 12) + 'px';
      tooltip.style.top = (e.clientY - 10) + 'px';
    });
    cell.addEventListener('mousemove', function(e) {
      tooltip.style.left = (e.clientX + 12) + 'px';
      tooltip.style.top = (e.clientY - 10) + 'px';
    });
    cell.addEventListener('mouseleave', function() { tooltip.style.display = 'none'; });
  });
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
