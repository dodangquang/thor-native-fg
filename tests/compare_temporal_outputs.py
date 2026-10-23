"""Require bit-exact extracted engine output against frozen GPU shader output."""
import argparse,json,pathlib
p=argparse.ArgumentParser();p.add_argument('baseline',type=pathlib.Path);p.add_argument('candidate',type=pathlib.Path);a=p.parse_args()
b=json.loads((a.baseline/'report.json').read_text());c=json.loads((a.candidate/'report.json').read_text())
assert b['fixtures_sha256']==c['fixtures_sha256'] and b['output']==c['output']
for i in range(32):
    name=f'frame-{i:03d}.rgba16f';before=(a.baseline/name).read_bytes();after=(a.candidate/name).read_bytes()
    assert len(after)==c['output'][0]*c['output'][1]*8 and before==after
r=dict(frames_bit_exact=32,rgba_components=32*c['output'][0]*c['output'][1]*4,baseline_native=b['native_sha256'],candidate_native=c['native_sha256'],fixtures_sha256=c['fixtures_sha256'])
(a.candidate/'bit-exact.json').write_text(json.dumps(r,indent=2));print(json.dumps(r,indent=2))
