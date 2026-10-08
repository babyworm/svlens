const assert = require('node:assert/strict');
const test = require('node:test');
const path = require('node:path');
const {locatedIssues, locatedCrossings} = require('../report');

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
