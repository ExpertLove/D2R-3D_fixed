# Changelog

## 1.2.0-experimental.24

Bug fixes:

- Targeting no longer silently stops in dense areas. More than 128 live monsters (or 2048 units including corpses/items) near the player used to discard the whole snapshot, so F/G/X/V did nothing (Cow Level, Chaos Sanctuary, large packs). The snapshot now keeps the 256 nearest monsters; the room walk is bounded at 16384 units and still fails closed on list cycles.
- The target seal no longer disappears at 1440p/4K or at close zoom. A fixed 350-pixel sanity limit was replaced by a resolution-scaled limit.
- Smooth-scrolling wheels and precision touchpads now zoom. Fractional wheel deltas used to be swallowed without any effect.
- The camera can no longer be pitched under the terrain (lower pitch limit -85 changed to -25 degrees relative to the native view).
- Lower render-thread overhead: the ~512 KB marker batch is reused instead of allocated and zeroed every frame, and the UI command queue is not copied when there is nothing to draw. The settings gear is hidden while mouse look owns the cursor (it could not be clicked then), removing about 1000 quads per frame in normal F10 play.
- Settings text uses about 45% fewer native quads (vertical run merging, pixel-identical, covered by a raster test).
- One invalid or conflicting value in `3dcam-settings.ini` no longer resets every setting to defaults; fields are repaired individually and older INI files migrate without losing anything.
- Camera slots are recycled instead of ignoring new camera objects after eight have been seen in one process.
- Targeting reports which native guard failed instead of becoming unavailable without a log line.
- The legacy (unused) health-bar geometry uses the native, non-culled vertex order.

New:

- Smooth, eased wheel zoom.
- **F11 view presets**: CLASSIC, CLOSE and TACTICAL with animated transitions.
- **Over-the-shoulder camera**: SHOULDER CENTER/RIGHT/LEFT in the settings panel.
- **Field of view** (45-90 degrees) and **INVERT Y** in the settings panel.
- Camera distance and pitch are remembered between sessions.
- **Off-screen target arrow**: when the selected enemy is outside the screen or behind the camera, a pulsing chevron at the screen edge points toward it.
- **Hotkey feedback**: short on-screen messages such as MOUSE LOOK ON/OFF, RELATIVE WASD ON/OFF, VIEW: CLOSE, NO TARGET NEARBY/IN VIEW and NO ELITE NEARBY.
- Optional **OPEN SETTINGS** hotkey (unbound by default; the same key closes the panel). **Backspace** clears a binding while assigning a key.
- Fourteen automated test suites.
- Not yet gameplay-tested; see the acceptance checklist in Known limitations.

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
