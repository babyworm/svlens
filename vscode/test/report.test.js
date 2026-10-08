const assert = require('node:assert/strict');
const test = require('node:test');
const path = require('node:path');
const {locatedIssues, locatedCrossings, crossingTree} = require('../report');

test('maps only source-backed issues to diagnostic positions', () => {
  const root = path.resolve('workspace');
  const report = {issues: [
    {file: 'rtl/top.sv', line: 12, column: 3, severity: 'ERROR',
     type: 'WIDTH_MISMATCH', detail: '8 to 16', port: 'top.u_a.o'},
    {severity: 'INFO', type: 'CONVENTION', detail: 'missing location'},
  ]};
  const {grouped, withoutLocation} = locatedIssues(report, root);
  assert.equal(grouped.size, 1);
  assert.equal(withoutLocation, 1);
  assert.deepEqual(grouped.get(path.join(root, 'rtl/top.sv'))[0], {
    line: 11, column: 2, severity: 'ERROR', rule: 'WIDTH_MISMATCH',
    message: '8 to 16',
  });
});

test('maps source-backed CDC crossings to their destination FF', () => {
  const root = path.resolve('workspace');
  const {grouped, withoutLocation} = locatedCrossings({crossings: [
    {dest_file: 'rtl/sync.sv', dest_line: 9, dest_column: 5,
     source: 'top.q_a', dest: 'top.q_b', category: 'VIOLATION', rule: 'Ac_cdc01'},
  ]}, root);
  assert.equal(withoutLocation, 0);
  assert.equal(grouped.get(path.join(root, 'rtl/sync.sv'))[0].line, 8);
});

test('ignores report paths outside the workspace', () => {
  const {grouped, withoutLocation} = locatedIssues({issues: [
    {file: '../outside.sv', line: 2, type: 'CONVENTION'},
  ]}, path.resolve('workspace'));
  assert.equal(grouped.size, 0);
  assert.equal(withoutLocation, 1);
});

test('groups CDC crossings by category in severity order', () => {
  const root = path.resolve('workspace');
  const tree = crossingTree({crossings: [
    {id: 'INFO-1', category: 'INFO', rule: 'Ac_cdc12', source: 'top.a', dest: 'top.b'},
    {id: 'VIOLATION-1', category: 'VIOLATION', rule: 'Ac_cdc01',
     source: 'top.q_a', dest: 'top.q_b', source_domain: 'clk_a', dest_domain: 'clk_b',
     dest_file: 'rtl/sync.sv', dest_line: 9, dest_column: 5,
     recommendation: 'use a 2-FF synchronizer'},
    {id: 'CAUTION-1', category: 'CAUTION', source: 'top.c', dest: 'top.d'},
    {id: 'VIOLATION-2', category: 'VIOLATION', rule: 'Ac_cdc01',
     source: 'top.e', dest: 'top.f'},
  ]}, root);
  assert.deepEqual(tree.map(group => group.label),
                   ['VIOLATION (2)', 'CAUTION (1)', 'INFO (1)']);
  assert.deepEqual(tree[0].items[0], {
    id: 'VIOLATION-1',
    label: 'Ac_cdc01: top.q_a → top.q_b',
    description: 'clk_a → clk_b',
    tooltip: 'use a 2-FF synchronizer',
    location: {file: path.join(root, 'rtl/sync.sv'), line: 8, column: 4},
  });
  // Location-free crossings stay browsable instead of being dropped.
  assert.equal(tree[0].items[1].location, null);
  assert.equal(tree[1].items[0].label, 'CDC: top.c → top.d');
});

test('tree locations fall back to the source and stay inside the workspace', () => {
  const root = path.resolve('workspace');
  const tree = crossingTree({crossings: [
    {category: 'CAUTION', source: 'a', dest: 'b', source_file: 'rtl/a.sv', source_line: 3},
    {category: 'CAUTION', source: 'c', dest: 'd', dest_file: '../out.sv', dest_line: 1},
  ]}, root);
  assert.deepEqual(tree[0].items[0].location,
                   {file: path.join(root, 'rtl/a.sv'), line: 2, column: 0});
  assert.equal(tree[0].items[1].location, null);
});

test('empty or missing crossings produce an empty tree', () => {
  assert.deepEqual(crossingTree({}, path.resolve('workspace')), []);
});
