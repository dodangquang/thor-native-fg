"""Author-owned ray scene with exact Vulkan0..1 depth and camera reprojection.

World/camera +Y is down, forward +Z. All geometry is static, so the zero object
velocity sentinel requests camera reprojection from depth in official SGSR2.
"""
import numpy as np

def camera(frame,aspect):
    t=frame%16;origin=np.array([(.12 if frame>=16 else 0)+t*.012,0,0.],dtype='f8')
    near,far=.1,30.;fy=1.6;fx=fy/aspect
    projection=np.array([[fx,0,0,0],[0,fy,0,0],[0,0,far/(far-near),-far*near/(far-near)],[0,0,1,0]],dtype='f8')
    view=np.eye(4);view[:3,3]=-origin
    return projection@view,origin,projection,near

def trace(u,v,frame):
    h,w=u.shape;vp,origin,p,near=camera(frame,w/h)
    directions=np.stack(((u*2-1)/p[0,0],(v*2-1)/p[1,1],np.ones_like(u)),-1)
    directions/=np.linalg.norm(directions,axis=-1,keepdims=True)
    color=np.stack((.09+.09*v,.15+.12*v,.23+.14*v),-1)
    depth=np.ones_like(u);distance=np.full_like(u,np.inf);world=np.zeros_like(directions)
    # Textured ground under the camera, finite horizon. Specular is omitted so
    # visible static material has the same color at corresponding world points.
    floor_t=1.1/np.maximum(directions[...,1],1e-8);hit=origin+directions*floor_t[...,None]
    mask=(directions[...,1]>.0001)&(hit[...,2]<20)
    checker=(np.floor(hit[...,0]*3)+np.floor(hit[...,2]*3))%2
    floor=(.17+.14*checker+.025*np.sin(hit[...,0]*63)*np.cos(hit[...,2]*51))[...,None]*np.array([.85,.95,1.])
    color=np.where(mask[...,None],floor,color);distance=np.where(mask,floor_t,distance);world=np.where(mask[...,None],hit,world)
    light=np.array([-.4,-.8,-.5]);light/=np.linalg.norm(light)
    for center,radius,base in [((-.65,.12,2.3),.55,(.65,.14,.06)),((.65,.38,3.2),.6,(.05,.27,.52)),((.15,.48,1.55),.23,(.24,.45,.08))]:
        center=np.array(center);oc=origin-center;b=(directions*oc).sum(-1)
        disc=b*b-(oc@oc-radius*radius);t=-b-np.sqrt(np.maximum(disc,0));mask=(disc>=0)&(t>0)&(t<distance)
        hit=origin+directions*t[...,None];normal=(hit-center)/radius;shade=.3+.7*np.maximum((normal*light).sum(-1),0)
        bands=.82+.18*np.sin(hit[...,0]*31+hit[...,1]*57)
        c=np.array(base)*(shade*bands)[...,None]
        color=np.where(mask[...,None],c,color);distance=np.where(mask,t,distance);world=np.where(mask[...,None],hit,world)
    # Thin foreground bars, fixed in world space, create disocclusion.
    t=(1.2-origin[2])/directions[...,2];hit=origin+directions*t[...,None]
    mask=(abs(hit[...,0])<1)&(hit[...,1]>.32)&(hit[...,1]<.8)&((np.mod(hit[...,0]*7,1))<.09)&(t<distance)
    color=np.where(mask[...,None],np.array([.04,.055,.07]),color);distance=np.where(mask,t,distance);world=np.where(mask[...,None],hit,world)
    finite=np.isfinite(distance);z=np.maximum(world[...,2],near)
    depth=np.where(finite,p[2,2]+p[2,3]/z,1).astype('<f4')
    if frame>=16:color=color[...,::-1]*np.array([.85,1.1,.9])
    return np.clip(color,0,1),depth

def render(w,h,frame,jitter,samples=1):
    total=np.zeros((h,w,3));depth=None;offsets=[.5] if samples==1 else [.25,.75]
    for oy in offsets:
        for ox in offsets:
            u,v=np.meshgrid((np.arange(w)+ox-jitter[0])/w,(np.arange(h)+oy-jitter[1])/h)
            c,depth=trace(u,v,frame);total+=c
    rgba=np.full((h,w,4),255,'u1');rgba[...,:3]=np.rint(total/(len(offsets)**2)*255)
    return rgba,depth,np.zeros((h,w,4),'<f4')
