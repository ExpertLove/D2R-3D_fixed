# Third-party notices and permission status

This is a derivative project, not an original implementation of every bundled component. No blanket MIT/Apache license is being asserted over upstream code or artwork.

## Original camera / render-distance code

- Project: https://github.com/emmericp/D2R-3D
- Source revision: `1eddc2214d22716752d21eccd333b23052dab774`
- Original attribution: Tandanu / emmericp, retained in plugin metadata.
- That source checkout did not include a root LICENSE file. The repository owner states that redistribution is permitted. This notice records that representation; it does not fabricate a license grant from the original author or independently verify its scope. Retain applicable author permissions with any distribution.

## D2RLoader PluginSDK

- Project: https://github.com/D2RLoader/PluginSDK
- Revision: `717f727a0ec52912d1558764345f8fa3453a2bd6`
- Copyright (c)2026 D2RLoader contributors.
- MIT license: full text in `plugin/third_party/sdk/LICENSE` (binary archives include `licenses/PluginSDK-MIT.txt`).
- The SDK MIT license applies to the SDK, not automatically to loader artwork or upstream camera code.

## Settings cog artwork

- Original asset: D2RLoader `data/hd/global/ui/frontend/hd/final/frontend_d2rloader_cog.sprite`.
- Normal frame; outer bevel removed, central64x64 crop resized to32x32 for the plugin's native UI renderer.
- `plugin/loader_gear_pixels.h` contains derived pixels, not an independently drawn icon.
- Included on the repository owner's statement that redistribution permission exists. Artwork remains attributed to its original owner; the exact permission text was not supplied with this source export.

## Game and build dependencies

Diablo II: Resurrected and its game assets belong to their respective rights holders. No game binaries, savegames or game archive are included. This project is not affiliated with or endorsed by Blizzard or D2RLoader.

Bazel dependencies are fetched according to `MODULE.bazel` and its lockfile and retain their respective licenses. They are not copied into this source export.

No permission is granted here to relicense third-party material. Obtain the underlying permission terms from the project maintainer where required.
