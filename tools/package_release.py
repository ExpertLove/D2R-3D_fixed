"""Allowlisted Standard/Extended archives; never reads games, saves or INI files."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def package(dll_dir, edition):
    version = (ROOT/'VERSION').read_text(encoding='utf8').strip()
    editions = ['standard', 'extended'] if edition == 'all' else [edition]
    plugins = ['d2rl-3dcam.dll']
    if 'extended' in editions:
        plugins.append('d2rl-renderdistance.dll')
    binaries = {}
    # Validate every required DLL before creating either edition.
    for name in plugins:
        data = (dll_dir/name).read_bytes()
        if data[:2] != b'MZ' or version.encode('ascii') not in data:
            raise ValueError(f'{name}: expected plugin version {version}; no archives created')
        binaries[name] = data
    docs = {
        'README.md': ROOT/'README.md',
        'CHANGELOG.md': ROOT/'CHANGELOG.md',
        'THIRD_PARTY_NOTICES.md': ROOT/'THIRD_PARTY_NOTICES.md',
        'docs/README.en.md': ROOT/'docs/README.en.md',
        'docs/KNOWN_ISSUES.md': ROOT/'docs/KNOWN_ISSUES.md',
        'docs/BUILDING.md': ROOT/'docs/BUILDING.md',
        'licenses/PluginSDK-MIT.txt': ROOT/'plugin/third_party/sdk/LICENSE',
    }
    documents = {name: path.read_bytes() for name, path in docs.items()}
    out = ROOT/'dist'
    out.mkdir(exist_ok=True)
    archives = {kind: out/f'd2r-third-person-{version}-{kind}-windows-x64.zip' for kind in editions}
    if any(p.exists() or p.with_suffix(p.suffix+'.sha256').exists() for p in archives.values()):
        raise FileExistsError('Release files already exist; refusing to overwrite')
    reports = []
    for kind, archive in archives.items():
        names = ['d2rl-3dcam.dll'] + (['d2rl-renderdistance.dll'] if kind == 'extended' else [])
        digests = {name: hashlib.sha256(binaries[name]).hexdigest() for name in names}
        manifest = {
            'version': version, 'edition': kind, 'dll_sha256': digests,
            'experimental': True, 'tested_game_exe': '3.2.92777',
            'tested_loader': '1.3.1-beta', 'plugin_abi': 4,
            'game_files_included': False, 'save_files_included': False,
            'online_safety_guaranteed': False,
            'distance_default_on_first_install': False,
            'distance_choice_persisted': kind == 'extended',
            'F12': 'camera only',
        }
        with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED) as z:
            for name in names:
                z.writestr(name, binaries[name])
            for name, data in documents.items():
                z.writestr(name, data)
            z.writestr('manifest.json', json.dumps(manifest, indent=2)+'\n')
            z.writestr('SHA256SUMS.txt', ''.join(digest+'  '+name+'\n' for name, digest in digests.items()))
        with zipfile.ZipFile(archive) as z:
            assert z.testzip() is None
            assert set(z.namelist()) == set(documents) | set(names) | {'manifest.json', 'SHA256SUMS.txt'}
            for name in names:
                assert hashlib.sha256(z.read(name)).hexdigest() == digests[name]
        archive_hash = hashlib.sha256(archive.read_bytes()).hexdigest()
        archive.with_suffix(archive.suffix+'.sha256').write_text(archive_hash+'  '+archive.name+'\n',encoding='ascii')
        reports.append({'archive': archive.name, 'sha256': archive_hash, **manifest})
    return reports

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--dll-dir', type=Path, default=ROOT/'bazel-bin/plugin')
    parser.add_argument('--edition', choices=['standard','extended','all'], default='all')
    args = parser.parse_args()
    print(json.dumps(package(args.dll_dir, args.edition), indent=2))
