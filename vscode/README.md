# svlens for VS Code

Open the `vscode/` directory as a local extension during development. Set
`svlens.binary` to the built executable, `svlens.top` to the elaboration top,
and optionally `svlens.filelist` to a workspace-relative filelist. Run
**svlens: Run Connectivity** from the Command Palette to show source-backed
issues as editor diagnostics. **svlens: Run CDC** shows source-backed crossing
diagnostics, writes a crossing summary to the svlens Output channel, and fills
the **svlens CDC Crossings** Explorer view. The view groups every crossing by
category (VIOLATION, CAUTION, CONVENTION, INFO, WAIVED), including crossings
without a source location; selecting a located crossing opens its destination
FF (or source, when only that is known). A failed CDC run clears the view.

The extension has no npm runtime dependencies. It uses the CLI JSON reports.
Findings without a file/line are not assigned an invented editor position.
Marketplace publishing requires a publisher account and is not configured here.
