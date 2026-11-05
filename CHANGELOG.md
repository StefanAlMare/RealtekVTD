# Changelog

## v0.21.0-beta.1 — 5 November 2026

First private GitHub beta of the accepted cumulative **MAC46-N64-TXPRECHECK-B** reference.

- Packages the original B image, UUID09046FEC-48A9-3EE5-9622-B7EE8F3CD90C, unchanged and without recompilation.
- Includes its exact281-entry source tree, pinned SDK, a separate native build-only entrypoint and distribution manifests.
- Documents origins, per-chip feature limits, AppleVTD/NoIOMMU validation boundaries, licensing, manual installation and rollback in English and Romanian.
- Records the owner's final Dell acceptance and closes this development/testing stage. LPI1 is superseded as reference; TXDIAG2 is excluded.
- Publishes as a prerelease inside a private repository. No public access, automated install, hardware test or network change.

The early capacity precheck was already implemented and physically tested in B before this packaging. It is **not a new change made for this beta**, nor a demonstrated repair for Apple QueueDrops or MAC46 AppleVTD watchdogs.
