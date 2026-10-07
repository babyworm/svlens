# svlens for VS Code

Open the `vscode/` directory as a local extension during development. Set
`svlens.binary` to the built executable, `svlens.top` to the elaboration top,
and optionally `svlens.filelist` to a workspace-relative filelist. Run
**svlens: Run Connectivity** from the Command Palette to show source-backed
issues as editor diagnostics. **svlens: Run CDC** shows source-backed crossing
diagnostics and writes a crossing summary to the svlens Output channel.

The extension has no npm runtime dependencies. It uses the CLI JSON reports.
Findings without a file/line are not assigned an invented editor position.
Marketplace publishing requires a publisher account and is not configured here.
