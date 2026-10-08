#pragma once

namespace sv_cdccheck {

inline constexpr const char* CDC_HTML_TEMPLATE = R"SVLENSHTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>svlens CDC: {{TOP_MODULE}}</title>
<style>
  :root { color-scheme: dark; --bg: #1a1c2b; --surface: #1e2b43; --border: #2b4265;
          --text: #e7ecf5; --muted: #a5afc2; --pink: #ff667f; --blue: #55aaf1;
          --green: #38d47e; --orange: #ff9a28; }
  * { box-sizing: border-box; }
  body { margin: 0; background: var(--bg); color: var(--text);
         font: 13px/1.45 -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; }
  header { background: #1b2740; border-bottom: 1px solid var(--border);
           padding: 10px 24px; display: flex; align-items: center; gap: 18px; }
  .brand { color: var(--pink); font-size: 16px; font-weight: 750; }
  .top { color: #b9d0f5; font-weight: 650; }
  .tag { background: #28405e; border-radius: 12px; padding: 2px 10px; font-size: 11px; }
  nav { background: #1b2740; border-bottom: 1px solid var(--border); padding: 0 24px; }
  nav span { display: inline-block; padding: 11px 22px; color: var(--pink);
             border-bottom: 2px solid var(--pink); font-weight: 650; }
  main { max-width: 1600px; margin: 0 auto; padding: 24px; }
  .stats { display: grid; grid-template-columns: repeat(5, minmax(120px, 1fr)); gap: 12px; }
  .stat { background: var(--surface); border: 1px solid var(--border); border-radius: 9px;
          padding: 17px 20px; min-height: 78px; }
  .stat strong { display: block; font-size: 27px; line-height: 1.15; }
  .stat span { color: var(--muted); font-size: 11px; text-transform: uppercase; letter-spacing: .07em; }
  .stat[data-kind="violations"] strong { color: #ff755f; }
  .stat[data-kind="cautions"] strong { color: var(--orange); }
  .stat[data-kind="info"] strong { color: var(--green); }
  .stat[data-kind="domains"] strong { color: var(--blue); }
  .workspace { display: grid; grid-template-columns: 270px minmax(0, 1fr); gap: 16px; margin-top: 24px; }
  .panel { background: #1b2740; border: 1px solid var(--border); border-radius: 9px; }
  .panel h2 { margin: 0; padding: 17px 18px; font-size: 13px; letter-spacing: .08em;
              text-transform: uppercase; color: #b9c5d6; border-bottom: 1px solid var(--border); }
  .filters { padding: 16px; }
  label { display: block; color: var(--muted); font-size: 11px; font-weight: 700;
          text-transform: uppercase; letter-spacing: .06em; margin-bottom: 7px; }
  input, select { width: 100%; background: #152137; border: 1px solid #385071;
                  border-radius: 6px; color: var(--text); padding: 9px 10px;
                  margin-bottom: 16px; font: inherit; }
  input:focus, select:focus { outline: 2px solid var(--blue); outline-offset: 1px; }
  .hint { color: var(--muted); font-size: 12px; margin: 0; }
  .content { min-width: 0; display: grid; gap: 16px; }
  .table-wrap { overflow: auto; max-height: 360px; }
  table { border-collapse: collapse; width: 100%; text-align: left; }
  th, td { padding: 10px 12px; border-bottom: 1px solid #2b3853; white-space: nowrap; }
  th { position: sticky; top: 0; background: #22314d; color: #b7c4d7;
       font-size: 11px; text-transform: uppercase; letter-spacing: .05em; }
  tbody tr { cursor: pointer; }
  tbody tr:hover, tbody tr.selected { background: #2a3c59; }
  td.signal { max-width: 240px; overflow: hidden; text-overflow: ellipsis; }
  .badge { border-radius: 12px; padding: 3px 9px; font-size: 10px; font-weight: 750; }
  .badge.violation { color: #ffc0b8; background: #5b2a35; }
  .badge.caution { color: #ffd6a3; background: #594027; }
  .badge.info { color: #b5f3cf; background: #25513e; }
  .badge.waived { color: #cad0da; background: #434b5c; }
  .detail { padding: 18px; min-height: 210px; }
  .detail h3 { margin: 0 0 12px; color: #bed6fa; font-size: 15px; }
  .meta { display: flex; gap: 9px; flex-wrap: wrap; margin-bottom: 15px; }
  .meta span { background: #263958; padding: 4px 8px; border-radius: 5px; color: #c5d1e0; }
  .trace { display: flex; flex-wrap: wrap; align-items: center; gap: 8px; margin-bottom: 15px; }
  .trace .node { background: #263d5f; border: 1px solid #3d6389; color: #d4e9ff;
                 border-radius: 6px; padding: 7px 10px; overflow-wrap: anywhere; }
  .trace .arrow { color: var(--pink); font-weight: 800; }
  .detail p { color: #c5cfdd; margin: 7px 0; }
  .empty { color: var(--muted); padding: 18px; }
  @media (max-width: 900px) { .stats { grid-template-columns: repeat(2, 1fr); }
    .workspace { grid-template-columns: 1fr; } }
</style>
</head>
<body>
<header><span class="brand">svlens CDC</span><span class="top">{{TOP_MODULE}}</span>
  <span class="tag" id="crossing-count"></span></header>
<nav><span>Crossings</span></nav>
<main>
  <section class="stats" aria-label="CDC summary">
    <div class="stat" data-kind="violations"><strong id="stat-violations">0</strong><span>Violations</span></div>
    <div class="stat" data-kind="cautions"><strong id="stat-cautions">0</strong><span>Cautions</span></div>
    <div class="stat" data-kind="info"><strong id="stat-info">0</strong><span>Info</span></div>
    <div class="stat" data-kind="domains"><strong id="stat-domains">0</strong><span>Domains</span></div>
    <div class="stat"><strong id="stat-visible">0</strong><span>Visible crossings</span></div>
  </section>
  <div class="workspace">
    <aside class="panel"><h2>Explore hierarchy</h2><div class="filters">
      <label for="module-filter">Module path</label>
      <select id="module-filter"><option value="">All modules</option></select>
      <label for="category-filter">Category</label>
      <select id="category-filter"><option value="">All categories</option></select>
      <label for="search-input">Signal or rule</label>
      <input id="search-input" type="search" placeholder="Filter crossings...">
      <p class="hint">Select a crossing to inspect its signal trace, clock relationship, and recommendation.</p>
    </div></aside>
    <div class="content">
      <section class="panel"><h2>Crossing list</h2><div class="table-wrap">
        <table><thead><tr><th>Category</th><th>Rule</th><th>Source</th><th>Destination</th><th>Sync</th></tr></thead>
        <tbody id="crossing-list"></tbody></table>
      </div></section>
      <section class="panel detail" aria-live="polite" id="trace-panel">
        <h3 id="detail-title">Select a crossing</h3>
        <div class="meta" id="detail-meta"></div>
        <div class="trace" id="trace-path"></div>
        <p id="detail-rationale"></p><p id="detail-recommendation"></p>
      </section>
    </div>
  </div>
</main>
<script id="report-data" type="application/json">{{JSON_DATA}}</script>
<script>
(() => {
  const report = JSON.parse(document.getElementById('report-data').textContent);
  const crossings = report.crossings || [];
  const $ = id => document.getElementById(id);
  const moduleOf = value => {
    const dot = String(value || '').lastIndexOf('.');
    return dot < 0 ? '' : value.slice(0, dot);
  };
  const modules = [...new Set(crossings.flatMap(c => [moduleOf(c.source), moduleOf(c.dest)]))]
    .filter(Boolean).sort();
  for (const module of modules) {
    const option = document.createElement('option');
    option.value = module;
    option.textContent = module;
    $('module-filter').append(option);
  }
  for (const category of [...new Set(crossings.map(c => c.category))].sort()) {
    const option = document.createElement('option');
    option.value = category;
    option.textContent = category;
    $('category-filter').append(option);
  }
  for (const key of ['violations', 'cautions', 'info'])
    $('stat-' + key).textContent = report.summary?.[key] ?? 0;
  $('stat-domains').textContent = (report.domains || []).length;
  $('crossing-count').textContent = crossings.length + ' crossings';

  let selectedId = null;
  function showDetail(crossing) {
    selectedId = crossing.id;
    $('detail-title').textContent = crossing.id + ' · ' + crossing.category;
    const meta = $('detail-meta');
    meta.replaceChildren();
    for (const item of [crossing.rule, crossing.sync_type,
                         (crossing.source_domain || '?') + ' → ' + (crossing.dest_domain || '?'),
                         crossing.relationship].filter(Boolean)) {
      const span = document.createElement('span');
      span.textContent = item;
      meta.append(span);
    }
    const trace = $('trace-path');
    trace.replaceChildren();
    const points = [crossing.source, ...(crossing.path || []), crossing.dest].filter(Boolean);
    points.forEach((point, index) => {
      if (index) {
        const arrow = document.createElement('span');
        arrow.className = 'arrow';
        arrow.textContent = '→';
        trace.append(arrow);
      }
      const node = document.createElement('span');
      node.className = 'node';
      node.textContent = point;
      trace.append(node);
    });
    $('detail-rationale').textContent = crossing.rationale || '';
    $('detail-recommendation').textContent = crossing.recommendation || '';
    document.querySelectorAll('#crossing-list tr').forEach(row =>
      row.classList.toggle('selected', row.dataset.id === selectedId));
  }
  function render() {
    const module = $('module-filter').value;
    const category = $('category-filter').value;
    const query = $('search-input').value.trim().toLowerCase();
    const visible = crossings.filter(c => {
      const inModule = !module || [c.source, c.dest].some(path =>
        String(path || '').startsWith(module + '.'));
      const inCategory = !category || c.category === category;
      const inSearch = !query || [c.source, c.dest, c.rule, c.id]
        .some(value => String(value || '').toLowerCase().includes(query));
      return inModule && inCategory && inSearch;
    });
    $('stat-visible').textContent = visible.length;
    const list = $('crossing-list');
    list.replaceChildren();
    for (const c of visible) {
      const row = document.createElement('tr');
      row.dataset.id = c.id;
      const badge = document.createElement('span');
      badge.className = 'badge ' + c.category.toLowerCase();
      badge.textContent = c.category;
      const categoryCell = document.createElement('td');
      categoryCell.append(badge);
      row.append(categoryCell);
      for (const value of [c.rule, c.source, c.dest, c.sync_type]) {
        const cell = document.createElement('td');
        cell.className = 'signal';
        cell.title = value || '';
        cell.textContent = value || '—';
        row.append(cell);
      }
      row.addEventListener('click', () => showDetail(c));
      if (c.id === selectedId) row.classList.add('selected');
      list.append(row);
    }
    if (!visible.length) {
      const row = document.createElement('tr');
      const cell = document.createElement('td');
      cell.colSpan = 5;
      cell.className = 'empty';
      cell.textContent = 'No crossings match these filters.';
      row.append(cell);
      list.append(row);
    }
    if (visible.length && !visible.some(c => c.id === selectedId)) showDetail(visible[0]);
  }
  for (const id of ['module-filter', 'category-filter', 'search-input'])
    $(id).addEventListener(id === 'search-input' ? 'input' : 'change', render);
  render();
})();
</script>
</body>
</html>)SVLENSHTML";

} // namespace sv_cdccheck
