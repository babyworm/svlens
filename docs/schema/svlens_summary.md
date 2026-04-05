# svlens_summary.json schema contract (stable-now)

`svlens_summary.json` is the stable summary artifact for `svlens both`.
Phase 1B freezes the following fields.

## Top-level keys
- `mode`
- `top`
- `conn_format`
- `cdc_format`
- `explicit_output`
- `used_filelist`
- `conn_exit_code`
- `cdc_exit_code`
- `exit_code`
- `source_file_count`
- `conn_status`
- `cdc_status`
- `filelists`
- `source_files`
- `outputs`
- `reports`

## `outputs`
- `conn`
- `cdc`
- `conn_dir`
- `cdc_dir`

## `reports`
- `connect_report`
- `cdc_report`

## Notes
- The `both` exit code is `max(conn_exit_code, cdc_exit_code)`.
- Future analytical-trust fields must be explicitly versioned or documented before they become stable.
