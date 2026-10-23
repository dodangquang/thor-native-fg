"""Independent CPU reset-frame FS math and phase-grid denominator diagnostic."""
import argparse,json,pathlib
import numpy as np
from PIL import Image

def weights(base):
    y=base-1;y2=y*y
    return (.75*y+y2)*y2

def reference(source,jitter,stable=False,full=False):
    h,w=source.shape[:2];oh,ow=h*2,w*2;y,x=np.mgrid[:oh,:ow]
    sx=(x+.5)/2;sy=(y+.5)/2
    ix=np.floor(np.clip(sx+jitter[0],0,w)).astype(int).clip(0,w-1);iy=np.floor(np.clip(sy+jitter[1],0,h)).astype(int).clip(0,h-1)
    dx=ix+.5-jitter[0]-sx;dy=iy+.5-jitter[1]-sy
    total=np.zeros((oh,ow,3));mass=np.zeros((oh,ow));lo=np.full_like(total,np.inf);hi=np.full_like(total,-np.inf)
    center=np.zeros_like(total);variance=np.zeros_like(total);boxmass=np.zeros_like(mass)
    motion_length=np.linalg.norm(np.array([.005,-.0024])*[ow,oh]);curve=-2-np.clip(motion_length*.02,0,1)
    # Flat depth only. Background ROI is deliberately away from foreground.
    k=.5 if stable else 1.
    for oy in (-1,0,1):
        for ox in (-1,0,1):
            if ox and oy and not full:continue # Upstream fragment path has its four corners disabled.
            color=source[(iy+oy).clip(0,h-1),(ix+ox).clip(0,w-1)]
            q=weights(np.clip(((dx+ox)**2+(dy+oy)**2)*k*k,0,1));mass+=q;total+=color*q[...,None]
            lo=np.minimum(lo,color);hi=np.maximum(hi,color)
            bw=np.exp(((dx+ox)**2+(dy+oy)**2)*curve);boxmass+=bw;center+=color*bw[...,None];variance+=color*color*bw[...,None]
    filtered=np.clip(total/mass[...,None],lo-.075,hi+.075)
    center/=boxmass[...,None];variance=np.sqrt(abs(variance/boxmass[...,None]-center*center))
    boxsize=20+(1-20)*np.clip(motion_length*.05,0,1)
    lo=np.maximum(lo,center-variance*boxsize);hi=np.minimum(hi,center+variance*boxsize)
    history=np.clip(np.zeros_like(total),lo,hi)
    contribution=mass/3
    base=np.minimum(1,1+(contribution*10-1)*np.clip(10*motion_length,0,1))
    base=np.minimum(base,base+(contribution-base)*np.clip(motion_length*.05,0,1))
    outside=np.any((lo>0)|(hi<0),-1);base=np.where(outside,np.minimum(base,.1),base)
    alpha=np.clip(contribution/np.maximum(1.192e-7,base+contribution)+1,0,1)
    return history*(1-alpha[...,None])+filtered*alpha[...,None],mass

def main():
    p=argparse.ArgumentParser();p.add_argument('fixtures',type=pathlib.Path);p.add_argument('gpu',type=pathlib.Path);p.add_argument('--stable',action='store_true');p.add_argument('--full',action='store_true');a=p.parse_args()
    m=json.loads((a.fixtures/'manifest.json').read_text());w,h=m['width'],m['height'];ow,oh=m['output']
    assert (ow,oh)==(w*2,h*2)
    source=np.asarray(Image.open(a.fixtures/'frame-000-input.png'),dtype='f8')[...,:3]/255
    expected,mass=reference(source,m['frames'][0]['jitter'],a.stable,a.full)
    actual=np.fromfile(a.gpu/'frame-000.rgba16f','<f2').reshape(oh,ow,4)[...,:3].astype('f8');roi=(slice(12,42),slice(12,ow-12))
    error=float(np.max(abs(actual[roi]-expected[roi])))
    print('CPU_DIAGNOSTIC error=',error,'actual=',actual[20,20],'expected=',expected[20,20],'mass=',mass[20,20],flush=True)
    assert error<=1/255
    phase=np.linspace(-.5,.5,513);dy,dx=np.meshgrid(phase,phase);summary={}
    for k in (1.,.5):
        mass_grid=np.zeros_like(dx)
        for oy in (-1,0,1):
            for ox in (-1,0,1):
                if ox and oy and not a.full:continue
                mass_grid+=weights(np.clip(((dx+ox)**2+(dy+oy)**2)*k*k,0,1))
        summary[str(k)]=dict(minimum_mass=float(mass_grid.min()),maximum_mass=float(mass_grid.max()),nonpositive=int(np.sum(mass_grid<=0)),small_absolute=int(np.sum(abs(mass_grid)<.001)))
    report=dict(cpu_reset_roi_max_byte_error=error*255,stable=a.stable,full=a.full,phase_grid=summary,limits='Reset-frame flat-depth ROI only; full temporal/disocclusion CPU conformance remains unproved. Grid covers513x513phases, not all real numbers.')
    (a.gpu/'kernel-probe.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
if __name__=='__main__':main()
