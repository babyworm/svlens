const vscode = require('vscode');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {spawn} = require('node:child_process');
const {locatedIssues, locatedCrossings} = require('./report');

function runBinary(binary, args, cwd) {
  return new Promise((resolve, reject) => {
    const process = spawn(binary, args, {cwd});
    let stderr = '';
    process.stderr.on('data', chunk => { stderr += chunk.toString(); });
    process.on('error', reject);
    process.on('close', code => resolve({code, stderr}));
  });
}

function severityOf(name) {
  if (name === 'ERROR') return vscode.DiagnosticSeverity.Error;
  if (name === 'WARN') return vscode.DiagnosticSeverity.Warning;
  return vscode.DiagnosticSeverity.Information;
}

function activate(context) {
  const diagnostics = vscode.languages.createDiagnosticCollection('svlens');
  const cdcDiagnostics = vscode.languages.createDiagnosticCollection('svlens-cdc');
  const output = vscode.window.createOutputChannel('svlens');
  context.subscriptions.push(diagnostics, cdcDiagnostics, output);

  function publish(collection, grouped) {
    collection.clear();
    for (const [filename, issues] of grouped) {
      const entries = issues.map(issue => {
        const range = new vscode.Range(issue.line, issue.column,
                                       issue.line, issue.column + 1);
        const diagnostic = new vscode.Diagnostic(range, issue.message,
                                                 severityOf(issue.severity));
        diagnostic.code = issue.rule;
        diagnostic.source = 'svlens';
        return diagnostic;
      });
      collection.set(vscode.Uri.file(filename), entries);
    }
  }

  async function run(mode) {
    const folder = vscode.workspace.workspaceFolders?.[0];
    if (!folder) {
      vscode.window.showErrorMessage('Open a workspace before running svlens.');
      return;
    }
    const root = folder.uri.fsPath;
    const config = vscode.workspace.getConfiguration('svlens', folder.uri);
    const top = config.get('top') || await vscode.window.showInputBox({prompt: 'Top module'});
    if (!top) return;
    const filelist = config.get('filelist');
    const activeFile = vscode.window.activeTextEditor?.document.uri.fsPath;
    if (!filelist && !activeFile) {
      vscode.window.showErrorMessage('Set svlens.filelist or open a SystemVerilog file.');
      return;
    }
    const configuredBinary = config.get('binary') || 'svlens';
    const binary = configuredBinary.includes(path.sep)
      ? path.resolve(root, configuredBinary) : configuredBinary;
    const sources = filelist ? ['-F', path.resolve(root, filelist)] : [activeFile];
    const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'svlens-vscode-'));
    try {
      const args = [mode, '--top', top, '--format', 'json', '-o', temp, ...sources];
      const result = await runBinary(binary, args, root);
      const reportPath = path.join(temp, mode === 'conn' ? 'connect_report.json' : 'cdc_report.json');
      if (!fs.existsSync(reportPath)) {
        throw new Error(result.stderr || `svlens exited ${result.code} without a report`);
      }
      const report = JSON.parse(fs.readFileSync(reportPath, 'utf8'));
      if (mode === 'conn') {
        const {grouped, withoutLocation} = locatedIssues(report, root);
        publish(diagnostics, grouped);
        output.appendLine(`Connectivity: ${report.issues?.length || 0} issues; ` +
                          `${withoutLocation} without a source location.`);
      } else {
        const {grouped, withoutLocation} = locatedCrossings(report, root);
        publish(cdcDiagnostics, grouped);
        output.appendLine(`CDC: ${report.crossings?.length || 0} crossings. ` +
                          `${withoutLocation} without a source location.`);
        for (const crossing of report.crossings || []) {
          if (crossing.category === 'VIOLATION' || crossing.category === 'CAUTION') {
            output.appendLine(`${crossing.category} ${crossing.rule}: ` +
                              `${crossing.source} -> ${crossing.dest}`);
          }
        }
      }
      output.show(true);
    } catch (error) {
      // Drop findings from an earlier run so stale locations are not shown
      // as if they belonged to the current sources.
      (mode === 'conn' ? diagnostics : cdcDiagnostics).clear();
      vscode.window.showErrorMessage(`svlens ${mode}: ${error.message}`);
    } finally {
      fs.rmSync(temp, {recursive: true, force: true});
    }
  }

  context.subscriptions.push(
    vscode.commands.registerCommand('svlens.runConn', () => run('conn')),
    vscode.commands.registerCommand('svlens.runCdc', () => run('cdc')),
  );
}

module.exports = {activate};
