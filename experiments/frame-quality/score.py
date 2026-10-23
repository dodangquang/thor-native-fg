from pathlib import Path
import csv
import json
import sys
import numpy as np
from PIL import Image

fixtures, results = map(Path, sys.argv[1:3])
manifest = json.loads((fixtures/'manifest.json').read_text())
metrics = {int(row['id']): row for row in csv.DictReader((results/'metrics.csv').open())}
rows = []
for case in manifest['cases']:
    i = case['id']; w,h = manifest['width'],manifest['height']
    actual = np.frombuffer((results/f'{i:03d}-mid.rgba').read_bytes(), np.uint8).reshape(h,w,4)[...,:3].astype(float)
    truth = np.asarray(Image.open(fixtures/f'{i:03d}-truth.png'))[...,:3].astype(float)
    current = np.asarray(Image.open(fixtures/f'{i:03d}-current.png'))[...,:3].astype(float)
    previous = np.asarray(Image.open(fixtures/f'{i:03d}-previous.png'))[...,:3].astype(float)
    hud = np.asarray(Image.open(fixtures/f'{i:03d}-hud.png')) > 0
    mask = np.zeros((h,w),bool);mask[12:-12,12:-12] = True
    def error(image, where):
        return float(np.abs(image-truth)[where].mean()) if np.any(where) else 0
    row = dict(**case, allow=int(metrics[i]['allow']), reasons=int(metrics[i]['reasons']),
               mae=error(actual,mask), duplicate_mae=error(current,mask),
               blend_mae=error((previous+current)/2,mask), hud_mae=error(actual,hud),
               max_hud_change=float(np.max(abs(actual-current)[hud])) if hud.any() else 0)
    rows.append(row)
    Image.fromarray(actual.astype(np.uint8)).save(results/f'{i:03d}-mid.png')
    print(f"{i:02d} {case['scene']:8s} dx={case['shift']:2} hud={int(case['hud'])} allow={row['allow']} mae={row['mae']:.3f} duplicate={row['duplicate_mae']:.3f} blend={row['blend_mae']:.3f} hud={row['hud_mae']:.3f}")
(results/'scores.json').write_text(json.dumps(rows,indent=2))
print('Accepted',sum(r['allow'] for r in rows),'/',len(rows),'; cut accepted',sum(r['allow'] for r in rows if r['expected_cut']))
