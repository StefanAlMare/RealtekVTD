# Acceptance, evidence and known limitations

## Decision

**CONDITIONAL GO for a private beta using the accepted B NoIOMMU reference on the validated Dell MAC46 profile.** This is not universal production qualification. The owner explicitly selected B and closed further development/testing at that reference. No new driver code, native rebuild or physical traffic was executed for publication.

Current image: UUID `09046FEC-48A9-3EE5-9622-B7EE8F3CD90C`; executable SHA-256 `9b00c46b5e123c33eb851e922b0716019dcac3afa842488544f413347d3f2d37`.

## Physical evidence and attribution

The owner-confirmed Dell B checkpoint reports:

| Measure | Reported value |
|---|---|
| TCP TX | 926.1 Mbps |
| Retransmissions, measured window | 27 |
| Retransmissions, omitted interval | 883 — a separate interval, not folded into27 |
| EarlyFull | 1,086,006 |
| Snapshot copies | 378,814 |
| FinalFullCopies | 0 |
| Apple QueueDrops | 410 |
| Submitted / completed | 397,650 /397,650 |
| TX used / pool leased at end | 0 /0 |
| Watchdog in session | None reported |

These numbers come from the accepted project handover and the owner's subsequent final confirmation. The named B physical archive was not available for independent re-decoding during the earlier handover audit; this publication does not invent that verification. The final acceptance authority is recorded in `provenance/ACCEPTANCE-20261105.md`.

QueueDrops are Apple queue statistics with enqueue/flush semantics. They are not automatically wire losses or equal to TCP retransmissions. EarlyFull measures a precheck result, not drops or completions. Their residual cause is not assigned to DMA. This beta is not a demonstrated fix for all historical drops.

## Existing offline build evidence

The original B delivery verified its exact281-entry source manifest, all22 native translation units, pinned SDK1232-member tree, native link, **517 linked imports unchanged from A**, ABI/vtables/layout, guarded-input stability, patch replay with no fuzz and archive CRC/SHA-256.

Original result summaries record focused62/62, cumulative152/152 and collector65/65, plus the cumulative FULL/OFF/LPI/shared and native64 matrices. These are historical executed results from B's construction, not freshly rerun tests for this publication. The small image and source manifest are checked again for distribution integrity. Host/simulated boundary tests remain separate from physical hardware and independent receiver checks.

The original `provenance/IDENTITATE-AB.json` and `driver/` retain their historical text, including statements written before B's later physical acceptance. Those older pending-status statements are superseded by the owner's final decision, not rewritten as if the original report had contained later evidence.

## Safety coverage

- Existing source and regression evidence covers pre-consumption Retry, same-mbuf ownership, final submit checks, epoch changes, mapper/native64 separation, callback behavior, post-OWN refusal rules and unsafe-retention protections.
- Matching submitted/completed and empty resources in the Dell session supports successful ownership closure in that session. It does not prove every failure path has been physically exercised.
- Invalid-cookie, partial-error, unconfirmed-BME and quarantine paths primarily have offline coverage. Do not claim deliberate unsafe-stop injection on Dell.
- Historical sleep/wake success on one project machine is not universal Dell B power-state qualification.
- The ON watchdog investigation is unresolved. B's precheck explicitly excludes mapper mode. Publishing B does not repair installed Dell0.6.x binaries or prove AppleVTD ON stable.

## Release boundaries

1. The exact B image is accepted for the defined private Dell NoIOMMU scope. No claim is made that every historical profile was physically re-tested on B.
2. VLAN/Jumbo/checksum/TSO validation history remains valuable but profile/build-specific. See `HARDWARE.md` and `CAPABILITIES.md`.
3. Independent receiver-side checksum and segmentation qualification is not established just by driver counters or host emitters.
4. Residual QueueDrops/retransmissions and intermittent MAC46 ON stalls remain documented limitations. No speculative follow-up version is created to hide them.
5. No tests, benchmark repetitions or new captures are requested as a prerequisite for this accepted beta publication.

Development resumes only for an explicit objective or a concrete reported problem. Original checkpoints and rollback images remain intact locally.
