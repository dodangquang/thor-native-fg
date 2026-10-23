"""Exact 2D plane/object temporal inputs for SGSR2; no external artwork."""
import argparse, hashlib, json, pathlib, struct
import numpy as np
from PIL import Image

def halton(i, base):
    value, scale = 0., 1.
    while i:
        scale /= base; value += (i % base) * scale; i //= base
    return value

def render(w, h, frame, jitter, motion=True):
    y, x = np.mgrid[:h, :w].astype('f4')
    u, v = (x + .5 - jitter[0]) / w, (y + .5 - jitter[1]) / h
    cut = frame >= 16
    t = frame if not cut else frame - 16
    sx, sy = (.0025, -.0012) if not cut else (-.003, .001)
    a, b = u - sx*t, v - sy*t
    rgb = np.stack((.35+.17*np.sin(a*91+b*23), .4+.22*np.cos(b*73-a*19), .3+.2*np.sin(a*47-b*63)), -1)
    if cut: rgb = rgb[..., ::-1].copy() * np.array([.75, .9, 1.1])
    foreground = (abs(u-(.3+t*.008))<.10)&(abs(v-.52)<.19)
    rgb[foreground] = np.stack((.7+.1*np.sin(v*190), .14+.05*np.cos(u*170), .08+u*.05), -1)[foreground]
    depth = np.where(foreground,.3,.7).astype('<f4')
    velocity = np.zeros((h,w,4), '<f4')
    velocity[...,0] = np.where(foreground,.008,sx)*2
    velocity[...,1] = np.where(foreground,0,sy)*2
    velocity[...,:2] = velocity[...,:2]*.2495+32767/65535
    rgba = np.full((h,w,4),255,'u1'); rgba[...,:3] = np.rint(np.clip(rgb,0,1)*255)
    return rgba, depth, velocity

def main():
    p=argparse.ArgumentParser();p.add_argument('directory',type=pathlib.Path);p.add_argument('--width',type=int,default=128);p.add_argument('--height',type=int,default=72);p.add_argument('--history-disabled',action='store_true');p.add_argument('--zero-velocity',action='store_true');p.add_argument('--constant-control',action='store_true');a=p.parse_args()
    a.directory.mkdir(parents=True,exist_ok=True);w,h=a.width,a.height
    assert w>=16 and h>=16
    records=[]
    with (a.directory/'fixtures.bin').open('wb') as out:
        out.write(struct.pack('<8s5I',b'THORGS2A',32,w,h,w*2,h*2))
        for frame in range(32):
            j=(halton(frame%16+1,2)-.5,halton(frame%16+1,3)-.5)
            params=bytearray(144)
            struct.pack_into('<4I6f',params,0,w,h,w*2,h*2,1/w,1/h,1/(w*2),1/(h*2),*j)
            struct.pack_into('<16f',params,48,*np.eye(4,dtype='f4').flatten())
            reset=a.history_disabled or frame in (0,16)
            # Camera is exactly identity for all frames; object motion is separate.
            struct.pack_into('<4f2I',params,112,1.,1.,.1,.1,1,int(reset))
            rgba,depth,velocity=render(w,h,frame,j)
            if a.zero_velocity: velocity[...,:2]=32767/65535
            if a.constant_control:
                rgba[...,:3]=(40,90,160) if frame<16 else (180,30,65)
                depth[:]=.7;velocity[...,:2]=32767/65535
            out.write(params);out.write(rgba.tobytes());out.write(depth.tobytes());out.write(velocity.tobytes())
            ref=render(w*2,h*2,frame,(0,0))[0]
            if a.constant_control: ref[...,:3]=rgba[0,0,:3]
            Image.fromarray(ref).save(a.directory/f'frame-{frame:03d}-reference.png')
            Image.fromarray(rgba).save(a.directory/f'frame-{frame:03d}-input.png')
            records.append(dict(frame=frame,jitter=j,reset=reset))
    manifest=dict(width=w,height=h,output=[w*2,h*2],frames=records,history_disabled=a.history_disabled,zero_velocity=a.zero_velocity,constant_control=a.constant_control,source='author-owned analytic moving planes; identity camera; normal depth; encoded object velocity; Halton jitter',limits='toy scene, no real-game integration or quality equivalence',sha256=hashlib.sha256((a.directory/'fixtures.bin').read_bytes()).hexdigest())
    (a.directory/'manifest.json').write_text(json.dumps(manifest,indent=2))
if __name__=='__main__':main()
