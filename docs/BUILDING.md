# Building and packaging

## Requirements

Windows x64, Visual Studio2022 C++ build tools with Windows SDK, and Bazelisk on PATH. `.bazelversion` pins the tested Bazel version. No game installation or save files are required to compile/test. Bazel downloads dependencies declared in `MODULE.bazel` and its lockfile.

From the repository root in PowerShell:

```powershell
bazelisk test //plugin/... --nocache_test_results --test_output=errors
bazelisk build //plugin:d2rl-3dcam.dll //plugin:d2rl-renderdistance.dll
```

For Git Bash, disable MSYS argument rewriting:

```bash
MSYS_NO_PATHCONV=1 bazelisk test //plugin/... --nocache_test_results --test_output=errors
```

Outputs:

- `bazel-bin/plugin/d2rl-3dcam.dll`
- `bazel-bin/plugin/d2rl-renderdistance.dll`

`//plugin/...` also builds a read-only diagnostic executable. Do not install every generated binary blindly. No standalone injector is included. Tests do not prove GPU visibility, real game performance or online safety.

## Package both editions

Python3 is required:

```powershell
python tools/package_release.py
```

The allowlisted packager checks both DLL versions and creates:

- `dist/d2r-third-person-1.1.0-experimental.23-standard-windows-x64.zip`
- `dist/d2r-third-person-1.1.0-experimental.23-extended-windows-x64.zip`
- A `.sha256` file for each archive.

Each ZIP contains installation instructions, notices, SDK license, an edition manifest and per-DLL checksums. Standard includes only the camera DLL. Extended includes the **same camera DLL** plus the updated distance DLL. No runtime INI is shipped: first installation defaults distance OFF, while existing preferences remain untouched.

Optional arguments:

```powershell
python tools/package_release.py --edition standard
python tools/package_release.py --dll-dir path/to/verified/build --edition all
```

The command refuses to overwrite an existing archive with the same version. It does not read the game directory, personal saves or local runtime configuration. Archive hash checks are integrity checks, not an authenticity signature.

For publication, use tag `v1.1.0-experimental.23`, mark the GitHub release as a **pre-release**, and attach both edition ZIPs and both checksum files. GitHub supplies source archives from the tag; a local source ZIP can also be produced with `git archive`. Review notices and archive contents before publishing.

## Inter-plugin control

`distance_control.h` defines a versioned `distance-control` service owned by plugin ID `d2r-3d-renderdistance`. The camera acquires it through the SDK's PluginCommunication service and retains a loader lease while calling it. Load-order differences are handled by retrying acquisition. The renderer only reads a copied atomic status; the input thread owns/releases the lease.

Panel/console requests update intent atomics. Native radius/pool changes run from a client DRLG-update hook, not from the keyboard hook. Ticketed requests preserve newer changes arriving while an earlier request applies. Distance owns `renderdistance-settings.ini`; camera preferences never overwrite it. Unsupported hooks or runtime faults fail closed and are reported in the panel. There is no F12 polling thread in the distance plugin.
