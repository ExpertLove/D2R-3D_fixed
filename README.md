# D2R Third Person — Experimental Beta

Perspective third-person camera, camera-relative movement and explicit target selection for **Diablo II: Resurrected through D2RLoader**.

**Version: 1.1.0-experimental.23.** This is a loader plugin, not a standalone injector. Not affiliated with or endorsed by Blizzard or D2RLoader.

[Building](docs/BUILDING.md) · [Known limitations](docs/KNOWN_ISSUES.md) · [Changelog](CHANGELOG.md) · [Credits and permissions](THIRD_PARTY_NOTICES.md)

## Choose a download

| Edition | Included plugins | Recommended for |
|---|---|---|
| **Standard** | `d2rl-3dcam.dll` | Normal draw distance; lowest additional overhead |
| **Extended** | The same camera DLL plus `d2rl-renderdistance.dll` | Optional increased draw distance, controlled from the settings panel |

Both editions have identical camera, movement, targeting and key-remapping features. The distance plugin is **optional**, not a camera requirement. Both plugins' sources live in this repository.

**Extended starts with increased distance OFF on first installation.** The distance switch remembers your choice between launches. **F12 controls the camera only**; it never toggles the updated distance plugin.

Increased distance can cause stuttering, higher memory use and lower FPS. Turn it off if needed. Pools allocated after enabling remain enlarged until the game exits; turning the option off is not guaranteed to immediately recover all memory or FPS. Standard avoids loading the distance plugin altogether.

## Features

- Perspective camera, continuous mouse look and wheel zoom.
- Eight-way WASD relative to the camera; hold Shift for a free cursor.
- Camera-aligned native Tab automap.
- Explicit next/previous, elite and screen-center enemy selection.
- Animated seal under the selected enemy, with the native name/health panel.
- In-game settings gear, physical-key remapping, sensitivity and selection-radius controls.
- Extended only: persisted render-distance switch, independent of the camera.

**No permanent crosshair, camera lock, automatic turning or autoattacks.** Manual attacks can aim at the selected target; selecting a target does not attack it.

## Compatibility and safety

Tested configuration: **Windows x64, D2R EXE 3.2.92777, D2RLoader 1.3.1-beta**, SDK 0.3.0 / plugin ABI 4. Other versions are not guaranteed. Signature checks reject known mismatches but are not a universal safety guarantee.

Use this beta for **offline testing** with a disposable character and backed-up saves. Battle.net compatibility, anti-cheat acceptance and account safety are **not guaranteed**. These plugins do not edit character saves. Neither the game nor the loader is included.

## Install or upgrade

1. Fully close the game and D2RLoader. Never replace a loaded DLL.
2. Install a compatible D2RLoader separately.
3. Back up existing plugin DLLs and their INI files **outside** `d2rloader/plugins/`.
4. Copy the DLL(s) from your chosen archive into `<game>/d2rloader/plugins/`.
   - **Standard:** install `d2rl-3dcam.dll`. Remove any existing `d2rl-renderdistance.dll` from this directory if you want a genuinely camera-only installation.
   - **Extended:** install **both matching .23 DLLs**. Replace the older distance DLL; do not keep it under a second filename. The old distance plugin has a different F12 behavior and does not support this panel service.
5. Launch through D2RLoader and enter an offline game.
6. Bind the game's native movement actions to **W/A/S/D**. Press **F12** for 3D, then **F10** for mouse look.

Do not place backup DLLs in the plugins directory: they may be loaded as extra plugins. Existing settings are preserved during an upgrade. Therefore, if distance was previously saved ON, Extended will restore ON rather than reset your preference.

## Open settings and select a target

1. While 3D is enabled, find the small red/gold gear at the **right edge, about 38% down the screen**.
2. In mouse-look mode, hold **Shift** and click the gear. Release Shift after opening; the panel keeps the cursor free.
3. Set **TARGETING: ON**. Targeting starts disabled until enabled by you.
4. Close with X or Escape. Press **V** when an enemy is near the screen center. A miss preserves your current live target.

The panel suspends movement/look and consumes new mouse-button and wheel actions, including clicks outside the panel. **The game does not pause. Configure controls in town.** Opening the panel can clear the selected target. Escape first cancels a pending key assignment; otherwise it closes. Alt/Windows/focus loss release control safely, and F10 may need re-enabling afterwards.

### Extended distance

When the matching distance plugin is present, the panel shows **DISTANCE: OFF/ON** next to the target-seal control. The switch is absent in Standard.

- Click to request ON/OFF. `WAIT ON/OFF` means the request awaits a native client update.
- The distance plugin applies the change on the game update path and saves it in `d2rloader/plugins/renderdistance-settings.ini`.
- `N/A` means required native guards failed or a runtime fault disabled the feature. Do not force it; inspect the distance plugin log.
- `UNSAVED` means a setting was applied but writing the INI failed. Make the plugin directory writable before relying on persistence.
- Disabling restores the normal visibility radius/depth through native hooks; additional rooms retire on subsequent updates. Already enlarged memory pools are retained until exit.
- `DEFAULTS` resets **camera-panel settings**, not the separately owned render-distance preference. Use the DISTANCE switch to change it.

The optional console command `renderdist` uses the same queued, persisted setting. No console command is required for normal panel use.

## Default controls

| Input | Action |
|---|---|
| F12 | Toggle 3D camera only |
| F10 | Toggle continuous mouse look |
| F9 | Toggle camera-relative WASD |
| Hold Shift | Temporarily free the cursor |
| Mouse wheel | Zoom |
| Middle-button drag | Orbit outside F10 mode |
| Tab | Native automap |
| F / G | Next / previous eligible enemy |
| X | Cycle elite enemies |
| V | Select an enemy near the center |
| Shift + left-click on an enemy | Select without attacking |
| Escape / Alt | Release control; Escape also closes settings |

F/G/X/V require **TARGETING + 3D + F10**. Held keys do not repeatedly cycle targets.

Click a key field, release modifiers and press a new key. Bindings use physical keyboard positions regardless of input language. **Single keys only**, not Ctrl/Alt/Shift chords. WASD, Shift and native UI safety keys are reserved. Internal duplicates are rejected; conflicts with the game's or other plugins' bindings are not detected.

Camera settings are saved automatically to `d2rloader/plugins/3dcam-settings.ini`. Missing/invalid bindings fall back to defaults. Sensitivity 100% preserves the base camera behavior. Default center-selection radius is 9% of viewport height, adjustable from 3% to 18%.

Optional camera console commands: `3dcam`, `clickaim`, `mapfollow`. Direct camera console toggles are session-local; TARGETING/MAP FOLLOW changes through the panel persist. Distance has its own persisted state and never depends on the camera's F12 toggle.

## Remove, switch editions or roll back

Close the game. Remove the relevant DLL(s) or restore matching backups. To switch Extended to Standard, remove the distance DLL; its INI may be kept so reinstalling Extended restores your preference. Delete `renderdistance-settings.ini` only if you want distance to start OFF again. Delete `3dcam-settings.ini` only to reset camera settings. Do not delete character saves.

## Validation and bug reports

Thirteen automated test suites cover selection, input policies, native-adapter mocks, geometry, settings, distance persistence and the optional inter-plugin service. The previous .22 camera received basic local gameplay acceptance. **The .23 distance integration still needs wider gameplay, restart, performance and edition-switching validation.** CI configuration is provided, but a remote GitHub Actions run is not claimed until it actually runs.

When reporting a bug, include plugin/game/loader versions, edition, resolution/DPI, other plugins, reproduction steps, screenshot and a sanitized excerpt of `d2rloader/logs/d2r-3d-3dcam.log` or `d2r-3d-renderdistance.log`. **Do not upload savegames, game binaries, memory dumps, passwords or tokens.**

## Credits

Based on [emmericp/D2R-3D](https://github.com/emmericp/D2R-3D), original camera/distance attribution **Tandanu**. SDK: [D2RLoader/PluginSDK](https://github.com/D2RLoader/PluginSDK). This branch adds camera-relative controls, selection, settings and optional distance integration. See [third-party notices](THIRD_PARTY_NOTICES.md) for component permissions; the SDK's MIT license is not a blanket license for all source or artwork.
