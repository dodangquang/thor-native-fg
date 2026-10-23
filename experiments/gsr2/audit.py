"""Independent format/math and history-reset controls for exported GPU results."""
import json,pathlib
import numpy as np
from PIL import Image

ROOT=pathlib.Path(__file__).resolve().parents[2]
def decode(packed):
    return np.stack(((packed>>21)/2047.5,((packed>>10)&2047)/2047.5-.5,(packed&1023)/1023.5-.5),-1)

def main():
    root=ROOT/'research';f=root/'gsr2-still-camera';g=root/'gsr2-final-camera-gpu';w,h=128,72;p=w*h
    b=(g/'diagnostic.bin').read_bytes();source=np.frombuffer(b[:p*4],'u1').reshape(h,w,4)
    original=np.asarray(Image.open(f/'frame-000-input.png'));assert np.array_equal(source,original)
    rgb=source[...,:3].astype('f8')/255;maximum=rgb.max(-1)+1;rgb=rgb/maximum[...,None]
    y=(rgb[...,0]+2*rgb[...,1]+rgb[...,2])*.25;co=np.clip(.5*rgb[...,0]+.5-.5*rgb[...,2],0,1);cg=np.clip(y+co-rgb[...,0],0,1)
    packed=(np.uint32(y*2047.5)<<21)|(np.uint32(co*2047.5)<<10)|np.uint32(cg*1023.5)
    actual=np.frombuffer(b[p*12:],'<u4').reshape(h,w);error=float(np.max(abs(decode(actual)-decode(packed))))
    assert error<=2/1023.5
    motion=np.frombuffer(b[p*4:p*12],'<f2').reshape(h,w,4).astype('f4');assert np.max(abs(motion[...,3]-maximum))<.002
    assert np.max(abs(motion[0,0,:2]-np.array([.005,-.0024])))<.00001
    identities=[json.loads((root/name/'report.json').read_text())['native_sha256'] for name in ('gsr2-final-camera-gpu','gsr2-final-reset-gpu','gsr2-constant-bounded-edge-gpu')]
    assert len(set(identities))==1
    for i in (0,16):assert (g/f'frame-{i:03d}.rgba16f').read_bytes()==(root/'gsr2-final-reset-gpu'/f'frame-{i:03d}.rgba16f').read_bytes()
    # SGSR2 intentionally adds600/65504 in inverse tonemap. Preserve that math;
    # compare constant GPU pixels with this independent definition, not raw input.
    max_error=0.;native_error=0.
    for i in range(32):
        c=np.array([40,90,160] if i<16 else [180,30,65],dtype='f8')/255;m=c.max()+1;c=c/m
        y=(c[0]+2*c[1]+c[2])*.25;co=.5*c[0]+.5-.5*c[2];cg=y+co-c[0]
        pack=(np.uint32(y*2047.5)<<np.uint32(21))|(np.uint32(co*2047.5)<<np.uint32(10))|np.uint32(cg*1023.5)
        y,co,cg=decode(pack);d=np.clip(np.array([y-cg+co,y+cg,y-cg-co]),0,1);expected=d/(1+600/65504-d.max())
        a=np.fromfile(root/'gsr2-constant-bounded-edge-gpu'/f'frame-{i:03d}.rgba16f','<f2').reshape(148,260,4)[...,:3].astype('f4')
        max_error=max(max_error,float(np.max(abs(a-expected))));native_error=max(native_error,float(np.max(abs(a-np.array([40,90,160] if i<16 else [180,30,65])/255))))
    print('CONTROL_DIAGNOSTIC constant_cpu_math_max_byte_error=',max_error*255,'original_input_max_byte_error=',native_error*255)
    assert max_error<2/255
    report=dict(source_exact=True,decoded_pack_max_error=error,motion_exposure_control=True,reset_frames_bit_exact=[0,16],constant_cpu_math_max_byte_error=max_error*255,constant_original_input_max_byte_error=native_error*255,limits='Controls verify format/math/reset only; no quality approval. Original-input error retained.')
    (root/'gsr2-control-audit.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
if __name__=='__main__':main()
