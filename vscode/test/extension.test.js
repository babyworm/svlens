const assert = require('node:assert/strict');
const Module = require('node:module');
const path = require('node:path');
const test = require('node:test');

// Minimal stand-in for the `vscode` API surface the extension touches, so the
// tree-view glue can be exercised without a running editor.
function fakeVscode() {
  const registered = {commands: new Map(), trees: new Map(), errors: []};
  class EventEmitter {
    constructor() { this.listeners = []; this.event = fn => this.listeners.push(fn); }
    fire(value) { for (const fn of this.listeners) fn(value); }
    dispose() {}
  }
  class TreeItem {
    constructor(label, collapsibleState) {
      this.label = label;
      this.collapsibleState = collapsibleState;
    }
  }
  class Range {
    constructor(startLine, startColumn, endLine, endColumn) {
      Object.assign(this, {startLine, startColumn, endLine, endColumn});
    }
  }
  const collection = () => ({clear() {}, set() {}, dispose() {}});
  const api = {
    EventEmitter, TreeItem, Range,
    TreeItemCollapsibleState: {None: 0, Collapsed: 1, Expanded: 2},
    DiagnosticSeverity: {Error: 0, Warning: 1, Information: 2},
    Uri: {file: fsPath => ({fsPath})},
    languages: {createDiagnosticCollection: collection},
    window: {
      createOutputChannel: () => ({appendLine() {}, show() {}, dispose() {}}),
      registerTreeDataProvider: (id, provider) => {
        registered.trees.set(id, provider);
        return {dispose() {}};
      },
      showErrorMessage: message => registered.errors.push(message),
      showInputBox: async () => undefined,
      activeTextEditor: {document: {uri: {fsPath: '/ws/top.sv'}}},
    },
    workspace: {
      workspaceFolders: [{uri: {fsPath: '/ws'}}],
      getConfiguration: () => ({
        get: key => ({top: 'top', binary: '/nonexistent/svlens'})[key],
      }),
    },
    commands: {
      registerCommand: (id, fn) => {
        registered.commands.set(id, fn);
        return {dispose() {}};
      },
    },
  };
  return {api, registered};
}

function loadExtension(api) {
  const extensionPath = path.join(__dirname, '..', 'extension.js');
  const originalLoad = Module._load;
  Module._load = function (request, ...rest) {
    return request === 'vscode' ? api : originalLoad.call(this, request, ...rest);
  };
  try {
    delete require.cache[require.resolve(extensionPath)];
    return require(extensionPath);
  } finally {
    Module._load = originalLoad;
  }
}

test('registers the CDC crossing view and renders grouped crossings', () => {
  const {api, registered} = fakeVscode();
  loadExtension(api).activate({subscriptions: []});
  const provider = registered.trees.get('svlens.cdcCrossings');
  assert.ok(provider, 'svlens.cdcCrossings tree provider not registered');

  const {crossingTree} = require('../report');
  provider.update(crossingTree({crossings: [
    {id: 'VIOLATION-1', category: 'VIOLATION', rule: 'Ac_cdc01',
     source: 'top.q_a', dest: 'top.q_b', dest_file: 'rtl/sync.sv', dest_line: 9},
    {id: 'CAUTION-1', category: 'CAUTION', source: 'top.c', dest: 'top.d'},
  ]}, '/ws'));

  const groups = provider.getChildren();
  assert.equal(groups.length, 2);
  const violationGroup = provider.getTreeItem(groups[0]);
  assert.equal(violationGroup.label, 'VIOLATION (1)');
  assert.equal(violationGroup.collapsibleState, api.TreeItemCollapsibleState.Expanded);
  assert.equal(provider.getTreeItem(groups[1]).collapsibleState,
               api.TreeItemCollapsibleState.Collapsed);

  const [located] = provider.getChildren(groups[0]);
  const locatedItem = provider.getTreeItem(located);
  assert.equal(locatedItem.command.command, 'vscode.open');
  assert.equal(locatedItem.command.arguments[0].fsPath, path.resolve('/ws', 'rtl/sync.sv'));
  assert.equal(locatedItem.command.arguments[1].selection.startLine, 8);

  const [unlocated] = provider.getChildren(groups[1]);
  assert.equal(provider.getTreeItem(unlocated).command, undefined);
});

test('a failed CDC run clears the crossing view', async () => {
  const {api, registered} = fakeVscode();
  loadExtension(api).activate({subscriptions: []});
  const provider = registered.trees.get('svlens.cdcCrossings');
  provider.update([{category: 'VIOLATION', label: 'VIOLATION (1)', items: []}]);

  // The configured binary does not exist, so the run fails without a report.
  await registered.commands.get('svlens.runCdc')();
  assert.deepEqual(provider.getChildren(), []);
  assert.equal(registered.errors.length, 1);
});
