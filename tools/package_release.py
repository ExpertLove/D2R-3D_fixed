"""Camera-only, allowlisted release archive. No game or save paths are inspected."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--dll', type=Path, default=ROOT/'bazel-bin/plugin/d2rl-3dcam.dll')
args = parser.parse_args()
version = (ROOT/'VERSION').read_text(encoding='utf8').strip()
data = args.dll.read_bytes()
if data[:2] != b'MZ' or version.encode('ascii') not in data:
    raise SystemExit('Not the expected plugin version; no archive created.')
digest = hashlib.sha256(data).hexdigest()
files = {
    'README.md': ROOT/'README.md',
    'CHANGELOG.md': ROOT/'CHANGELOG.md',
    'THIRD_PARTY_NOTICES.md': ROOT/'THIRD_PARTY_NOTICES.md',
    'docs/README.en.md': ROOT/'docs/README.en.md',
    'docs/KNOWN_ISSUES.md': ROOT/'docs/KNOWN_ISSUES.md',
    'docs/BUILDING.md': ROOT/'docs/BUILDING.md',
    'licenses/PluginSDK-MIT.txt': ROOT/'plugin/third_party/sdk/LICENSE',
}
manifest = {
    'version': version,
    'dll': 'd2rl-3dcam.dll',
    'dll_sha256': digest,
    'experimental': True,
    'tested_game_exe': '3.2.92777',
    'tested_loader': '1.3.1-beta',
    'plugin_abi': 4,
    'game_files_included': False,
    'save_files_included': False,
    'online_safety_guaranteed': False,
}
out = ROOT/'dist'
out.mkdir(exist_ok=True)
archive = out/f'd2r-third-person-{version}-windows-x64.zip'
with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED) as z:
    z.writestr('d2rl-3dcam.dll', data)
    for name, path in files.items():
        z.writestr(name, path.read_bytes())
    z.writestr('manifest.json', json.dumps(manifest, indent=2)+'\n')
    z.writestr('SHA256SUMS.txt', digest+'  d2rl-3dcam.dll\n')
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    assert set(z.namelist()) == set(files) | {'d2rl-3dcam.dll', 'manifest.json', 'SHA256SUMS.txt'}
    assert hashlib.sha256(z.read('d2rl-3dcam.dll')).hexdigest() == digest
archive_hash = hashlib.sha256(archive.read_bytes()).hexdigest()
archive.with_suffix(archive.suffix+'.sha256').write_text(archive_hash+'  '+archive.name+'\n',encoding='ascii')
print(json.dumps({'archive': archive.name, 'sha256': archive_hash, **manifest}, indent=2))
