# Homebrew packaging status

Homebrew distribution is **intentionally deferred** until release archives and
tagged-version URLs are stable.

Phase 3 documents the validation path instead of shipping a live formula:

1. produce a tagged release archive
2. compute and publish SHA256
3. add a tap formula pointing at that archive
4. validate on macOS CI with `brew install`

The current source-of-truth document is [`docs/release.md`](../../docs/release.md).
