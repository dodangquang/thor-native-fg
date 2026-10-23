"""Prepare the pinned ARM64EC GSR2 test files; never modify a game/device."""
import argparse
import hashlib
import json
import pathlib
import re
import zipfile

ARCHIVE_SHA = 'a520471c43118d0d07ab3264ede4f2dd11a94c1f6c5449b3635cf25a1a1c0a27'
ROOT = pathlib.Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('archive', type=pathlib.Path)
args = parser.parse_args()
if hashlib.sha256(args.archive.read_bytes()).hexdigest() != ARCHIVE_SHA:
    raise SystemExit('Release archive checksum does not match pinned asset')
destination = ROOT / 'build' / 'droiddeck-gsr2-test'
destination.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(args.archive) as package:
    # Read only known files rather than extracting arbitrary archive paths.
    dll = package.read('OptiScaler.dll')
    ini = package.read('OptiScaler.ini').decode('utf-8-sig')
    updates = {
        ('Upscalers', 'Dx12Upscaler'): 'sgsr2',
        ('Upscalers', 'Dx11Upscaler'): 'sgsr2_12',
        ('Upscalers', 'VulkanUpscaler'): 'sgsr2',
        ('FrameGen', 'Enabled'): 'false',
        ('FrameGen', 'FGInput'): 'nofg',
        ('FrameGen', 'FGOutput'): 'nofg',
        ('Plugins', 'LoadAsiPlugins'): 'false',
        ('Menu', 'UseHQFont'): 'false',
        ('Spoofing', 'Dxgi'): 'true',
        ('Log', 'LogToFile'): 'true',
        ('Log', 'LogLevel'): '2',
        ('Log', 'LogFileName'): 'OptiScaler-Thor-GSR2.log',
    }
    section = ''
    changed = set()
    lines = []
    for line in ini.splitlines():
        match = re.fullmatch(r'\[([^]]+)\]', line.strip())
        if match:
            section = match[1]
        key = line.split('=', 1)[0].strip()
        identity = (section, key)
        if '=' in line and identity in updates:
            if identity in changed:
                raise SystemExit(f'Duplicate setting: {identity}')
            line = key + '=' + updates[identity]
            changed.add(identity)
        lines.append(line)
    if changed != updates.keys():
        raise SystemExit(f'Missing settings: {updates.keys() - changed}')
    (destination / 'dxgi.dll').write_bytes(dll)
    (destination / 'OptiScaler.ini').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    names = ['README.md'] + [name for name in package.namelist()
                             if name.startswith('licenses/') and name.endswith('.txt')]
    for name in names:
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(package.read(name))
manifest = {
    'source': 'https://github.com/justradical/Sloptiscaler',
    'commit': 'f80e4da339cd4624212fd0546922571fe01205c1',
    'asset': 'https://github.com/justradical/Sloptiscaler/releases/download/finally/OptiScaler-SGSR2-arm64ec-f80e4da3.zip',
    'archive_sha256': ARCHIVE_SHA,
    'files': {name: hashlib.sha256((destination / name).read_bytes()).hexdigest()
              for name in ('dxgi.dll', 'OptiScaler.ini')},
    'tested_on_this_device': False,
    'frame_generation': 'disabled',
    'requires': 'Per-game DLSS/FSR2+/XeSS input interception under compatible ARM64EC Proton',
}
(destination / 'provenance.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
print(f'Prepared isolated test files: {destination}; no game/device modified')
