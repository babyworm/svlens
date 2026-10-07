# User-defined name checkers

`svlens conn --user-rules checkers.yaml` registers name rules without
rebuilding the analyzer. The current targets are `module`, `instance`,
`signal` (internal nets and variables), and `port`.

```yaml
checkers:
  - id: USR-001
    description: Module names must end in _ip
    target: module
    pattern: ".*_ip"
    severity: error
  - id: USR-002
    description: Temporary signals are forbidden
    target: signal
    forbidden: "tmp_.*"
    severity: warning
```

`pattern` requires the entire name to match; `forbidden` reports when the
entire name matches. Each rule needs exactly one of these ECMAScript regex
fields, a unique `id`, a description, a target, and `error`, `warning`, or
`info` severity. Invalid schemas and regexes fail the conn run visibly.
Findings use type `CONVENTION` and include `rule_id` in JSON. A conn waiver
can match that ID through its `type` field, for example `type: USR-001`.
Conn also accepts a source comment directly above or on a declaration:

```systemverilog
// svlens: waive USR-002 reason: reviewed local exception
logic tmp_debug;
```

The `reason:` text is required. Connection-specific findings such as
`WIDTH_MISMATCH` additionally require `path=<full.issue.path>` in the
directive so one declaration does not suppress unrelated instances.

This registers name-pattern checkers. Arbitrary C++/Python AST plugins are
outside the current extension surface.
