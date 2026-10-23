"""Verify this run and export binary GPU outputs without shell redirection."""
import argparse,hashlib,json,pathlib,re,subprocess,tarfile
import numpy as np
from PIL import Image

def bilinear(image,ow,oh,jitter):
    h,w=image.shape[:2];x=(np.arange(ow)+.5)*w/ow-.5+jitter[0];y=(np.arange(oh)+.5)*h/oh-.5+jitter[1]
    ix=np.floor(x).astype(int);iy=np.floor(y).astype(int);fx=x-ix;fy=y-iy
    top=image[np.clip(iy,0,h-1)[:,None],np.clip(ix,0,w-1)]*(1-fx)[None,:,None]+image[np.clip(iy,0,h-1)[:,None],np.clip(ix+1,0,w-1)]*fx[None,:,None]
    bottom=image[np.clip(iy+1,0,h-1)[:,None],np.clip(ix,0,w-1)]*(1-fx)[None,:,None]+image[np.clip(iy+1,0,h-1)[:,None],np.clip(ix+1,0,w-1)]*fx[None,:,None]
    return top*(1-fy)[:,None,None]+bottom*fy[:,None,None]

def main():
    p=argparse.ArgumentParser();p.add_argument('fixtures',type=pathlib.Path);p.add_argument('directory',type=pathlib.Path);p.add_argument('instrumentation',type=pathlib.Path);a=p.parse_args()
    m=json.loads((a.fixtures/'manifest.json').read_text());run=a.instrumentation.read_text(encoding='utf-8-sig');pid=re.search(r'pid=(\d+)',run);native=re.search(r'native_sha256=(\w+)',run)
    assert pid and native and 'INSTRUMENTATION_CODE: -1' in run and m['sha256']==hashlib.sha256((a.fixtures/'fixtures.bin').read_bytes()).hexdigest() and 'fixtures_sha256='+m['sha256'] in run
    a.directory.mkdir(parents=True,exist_ok=True);adb=['E:/DevTools/Android/Sdk/platform-tools/adb.exe','-s','c2b670a0']
    log=subprocess.check_output(adb+['logcat','-d','--pid='+pid[1],'-v','threadtime']).decode(errors='replace');(a.directory/'validation.txt').write_text(log,encoding='utf-8')
    assert 'Loaded layer VK_LAYER_KHRONOS_validation' in log and 'GSR2_RESULT frames=32 failures=0 validation=1' in log and not re.search(r'VUID-|SYNC-HAZARD|Validation Error|Fatal signal|FATAL EXCEPTION',log)
    archive=a.directory/'outputs.tar'
    with archive.open('wb') as f:subprocess.run(adb+['exec-out','run-as','com.thor.nativefg.gsr2qa','tar','-cf','-','files/results'],stdout=f,check=True)
    ow,oh=m['output'];expected={f'frame-{f:03d}.rgba16f' for f in range(32)};found=set()
    with tarfile.open(archive) as tar:
        for member in tar:
            if not member.isfile():continue
            name=pathlib.PurePosixPath(member.name).name
            if name in ('diagnostic.bin','diagnostic-001.bin'):
                assert member.size==m['width']*m['height']*16
                (a.directory/name).write_bytes(tar.extractfile(member).read());continue
            assert name in expected and name not in found and member.size==ow*oh*8
            (a.directory/name).write_bytes(tar.extractfile(member).read());found.add(name)
    assert found==expected
    rows=[]
    for frame in m['frames']:
        i=frame['frame'];out=np.fromfile(a.directory/f'frame-{i:03d}.rgba16f',dtype='<f2').reshape(oh,ow,4).astype('f4')[...,:3]
        assert np.isfinite(out).all()
        source=np.asarray(Image.open(a.fixtures/f'frame-{i:03d}-input.png'),dtype='f4')[...,:3]/255
        reference=np.asarray(Image.open(a.fixtures/f'frame-{i:03d}-reference.png'),dtype='f4')[...,:3]/255
        linear=bilinear(source,ow,oh,frame['jitter']);crop=(slice(8,-8),slice(8,-8))
        row=dict(frame=i,reset=frame['reset'],gsr2_mse=float(np.mean((out[crop]-reference[crop])**2)),bilinear_mse=float(np.mean((linear[crop]-reference[crop])**2)),minimum=float(out.min()),maximum=float(out.max()))
        rows.append(row)
        if i in (0,7,15,16,23,31):
            side=np.concatenate((reference,linear,out),axis=1);Image.fromarray(np.rint(np.clip(side,0,1)*255).astype('u1')).save(a.directory/f'comparison-{i:03d}.png')
    settled=[r for r in rows if r['frame']%16>=4];summary={}
    for name in ('gsr2','bilinear'):
        mse=float(np.mean([r[name+'_mse'] for r in settled]));summary[name+'_mse']=mse
        summary[name+'_psnr']=float(-10*np.log10(mse)) if mse>0 else None # null means exact/infinite PSNR
    times=[float(t) for t in re.findall(r'GSR2_FRAME frame=\d+ gpu_ms=([\d.]+)',log)];assert len(times)==32
    scene='Synthetic3D/static geometry/lateral camera' if m.get('scene3d') else 'Two synthetic moving planes'
    report=dict(pid=int(pid[1]),native_sha256=native[1],fixtures_sha256=m['sha256'],input=[m['width'],m['height']],output=m['output'],summary=summary,gpu_median_ms=float(np.median(times[4:])),rows=rows,limits=scene+'; short GPU stage timestamps exclude upload/readback/game/display/input latency. Finite output is not correctness/equivalence proof.')
    (a.directory/'report.json').write_text(json.dumps(report,indent=2,allow_nan=False));print(json.dumps({k:v for k,v in report.items() if k!='rows'},indent=2,allow_nan=False))
if __name__=='__main__':main()
