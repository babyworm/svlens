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
* { margin: 0; padding: 0; box-sizing: border-box; }
body { font-family: 'Segoe UI', system-ui, -apple-system, sans-serif; background: #1a1a2e; color: #e0e0e0; }
#header { background: #16213e; padding: 14px 24px; display: flex; align-items: center; gap: 18px; flex-wrap: wrap; border-bottom: 1px solid #0f3460; }
#header h1 { font-size: 18px; color: #e94560; white-space: nowrap; }
#header .top-name { font-size: 15px; color: #a0c4ff; font-weight: 600; }
.badge { display: inline-block; padding: 3px 10px; border-radius: 12px; font-size: 12px; font-weight: 700; margin-left: 6px; }
.badge-err { background: #e94560; color: #fff; }
.badge-warn { background: #e97c00; color: #fff; }
.badge-info { background: #4a90d9; color: #fff; }
.badge-conn { background: #2d6a4f; color: #fff; }
.badge-waived { background: #555; color: #ccc; }
#main { display: flex; height: calc(100vh - 52px); }
#graph-container { flex: 1; background: #0f0f23; }
#sidebar { width: 360px; background: #16213e; border-left: 1px solid #0f3460; display: flex; flex-direction: column; overflow: hidden; }
#filter-bar { padding: 10px 12px; display: flex; flex-wrap: wrap; gap: 6px; border-bottom: 1px solid #0f3460; }
.filter-btn { padding: 4px 10px; border-radius: 4px; border: 1px solid #334; background: #1a1a2e; color: #aaa; cursor: pointer; font-size: 11px; transition: all .15s; }
.filter-btn:hover { border-color: #e94560; color: #fff; }
.filter-btn.active { background: #e94560; color: #fff; border-color: #e94560; }
#issue-list { flex: 1; overflow-y: auto; padding: 8px; }
.issue-card { background: #1a1a2e; border-radius: 6px; padding: 10px 12px; margin-bottom: 6px; border-left: 3px solid #555; cursor: pointer; transition: background .15s; }
.issue-card:hover { background: #222244; }
.issue-card.sev-ERROR { border-left-color: #e94560; }
.issue-card.sev-WARN { border-left-color: #e97c00; }
.issue-card.sev-INFO { border-left-color: #4a90d9; }
.issue-card .issue-type { font-size: 11px; font-weight: 700; text-transform: uppercase; margin-bottom: 2px; }
.issue-card .issue-type.sev-ERROR { color: #e94560; }
.issue-card .issue-type.sev-WARN { color: #e97c00; }
.issue-card .issue-type.sev-INFO { color: #4a90d9; }
.issue-card .issue-port { font-size: 13px; color: #a0c4ff; }
.issue-card .issue-detail { font-size: 12px; color: #888; margin-top: 3px; }
#no-issues { text-align: center; color: #555; padding: 40px 20px; font-size: 14px; }
</style>
</head>
<body>
<div id="header">
  <h1>slang-connect</h1>
  <span class="top-name">{{TOP_MODULE}}</span>
  <span id="badges"></span>
</div>
<div id="main">
  <div id="graph-container"></div>
  <div id="sidebar">
    <div id="filter-bar"></div>
    <div id="issue-list"></div>
  </div>
</div>
<script>
const DATA = {{JSON_DATA}};

// --- Badges ---
(function() {
  const s = DATA.summary;
  const el = document.getElementById('badges');
  let h = '<span class="badge badge-conn">' + s.connections_analyzed + ' connections</span>';
  if (s.errors)   h += '<span class="badge badge-err">'  + s.errors   + ' errors</span>';
  if (s.warnings) h += '<span class="badge badge-warn">' + s.warnings + ' warnings</span>';
  if (s.info)     h += '<span class="badge badge-info">' + s.info     + ' info</span>';
  if (s.waived)   h += '<span class="badge badge-waived">' + s.waived + ' waived</span>';
  el.innerHTML = h;
})();

// --- Issue sidebar ---
const issueTypes = [...new Set(DATA.issues.map(i => i.type))];
let activeFilters = new Set(issueTypes);

function renderFilters() {
  const bar = document.getElementById('filter-bar');
  if (issueTypes.length === 0) { bar.style.display = 'none'; return; }
  bar.innerHTML = '<button class="filter-btn active" data-type="ALL">All</button>' +
    issueTypes.map(t => '<button class="filter-btn active" data-type="' + t + '">' + t.replace(/_/g,' ') + '</button>').join('');
  bar.querySelectorAll('.filter-btn').forEach(btn => {
    btn.addEventListener('click', function() {
      const tp = this.dataset.type;
      if (tp === 'ALL') {
        const allActive = activeFilters.size === issueTypes.length;
        if (allActive) { activeFilters.clear(); } else { activeFilters = new Set(issueTypes); }
      } else {
        if (activeFilters.has(tp)) activeFilters.delete(tp); else activeFilters.add(tp);
      }
      bar.querySelectorAll('.filter-btn').forEach(b => {
        if (b.dataset.type === 'ALL') b.classList.toggle('active', activeFilters.size === issueTypes.length);
        else b.classList.toggle('active', activeFilters.has(b.dataset.type));
      });
      renderIssues();
    });
  });
}

function renderIssues() {
  const list = document.getElementById('issue-list');
  const filtered = DATA.issues.filter(i => activeFilters.has(i.type));
  if (filtered.length === 0) {
    list.innerHTML = '<div id="no-issues">No issues to display</div>';
    return;
  }
  list.innerHTML = filtered.map((iss, idx) => {
    const sev = iss.severity;
    return '<div class="issue-card sev-' + sev + '" data-idx="' + idx + '">' +
      '<div class="issue-type sev-' + sev + '">' + sev + ' : ' + iss.type.replace(/_/g,' ') + '</div>' +
      '<div class="issue-port">' + iss.port + '</div>' +
      '<div class="issue-detail">' + iss.detail + '</div>' +
    '</div>';
  }).join('');
}

renderFilters();
renderIssues();

// --- Graph ---
(function() {
  // Build instance set and grouped edges
  const instances = new Map();
  DATA.connections.forEach(c => {
    const sParts = c.source.split('.');
    const dParts = c.dest.split('.');
    const sInst = sParts.slice(0, -1).join('.');
    const dInst = dParts.slice(0, -1).join('.');
    const sShort = sInst.split('.').pop();
    const dShort = dInst.split('.').pop();
    if (!instances.has(sInst)) instances.set(sInst, sShort);
    if (!instances.has(dInst)) instances.set(dInst, dShort);
  });

  // Nodes
  const nodes = new vis.DataSet();
  let nodeId = 0;
  const instToId = new Map();
  instances.forEach((label, path) => {
    instToId.set(path, nodeId);
    nodes.add({ id: nodeId, label: label, shape: 'box',
      color: { background: '#16213e', border: '#0f3460', highlight: { background: '#1a3a5c', border: '#e94560' } },
      font: { color: '#e0e0e0', size: 14 } });
    nodeId++;
  });

  // Build severity map for connections
  const connSev = new Map();
  DATA.issues.forEach(iss => {
    if (iss.source && iss.dest) {
      const srcInst = iss.source.instance;
      const dstInst = iss.dest.instance;
      const key = srcInst + '->' + dstInst;
      const cur = connSev.get(key) || 'OK';
      if (iss.severity === 'ERROR' || cur !== 'ERROR') connSev.set(key, iss.severity);
    }
  });

  // Group connections by instance pair
  const edgeGroups = new Map();
  DATA.connections.forEach(c => {
    const sParts = c.source.split('.');
    const dParts = c.dest.split('.');
    const sInst = sParts.slice(0, -1).join('.');
    const dInst = dParts.slice(0, -1).join('.');
    const key = sInst + '->' + dInst;
    if (!edgeGroups.has(key)) edgeGroups.set(key, { from: sInst, to: dInst, labels: [], status: 'OK' });
    const g = edgeGroups.get(key);
    const portPart = sParts.pop().replace(/\[.*/, '') + ' -> ' + dParts.pop().replace(/\[.*/, '');
    g.labels.push(portPart + (c.status !== 'OK' ? ' [' + c.status + ']' : ''));
    if (c.status !== 'OK') g.status = c.status;
  });

  // Apply issue severity to edge groups
  connSev.forEach((sev, key) => {
    const g = edgeGroups.get(key);
    if (g) g.severity = sev;
  });

  const sevColor = { ERROR: '#e94560', WARN: '#e97c00', INFO: '#4a90d9' };
  const edges = new vis.DataSet();
  edgeGroups.forEach(g => {
    const fromId = instToId.get(g.from);
    const toId = instToId.get(g.to);
    if (fromId === undefined || toId === undefined) return;
    const sev = g.severity;
    const col = sev ? (sevColor[sev] || '#3a3a5a') : '#3a3a5a';
    edges.add({ from: fromId, to: toId, arrows: 'to',
      label: g.labels.join('\n'), font: { color: '#888', size: 10, multi: 'html' },
      color: { color: col, highlight: '#e94560' }, width: sev === 'ERROR' ? 2 : 1 });
  });

  const container = document.getElementById('graph-container');
  new vis.Network(container, { nodes, edges }, {
    layout: { hierarchical: { direction: 'LR', sortMethod: 'directed', levelSeparation: 220, nodeSpacing: 100 } },
    physics: false,
    edges: { smooth: { type: 'cubicBezier' } },
    interaction: { hover: true, tooltipDelay: 100 }
  });
})();
</script>
</body>
</html>
)HTMLTPL";

} // namespace connect
