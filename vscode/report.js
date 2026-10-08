const path = require('node:path');

function locatedIssues(report, workspaceRoot) {
  const grouped = new Map();
  let withoutLocation = 0;
  for (const issue of report.issues || []) {
    if (!issue.file || !Number.isInteger(issue.line) || issue.line < 1) {
      withoutLocation += 1;
      continue;
    }
    const filename = path.resolve(workspaceRoot, issue.file);
    const relative = path.relative(workspaceRoot, filename);
    if (relative === '..' || relative.startsWith('..' + path.sep)
        || path.isAbsolute(relative)) {
      withoutLocation += 1;
      continue;
    }
    const items = grouped.get(filename) || [];
    items.push({
      line: issue.line - 1,
      column: Math.max(0, (issue.column || 1) - 1),
      severity: issue.severity,
      rule: issue.type,
      message: issue.detail || issue.type,
    });
    grouped.set(filename, items);
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

module.exports = {locatedIssues, locatedCrossings};
