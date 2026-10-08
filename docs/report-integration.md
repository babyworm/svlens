# SARIF and PR summaries

`scripts/report_convert.py` converts existing JSON reports without rerunning
analysis or posting to GitHub:

```bash
python3 scripts/report_convert.py \
  --conn reports/conn/connect_report.json \
  --cdc reports/cdc/cdc_report.json \
  --format sarif --output reports/svlens.sarif

python3 scripts/report_convert.py \
  --conn reports/conn/connect_report.json \
  --cdc reports/cdc/cdc_report.json \
  --format pr-comment --max-items 50 --output reports/pr-summary.md

python3 scripts/report_convert.py \
  --conn reports/conn/connect_report.json \
  --cdc reports/cdc/cdc_report.json \
  --format pr-comments-json --output reports/pr-comment-candidates.json
```

The converter emits SARIF 2.1.0 with stable conn/CDC rule IDs. Conn findings
use trusted port declaration locations; CDC findings use destination FF
declaration locations when available. Other findings keep logical signal
locations. GitHub code scanning needs physical source locations for inline PR
alerts, so location-free findings may appear only in the Markdown summary. The
converter never fabricates a file or line for them. These commands publish
neither comments nor SARIF; a CI workflow can use the generated files.
`pr-comments-json` emits `{path, line, side, body}` candidates for findings with
source locations. Review-comment lines must also be present in the PR diff,
which this converter does not inspect. No comment is posted automatically.
See [GitHub's review-comment API](https://docs.github.com/en/rest/pulls/comments)
for the line and diff requirements.

See the [OASIS SARIF 2.1.0 standard](https://docs.oasis-open.org/sarif/sarif/v2.1.0/os/sarif-v2.1.0-os.html)
and [GitHub's SARIF location guidance](https://docs.github.com/en/code-security/reference/code-scanning/sarif-files/sarif-support)
for consumer-specific behavior.
