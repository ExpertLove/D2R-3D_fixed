# Build / сборка

Requirements: Windows x64, Visual Studio2022 C++ build tools with Windows SDK, Bazelisk on PATH. `.bazelversion` selects the tested Bazel version. No game installation or save files are required to build/test. Bazel downloads its declared dependencies on first build.

From the repository root in PowerShell:

```powershell
bazelisk test //plugin/... --nocache_test_results --test_output=errors
bazelisk build //plugin:d2rl-3dcam.dll
```

Git Bash requires disabling MSYS path rewriting:

```bash
MSYS_NO_PATHCONV=1 bazelisk test //plugin/... --nocache_test_results --test_output=errors
```

Output: `bazel-bin/plugin/d2rl-3dcam.dll`.

`//plugin/...` also builds the upstream render-distance plugin and a read-only diagnostic executable; these are **not part of the standard camera release**. Do not install every generated binary blindly.

No standalone injector is included. Run the DLL through a compatible D2RLoader, never replace a loaded DLL. Tests cover pure selection, lifetime, mock adapters, geometry, configuration and UI layout. Passing tests do not prove in-game pixel visibility, performance or multiplayer safety.

## Release packaging

```powershell
python tools/package_release.py
```

Requires Python3. It creates an allowlisted camera-only ZIP in `dist/`, checksums and a JSON manifest. It never reads the game directory, personal saves or runtime configuration. The command refuses to package a DLL unless the file exists at the build output path.

For a tag/release, use `v1.1.0-experimental.22`, mark it as a **pre-release**, and attach the generated ZIP and its `.sha256` file. Review third-party permissions and release contents before making the repository public.
