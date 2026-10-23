"""Export tracked changes and new provider headers without staging upstream."""
import difflib
import pathlib
import subprocess

root = pathlib.Path(__file__).resolve().parent
source = root.parents[1] / 'research' / 'eden-thor'
base = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=source, text=True).strip()
if base != '989768aa46720e3fcab9e2111e3ddd70819c382b':
    raise SystemExit('Unexpected Eden base; review before exporting')
patch = subprocess.check_output(['git', 'diff', '--binary', 'HEAD'], cwd=source)
for name in ('thor_native.h', 'thor_composite_layout.h', 'thor_temporal_probe.h'):
    relative = 'src/video_core/renderer_vulkan/present/' + name
    contents = (source / relative).read_text(encoding='utf-8').splitlines(keepends=True)
    delta = ''.join(difflib.unified_diff([], contents, fromfile='/dev/null',
                                       tofile='b/' + relative))
    patch += ('diff --git a/' + relative + ' b/' + relative + '\n'
              'new file mode 100644\n' + delta).encode('utf-8')
(root / 'thor-spatial-provider.patch').write_bytes(patch)
print(f'Exported {len(patch)} bytes against {base}')
