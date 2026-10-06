# Changelog

## 1.1.0-experimental.23

- Two binary editions: Standard (camera) and Extended (camera plus optional distance).
- Add an Extended-only DISTANCE switch through a versioned SDK service with a retained provider lease.
- Default distance OFF on first install; persist both ON and OFF in a separate distance INI.
- Remove the distance F12 polling thread. F12 now controls only the camera.
- Queue panel/console distance intents and apply native changes during client updates; preserve newer requests and report pending/fault/save states.
- Reject enabling when required guards or pool growth fail. Track a fresh native depth baseline when the room manager changes.
- Translate all public documentation/templates to English and update edition-aware release packaging.
- Add distance state, persistence, service availability/load-order/lifetime and panel-layout tests (13 suites total).
- OFF does not reclaim already enlarged pools until exit; performance/gameplay validation remains necessary.

## 1.1.0-experimental.22

- Original D2RLoader cog artwork, outer frame removed, small red-backed settings button at right edge.
- Carries native rectangle-winding fix from .21.
- Twelve automated suites pass; basic local gameplay acceptance, not exhaustive regression coverage.

## 1.1.0-experimental.21

- Fix settings rectangles' vertex winding to match native UI emission; add winding regression test.

## 1.1.0-experimental.20

- Explicit V target selection within a small central area; no eligible enemy preserves current target.
- Native in-game settings panel and modal input handling.
- Configurable physical keys, sensitivity, center radius and target seal; INI persistence and defaults.

## Earlier experimental work

- .19: fresh paired camera snapshots for target marker; scoped native automap clipping fixes.
- .18: elite cycle on X; keep native name/HP rather than custom overhead HP.
- .16: stable F/G target cycling and selection-only Shift+left-click; animated ground seal.
- Camera-relative eight-way WASD, Shift cursor, heading-up native Tab map and manual selected-target aiming.

Development release numbers are preserved; this export does not claim a stable1.1.0 release.
