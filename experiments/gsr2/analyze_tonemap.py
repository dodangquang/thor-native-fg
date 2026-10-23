"""Offline diagnostic only: quantify upstream inverse-tonemap regularizer bias."""
import json,pathlib
import numpy as np
from PIL import Image
root=pathlib.Path(__file__).resolve().parents[2]/'research'
reports=[]
for fixture,result in [('gsr2-still-camera','gsr2-final-camera-gpu'),('gsr2-1080','gsr2-1080-gpu')]:
    m=json.loads((root/fixture/'manifest.json').read_text());ow,oh=m['output'];errors=[]
    for i in list(range(4,16))+list(range(20,32)):
        original=np.fromfile(root/result/f'frame-{i:03d}.rgba16f','<f2').reshape(oh,ow,4)[...,:3].astype('f4')
        k=1+600/65504
        corrected=original*k/(1-(k-1)*original.max(-1,keepdims=True))
        reference=np.asarray(Image.open(root/fixture/f'frame-{i:03d}-reference.png'),dtype='f4')[...,:3]/255
        errors.append(float(np.mean((corrected[8:-8,8:-8]-reference[8:-8,8:-8])**2)))
    reports.append(dict(fixture=fixture,diagnostic_corrected_psnr=float(-10*np.log10(np.mean(errors))),raw_report=str(root/result/'report.json')))
report=dict(results=reports,limits='Offline transformed output, not another measured GPU implementation or quality approval. Correction only inverts known regularizer for preExposure1 LDR inputs, not arbitrary HDR/exposure. Does not supply missing renderer data.')
(root/'gsr2-tonemap-diagnostic.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
