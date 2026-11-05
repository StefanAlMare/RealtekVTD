# Development

This repository is private until the owner explicitly authorizes public access. As a repository-maintenance policy, do not make it public, create a public mirror or publish private captures as an automatic development step. This workflow policy is not an additional restriction on the rights granted by GPLv2.

The beta reference is closed at B. New development needs an explicit objective or a concrete problem. Keep original source manifests and accepted binaries intact; work in a separate copy or branch and assign a new identity to any changed native image.

Extend the cumulative driver rather than replacing it with an older hardware-specific fork. Preserve all existing profiles, mapper/native64 paths, DMA/BME/OWN safety, feature policies and fallbacks. Changes in eligibility require proof of the actual hardware and DMA prerequisites. Never fabricate MapperReady, force NoIOMMU globally, return Retry after ownership has transferred, or treat an unconfirmed stop as permission to release DMA memory.

Start with `docs/BUILDING.md`. Rebuilding is an engineering operation and does not inherit the original binary's physical acceptance. The source checkout provides a native build-only entrypoint; the owner's full historical regression closure is separate. Do not describe a compilation-only run as the full regression matrix.

For a proposed functional change, record a minimal reproducer, before/after expectations, affected profiles, source and binary hashes, ABI/import differences, ownership/error/lifecycle checks and precise limits of host versus physical validation. Keep payloads, addresses, serials and other personal capture data out of issue examples unless explicitly authorized and necessary.

GPL-2.0-only and separate firmware/SDK notices apply to contributions and redistributions. See `THIRD_PARTY_NOTICES.md`.
