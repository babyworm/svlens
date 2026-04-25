# Homebrew packaging

This directory holds the Homebrew formula template for svlens.

## Files

- [`svlens.rb`](svlens.rb) -- formula template tracking the latest tagged
  release. Update `url` and `sha256` for each new release before copying it
  into the public tap.

## Release-time checklist

1. The `release-artifacts` workflow has finished and produced
   `svlens-macos-arm64.tar.gz` for the new tag.
2. Compute SHA256 against the **GitHub source tarball** (not the binary
   archive), since the formula builds from source:

   ```bash
   curl -sSL -o svlens-vX.Y.Z.tar.gz \
     https://github.com/babyworm/svlens/archive/refs/tags/vX.Y.Z.tar.gz
   shasum -a 256 svlens-vX.Y.Z.tar.gz
   ```

3. Update `svlens.rb` (this directory):
   - `url` -> the tag tarball URL above.
   - `sha256` -> the digest from step 2.
4. Copy the updated `svlens.rb` into the
   [`babyworm/homebrew-svlens`](https://github.com/babyworm/homebrew-svlens) tap
   (or whichever tap repository is canonical).
5. Validate locally:

   ```bash
   brew tap babyworm/svlens
   brew install --build-from-source svlens
   brew test svlens
   ```

6. Audit for issues:

   ```bash
   brew audit --new-formula svlens
   ```

## Tap repository layout

The tap repo (`babyworm/homebrew-svlens`) is expected to follow the standard
Homebrew tap layout:

```text
homebrew-svlens/
├── Formula/
│   └── svlens.rb     # synced from this template
└── README.md
```

End users then install with:

```bash
brew tap babyworm/svlens
brew install svlens
```

## Notes

- The formula uses `SVLENS_FETCH_DEPS=OFF` and pins slang via
  `scripts/setup-deps.sh`. If a Homebrew slang formula becomes available
  upstream, switch to a `depends_on "slang"` block and remove the setup-deps
  call from `install`.
- `head` builds (`brew install --HEAD svlens`) track the `main` branch and
  are useful for testing unreleased changes.
