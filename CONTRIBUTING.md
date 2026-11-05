# Contributing

Source review, testing and development contributions are welcome under the existing GPL-2.0-only license.

Use the [build instructions](docs/BUILDING.md). For a proposed change, provide a reproducer, the affected chip and DMA mode, tests, executable identity and ABI/import differences. Distinguish host tests from physical and receiver-side validation.

Preserve the cumulative chip profiles, both DMA paths, ownership, BME/OWN ordering, synchronization, safe-stop and software fallbacks. Work in a separate copy; do not overwrite the accepted image or remove identity guards to enable an unsupported feature.

Credits: [CONTRIBUTORS.md](CONTRIBUTORS.md). Component licenses: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
