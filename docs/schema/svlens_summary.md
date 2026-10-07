# svlens_summary.json schema contract (stable-now)

`svlens_summary.json` is written by `svlens all` (`svlens both` is a
backward-compatible alias). It records dispatch results and artifact paths for
`conn`, `cdc`, and `metrics`.

## Top-level keys
- `mode`
- `top`
- `conn_format`
- `cdc_format`
- `explicit_output`
- `used_filelist`
- `conn_exit_code`
- `cdc_exit_code`
- `metrics_exit_code`
- `exit_code`
- `source_file_count`
- `conn_status`
- `cdc_status`
- `metrics_status`
- `filelists`
- `source_files`
- `cross_reference_status`
- `cross_references`
- `outputs`
- `reports`

## `outputs`
- `conn`
- `cdc`
- `metrics`
- `conn_dir`
- `cdc_dir`
- `metrics_dir`

## `reports`
- `connect_report`
- `cdc_report`
- `metrics_report`

## `cross_references[*]`

- `conn_issue_index` -- zero-based index in `connect_report.json:issues[]`
- `cdc_crossing_id` -- ID in `cdc_report.json:crossings[]`
- `matched_signal` -- exact signal path present in both records

`cross_reference_status` is `matched`, `no_exact_match`, or `unavailable`.
The last value means one of the JSON reports was absent or unreadable. The
array never uses module-prefix or name-similarity guesses.

## Notes
- `mode` is `"all"`, including when invoked through the `both` alias.
- `exit_code` is `max(conn_exit_code, cdc_exit_code, metrics_exit_code)`.
- The report and output fields are paths. Exact-path links between conn issues
  and CDC crossings are included when both JSON reports are available; metrics
  cones are not correlated yet.
- New analytical fields must be documented before they are treated as stable.
