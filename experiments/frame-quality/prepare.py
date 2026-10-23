"""Author-owned temporal scenes with independently rendered t=0, .5, 1.
No game captures or downloaded assets. Not evidence of general game quality.
"""
from pathlib import Path
import hashlib
import json
import struct
import sys
import numpy as np
from PIL import Image

out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
w, h = 256, 144
y, x = np.mgrid[:h, :w].astype(np.float32)

def render(t, shift, scene, hud):
    xx = x - shift * t if scene in ('pan','diagonal') else x
    yy = y - shift * .6*t if scene == 'diagonal' else y
    if scene == 'zoom':
        xx = (x-w/2)/(1+shift*.012*t)+w/2
        yy = (y-h/2)/(1+shift*.012*t)+h/2
    phase = 9.7 if scene in ('diagonal','zoom','thin','spin') else 0
    rgb = np.stack([110 + 32*np.sin((xx+phase)*.18)*np.cos(yy*.11),
                    120 + 35*np.cos(xx*.09+yy*.15+phase),
                    105 + 30*np.sin(xx*.13-yy*.12+phase)], -1)
    if scene in ('object', 'crossing'):
        ox = x - 76 - shift*t
        object_mask = (ox >= 0) & (ox < 46) & (y >= 42) & (y < 100)
        obj = np.stack([170+20*np.sin(ox*.45), 85+15*np.cos(y*.3),
                        90+20*np.sin((ox+y)*.24)], -1)
        rgb[object_mask] = obj[object_mask]
        if scene == 'crossing':
            occluder = (x > 102) & (x < 114) & (y > 25) & (y < 117)
            rgb[occluder] = [65, 150, 95]
    if scene == 'cut':
        if t > 0:
            rgb = 240-rgb[..., [2, 0, 1]]
    if scene == 'thin':
        line = abs(x - 89 - shift*t - .18*(y-70)) < 1.5
        rgb[line & (y>30) & (y<114)] = [198, 62, 170]
    if scene == 'spin':
        theta=shift*.055*t
        ox=(x-128)*np.cos(theta)+(y-72)*np.sin(theta)
        oy=-(x-128)*np.sin(theta)+(y-72)*np.cos(theta)
        obj=(abs(ox)<31)&(abs(oy)<17)
        tex=np.stack([170+25*np.sin(ox*.3),90+18*np.cos(oy*.5),95+22*np.sin((ox+oy)*.17)],-1)
        rgb[obj]=tex[obj]
    if scene == 'flash':
        rgb += 14*t
    mask = np.zeros((h, w), np.uint8)
    if hud:
        panel = (x >= 8) & (x < 87) & (y >= 7) & (y < 25)
        rgb[panel] = [22, 26, 32]
        # Fixed authored glyph-like strokes and a one-pixel reticle.
        strokes = panel & (((x.astype(int) % 10 < 2) & (y > 10) & (y < 21)) |
                           ((y == 11) & (x > 12) & (x < 79)))
        reticle = ((abs(x-128) <= 6) & (y == 72)) | ((x == 128) & (abs(y-72) <= 6))
        rgb[strokes | reticle] = [232, 235, 239]
        mask[panel | reticle] = 255
    rgba = np.concatenate([np.rint(np.clip(rgb, 0, 255)).astype(np.uint8),
                           np.full((h, w, 1), 255, np.uint8)], -1)
    return rgba, mask

holdout = '--holdout' in sys.argv[2:]
cases = []
for scene in (('diagonal','zoom','thin','spin') if holdout else ('pan', 'object', 'crossing')):
    for shift in ((1,2,3,6,-3) if holdout else (1, 2, 3, 4, 8, -2)):
        for hud in (False, True):
            cases.append((scene, shift, hud))
if not holdout:
    cases += [('cut', 0, False), ('cut', 0, True), ('flash', 0, False), ('pan', 0, True)]
manifest = []
with (out/'pairs.bin').open('wb') as f:
    f.write(b'THORFGQ1' + struct.pack('<III', w, h, len(cases)))
    for i, (scene, shift, hud) in enumerate(cases):
        frames = [render(t, shift, scene, hud)[0] for t in (0, .5, 1)]
        for name, frame in zip(('previous', 'truth', 'current'), frames):
            Image.fromarray(frame).save(out/f'{i:03d}-{name}.png')
        mask = render(0, shift, scene, hud)[1]
        Image.fromarray(mask).save(out/f'{i:03d}-hud.png')
        f.write(frames[0].tobytes()); f.write(frames[2].tobytes())
        manifest.append(dict(id=i, scene=scene, shift=shift, hud=hud,
                             expected_cut=scene == 'cut'))
data = dict(width=w, height=h, cases=manifest,
            renderer_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            fixture_sha256=hashlib.sha256((out/'pairs.bin').read_bytes()).hexdigest())
(out/'manifest.json').write_text(json.dumps(data, indent=2))
print(f'Generated {len(cases)} independent temporal pairs in {out}')
