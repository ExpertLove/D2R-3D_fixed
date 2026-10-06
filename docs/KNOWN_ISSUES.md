# Known limitations / известные ограничения

- Experimental, version-specific native hooks; only D2R EXE3.2.92777 + D2RLoader1.3.1-beta was exercised. Other versions can reject hooks or behave differently. No online/anti-cheat/ban-safety claim.
- Relative WASD uses eight native directions, not analog360-degree movement. Native game movement must be bound to WASD.
- Shift temporarily frees the cursor. Default game UI shortcuts release mouse look; custom game bindings may require manually releasing F10 first. The plugin does not enumerate every possible native UI panel.
- Targeting is opt-in. No camera tracking, automatic attacking or permanent crosshair. Manual attacks may aim at the selected monster.
- Center selection approximates monster body height as player ground+4 render units. Terrain line-of-sight is from the player, not GPU depth visibility from the camera. Slopes, height differences and partially occluded enemies can produce imperfect selection.
- The seal is a screen-projected ground marker, not a depth-tested terrain decal. Ground height and interpolation remain approximate. Earlier drift/map distortion received fixes but long-session stability is not exhaustively verified.
- The settings panel does not pause gameplay. Opening it can clear selection. Its close/focus/stale-frame paths are designed to release control safely, but input combinations and alt-tab should be tested in town.
- English UI labels, single physical-key bindings only. WASD/Shift/UI safety keys reserved; other plugins' and the game's bindings are not checked. Console toggles are session-local; TARGETING/MAP FOLLOW toggles from the panel persist.
- If renderdistance is separately installed, F12 may still toggle it. Rebinding the camera does not rebind renderdistance. The distance DLL is not shipped in the standard archive.
- Cog pixels are rendered as native solid-quad rows instead of a new unverified texture API. The gear uses1004 quads, an open panel3016 quads (capacity4096). Visual fidelity and FPS overhead vary; report regressions. The cog is derived from original loader artwork with its surrounding frame removed.
- CPU geometry previews and successful native upload counters are not proof of on-screen pixels. .20 had reversed settings-rectangle winding despite passing geometry bounds checks; .21 corrected order and added a regression test. The .22 user reported basic operation, not exhaustive validation.
- Config file must be writable beside the DLL. `SAVE FAILED` means settings changed for this session but were not persisted. Missing or invalid binding config falls back to defaults.
- Failure to install target UI hooks disables targeting rather than silently selecting without its required UI support. Check the plugin log for hook failures.

## Minimal manual acceptance checklist

- Enter offline test game; F12/F10, WASD, Shift and Tab behave correctly.
- Gear and panel appear and match click coordinates at your resolution/DPI.
- Panel clicks/wheel do not attack or move; held movement releases correctly; Escape/X/Alt-Tab release safely.
- V selects only an eligible enemy near center; miss preserves a live selection. F/G/X and Shift-click remain functional.
- Assign an unused key, reject duplicates/reserved keys, restart and confirm persistence, then test DEFAULTS.
- Check FPS with panel open/closed, several teleports, slopes and a longer session.
