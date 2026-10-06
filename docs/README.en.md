# D2R Third Person — experimental beta

D2RLoader plugin for perspective third-person camera, mouse look, camera-relative WASD, heading-up native automap, explicit target selection and an in-game settings panel.

**1.1.0-experimental.22. Tested configuration only:** Windows x64, D2R EXE3.2.92777, D2RLoader1.3.1-beta, SDK0.3.0 / plugin ABI4. Offline testing only; other versions, Battle.net compatibility and account safety are not guaranteed. The plugin does not edit character save files. Back up your saves and use a test character.

## Install

Close the game and loader. Back up any old camera DLL outside the plugins folder. Copy `d2rl-3dcam.dll` to `<game>/d2rloader/plugins/`, then launch through D2RLoader. No game, loader, standalone injector or render-distance DLL is included.

Enter a game, press F12 for3D, F10 for mouse look. Bind native movement to W/A/S/D first. Hold Shift for a free cursor; wheel zooms, middle-button drag rotates outside F10. Tab opens the normal automap.

The small red/gold gear appears at the right edge, about38% down the screen. Shift-click it during mouse look. Release Shift after opening. Enable **TARGETING**, close with X or Escape, then use:

- F/G: next/previous eligible target;
- X: elite cycle;
- V: enemy within a small circle around the screen center; miss preserves a live current selection;
- Shift+left-click: select hostile without attacking.

Targeting requires3D+F10+TARGETING. No autoattacks, camera lock or permanent crosshair. Manual attacks can aim at your chosen target. F9 toggles relative movement.

## Settings

Click a key field and press a single physical key with Shift released. Duplicates and reserved movement/UI keys are rejected; conflicts with native game/other-plugin bindings are not detected. F/G/X/V/F9/F10/F12 can be rebound. Sensitivity, center radius and seal controls are available. Settings are saved beside the DLL as `3dcam-settings.ini`; DEFAULTS restores defaults.

The panel keeps the cursor free and suspends movement/look, but **does not pause the game**. Opening it can clear selection. Escape cancels a pending binding first, otherwise closes. Alt/Windows/focus loss release control safely; F10 may need re-enabling.

Console commands: `3dcam`, `clickaim`, `mapfollow`. Console toggles are session-only; corresponding panel toggles persist. If you separately use renderdistance, its F12 is independent and is not rebound by this plugin.

## Uninstall / report issues

Close the game; remove the DLL or restore your backup. Optionally delete its INI. Do not remove character saves.

See [known issues](KNOWN_ISSUES.md), [build instructions](BUILDING.md), and [third-party notices](../THIRD_PARTY_NOTICES.md). Include versions, resolution/DPI, reproduction steps and a sanitized plugin log when reporting bugs. Do not upload game binaries, saves or memory dumps.

Based on [emmericp/D2R-3D](https://github.com/emmericp/D2R-3D), original camera attribution Tandanu; SDK from [D2RLoader/PluginSDK](https://github.com/D2RLoader/PluginSDK). Not affiliated with Blizzard or D2RLoader. Twelve automated suites passed and the local tester reported basic operation; this is not exhaustive stability/performance validation.
