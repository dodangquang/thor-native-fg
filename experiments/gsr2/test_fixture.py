"""Motion semantics must agree with the rendered material, including the object."""
import unittest
import numpy as np
from prepare import render
from collect import bilinear
from scene3d import camera,render as render3d

def probe(u,v,frame):
    # render() evaluates analytic coordinates; arbitrary jitter selects the point
    # for this CPU-only probe. No invalid jitter is written to GPU fixtures.
    return render(1,1,frame,(.5-u,.5-v))

class FixtureTests(unittest.TestCase):
    def test_material_advection(self):
        for frame in (1,4,9,15,17,22,31):
            t=frame if frame<16 else frame-16
            for u,v in ((.3+t*.008,.51),(.32+t*.008,.48),(.85,.23),(.80,.81)):
                current,depth,encoded=probe(u,v,frame)
                velocity=(encoded[0,0,:2].astype('f8')-32767/65535)/.2495
                previous,pdepth,_=probe(u-velocity[0]/2,v-velocity[1]/2,frame-1)
                self.assertEqual(float(depth[0,0]),float(pdepth[0,0]))
                self.assertLessEqual(int(np.max(abs(current.astype(int)-previous.astype(int)))),1)
    def test_bilinear_constant(self):
        a=np.full((8,9,3),.37);self.assertLess(float(np.max(abs(bilinear(a,18,16,(.3,-.4))-.37))),1e-12)
    def test_bilinear_jitter_ramp(self):
        y,x=np.mgrid[:8,:9];a=np.repeat((x+2*y)[...,None],3,-1).astype('f8')
        out=bilinear(a,18,16,(.25,-.25));oy,ox=np.mgrid[:16,:18]
        expected=(ox+.5)/2-.5+.25+2*((oy+.5)/2-.5-.25)
        self.assertLess(float(np.max(abs(out[2:-2,2:-2,0]-expected[2:-2,2:-2]))),1e-12)
    def test_perspective_camera_reprojection(self):
        for frame in (1,6,15,17,24,31):
            cur,_,projection,_=camera(frame,16/9);prev=camera(frame-1,16/9)[0]
            matrix=prev@np.linalg.inv(cur)
            packed=np.asarray(matrix.T.flatten(),dtype='<f4').tobytes()
            decoded=np.frombuffer(packed,'<f4').reshape(4,4).T
            for world in ((-.4,.3,2,1),(.9,.8,5,1),(-1,.6,12,1)):
                clip=cur@world;ndc=clip/clip[3];old=prev@world;expected=old/old[3]
                actual=decoded@ndc;actual/=actual[3]
                self.assertLess(float(np.max(abs(actual-expected))),1e-6)
                self.assertAlmostEqual(float(ndc[0]-expected[0]),-projection[0,0]*.012/world[2],places=12)
    def test_normal_depth_from_ground_plane(self):
        _,depth,velocity=render3d(128,72,0,(0,0));projection=camera(0,16/9)[2]
        z=1.1*projection[1,1]/(2*(65.5/72)-1)
        self.assertAlmostEqual(float(depth[65,120]),projection[2,2]+projection[2,3]/z,places=6)
        self.assertTrue(np.all(velocity==0)) # Camera fallback sentinel, not encoded object-zero.
if __name__=='__main__':unittest.main()
