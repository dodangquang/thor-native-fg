"""Check GPU camera motion against independently decoded fixture matrices/depth."""
import argparse,json,pathlib,struct
import numpy as np

def main():
    p=argparse.ArgumentParser();p.add_argument('fixtures',type=pathlib.Path);p.add_argument('gpu',type=pathlib.Path);a=p.parse_args()
    m=json.loads((a.fixtures/'manifest.json').read_text());assert m['scene3d'];w,h=m['width'],m['height'];count=w*h
    payload=(a.fixtures/'fixtures.bin').read_bytes();record=144+count*24;start=28+record
    params=payload[start:start+144];matrix=np.frombuffer(params[48:112],'<f4').reshape(4,4).T.astype('f8')
    depth=np.frombuffer(payload[start+144+count*4:start+144+count*8],'<f4').reshape(h,w).astype('f8')
    # Literal upstream fragment gather/swizzle footprint. It uses rows-1,0,+2,
    # unlike the compute variant's symmetric3x3; do not silently substitute it.
    y,x=np.mgrid[:h,:w];nearest=np.full((h,w),np.inf)
    for dx,dy in ((-1,-1),(0,-1),(-1,0),(0,0),(1,-1),(0,2),(1,2),(-1,2),(1,0)):
        nearest=np.minimum(nearest,depth[(y+dy).clip(0,h-1),(x+dx).clip(0,w-1)])
    y,x=np.mgrid[:h,:w];current=np.stack((2*(x+.5)/w-1,2*(y+.5)/h-1,nearest,np.ones_like(nearest)),-1)
    old=current@matrix.T;old=old[...,:2]/old[...,3:4];expected=current[...,:2]-old
    binary=(a.gpu/'diagnostic-001.bin').read_bytes();actual=np.frombuffer(binary[count*4:count*12],'<f2').reshape(h,w,4)[...,:2].astype('f8')
    error=float(np.max(abs(actual-expected)));print('CAMERA_DIAGNOSTIC ndc_error=',error,flush=True);assert error<.00002
    # Require nontrivial camera flow; a zero/identity provider must not pass.
    assert float(np.max(abs(expected)))>.005
    report=dict(max_ndc_error=error,max_source_pixel_error=float(np.max(abs(actual-expected)*[w/2,h/2])),nonzero_camera_motion=True,limits='frame1, lateral camera/static geometry, normal depth only; not full arbitrary-camera or guest-game integration')
    (a.gpu/'camera-audit.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
if __name__=='__main__':main()
