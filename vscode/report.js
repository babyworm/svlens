const path = require('node:path');

// Category display order for the CDC tree; unknown categories sort last.
const CATEGORY_ORDER = ['VIOLATION', 'CAUTION', 'CONVENTION', 'INFO', 'WAIVED'];

// Resolve a 1-based report location to a 0-based editor position inside the
// workspace, or null when the location is missing or points outside it.
function resolveLocation(file, line, column, workspaceRoot) {
  if (!file || !Number.isInteger(line) || line < 1) return null;
  const filename = path.resolve(workspaceRoot, file);
  const relative = path.relative(workspaceRoot, filename);
  if (relative === '..' || relative.startsWith('..' + path.sep)
      || path.isAbsolute(relative)) {
    return null;
  }
  return {file: filename, line: line - 1, column: Math.max(0, (column || 1) - 1)};
}

// Prefer the destination FF location; fall back to the source endpoint.
function crossingLocation(crossing, workspaceRoot) {
  const endpoint = crossing.dest_file && crossing.dest_line ? 'dest' : 'source';
  return resolveLocation(crossing[endpoint + '_file'], crossing[endpoint + '_line'],
                         crossing[endpoint + '_column'], workspaceRoot);
}

function locatedIssues(report, workspaceRoot) {
  const grouped = new Map();
  let withoutLocation = 0;
  for (const issue of report.issues || []) {
    const location = resolveLocation(issue.file, issue.line, issue.column, workspaceRoot);
    if (!location) {
      withoutLocation += 1;
      continue;
    }
    const items = grouped.get(location.file) || [];
    items.push({
      line: location.line,
      column: location.column,
      severity: issue.severity,
      rule: issue.type,
      message: issue.detail || issue.type,
    });
    grouped.set(location.file, items);
  }
  return {grouped, withoutLocation};
}

function locatedCrossings(report, workspaceRoot) {
  const issues = (report.crossings || [])
    .filter(crossing => crossing.category !== 'WAIVED')
    .map(crossing => {
      const endpoint = crossing.dest_file && crossing.dest_line ? 'dest' : 'source';
      return {
        file: crossing[endpoint + '_file'],
        line: crossing[endpoint + '_line'],
        column: crossing[endpoint + '_column'],
        severity: crossing.category === 'VIOLATION' ? 'ERROR'
          : crossing.category === 'CAUTION' ? 'WARN' : 'INFO',
        type: crossing.rule || 'CDC',
        detail: `${crossing.source} → ${crossing.dest}: ` +
          (crossing.recommendation || crossing.rationale || crossing.category),
      };
    });
  return locatedIssues({issues}, workspaceRoot);
}

// Group every CDC crossing by category for a tree view. Unlike diagnostics,
// crossings without a source location are kept (location: null) so they
// remain browsable.
function crossingTree(report, workspaceRoot) {
  const groups = new Map();
  for (const crossing of report.crossings || []) {
    const category = crossing.category || 'INFO';
    const items = groups.get(category) || [];
    items.push({
      id: crossing.id || '',
      label: `${crossing.rule || 'CDC'}: ${crossing.source} → ${crossing.dest}`,
      description: crossing.source_domain && crossing.dest_domain
        ? `${crossing.source_domain} → ${crossing.dest_domain}` : '',
      tooltip: crossing.recommendation || crossing.rationale || category,
      location: crossingLocation(crossing, workspaceRoot),
    });
    groups.set(category, items);
  }
  const rank = category => {
    const index = CATEGORY_ORDER.indexOf(category);
    return index === -1 ? CATEGORY_ORDER.length : index;
  };
  return [...groups.entries()]
    .sort(([a], [b]) => rank(a) - rank(b) || a.localeCompare(b))
    .map(([category, items]) => ({
      category,
      label: `${category} (${items.length})`,
      items,
    }));
}

module.exports = {locatedIssues, locatedCrossings, crossingTree};
