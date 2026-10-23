"""Bind the 3D improvement claim to paired imagery and same-native controls."""
import hashlib,json,pathlib
root=pathlib.Path(__file__).resolve().parents[2]/'research'
names=['gsr2-v2-3d','gsr2-v2-3d-reset','gsr2-v2-3d-bad-motion']
gpu=['gsr2-stable-v2-3d-gpu','gsr2-stable-v2-3d-reset-gpu','gsr2-stable-v2-3d-bad-motion-gpu']
reports=[json.loads((root/p/'report.json').read_text()) for p in gpu]
assert len({r['native_sha256'] for r in reports})==1
for frame in range(32):
    for suffix in ('input','reference'):
        hashes=[hashlib.sha256((root/p/f'frame-{frame:03d}-{suffix}.png').read_bytes()).hexdigest() for p in names]
        assert len(set(hashes))==1
for frame in (0,16):
    files=[(root/p/f'frame-{frame:03d}.rgba16f').read_bytes() for p in gpu[:2]]
    assert files[0]==files[1]
normal,reset,bad=[r['summary']['gsr2_mse'] for r in reports]
assert normal<reset and normal<bad and normal<reports[0]['summary']['bilinear_mse']
result=dict(native=reports[0]['native_sha256'],paired_images_exact=64,reset_frames_exact=[0,16],normal_psnr=reports[0]['summary']['gsr2_psnr'],history_disabled_psnr=reports[1]['summary']['gsr2_psnr'],incorrect_zero_motion_psnr=reports[2]['summary']['gsr2_psnr'],bilinear_psnr=reports[0]['summary']['bilinear_psnr'],limits='One authored3Dscene family/lateral camera, not real-game/DLSS or universal quality evidence')
(root/'gsr2-3d-control-comparison.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
