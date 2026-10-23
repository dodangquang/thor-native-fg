"""Port official fragment math to compute for the existing headless harness.

This is NOT a fragment-pipeline performance measurement. Original sources stay
unchanged. Shared144byte UBO uses the validated compute-provider field layout.
"""
import argparse,pathlib,re

def adapt(source,name,stable=False,full=False):
    source=source.replace('#version 320 es','#version 450').replace('mediump','highp')
    common='''layout(std140,binding=0) uniform Params {
 uvec2 renderSize; uvec2 displaySize; vec2 renderSizeRcp; vec2 displaySizeRcp;
 vec2 jitterOffset; vec4 clipToPrevClip[4]; float preExposure; float cameraFovAngleHor;
 float cameraNear; float MinLerpContribution; uint sameCameraFrmNum; uint reset;
} params;'''
    source=re.sub(r'layout\(std140, set = 0, binding = 0\) uniform (?:readonly )?Params\s*\{.*?\} params;',common,source,flags=re.S)
    source=re.sub(r'layout\(location = 0\) (?:out.*?;|in.*?;)', '',source)
    source=re.sub(r'params\.renderSize\b','vec2(params.renderSize)',source)
    source=re.sub(r'params\.outputSize\b','vec2(params.displaySize)',source)
    source=source.replace('params.outputSizeRcp','params.displaySizeRcp').replace('params.minLerpContribution','params.MinLerpContribution').replace('params.bSameCamera','params.sameCameraFrmNum').replace('params.reset','float(params.reset)')
    source=source.replace('params.scaleRatio.x','(float(params.displaySize.x)/float(params.renderSize.x))').replace('params.scaleRatio.y','min(20.0,pow(float(params.displaySize.x*params.displaySize.y)/float(params.renderSize.x*params.renderSize.y),3.0))')
    if name=='convert':
        source=source.replace('binding = 1) uniform highp sampler2D InputDepth','binding = 2) uniform highp sampler2D InputDepth').replace('binding = 2) uniform highp sampler2D InputVelocity','binding = 3) uniform highp sampler2D InputVelocity')
        output='MotionDepthClipAlphaBuffer';size='renderSize'
        suffix='imageStore(result,ivec2(gl_GlobalInvocationID.xy),'+output+');'
        declaration='layout(binding=10,rgba16f) uniform writeonly image2D result;'
    else:
        source=source.replace('binding = 1) uniform highp sampler2D PrevOutput','binding = 7) uniform highp sampler2D PrevOutput').replace('binding = 2) uniform highp sampler2D MotionDepthClipAlphaBuffer','binding = 8) uniform highp sampler2D MotionDepthClipAlphaBuffer').replace('binding = 3) uniform highp sampler2D InputColor','binding = 1) uniform highp sampler2D InputColor')
        source=source.replace('ivec2 InputPos = ivec2(Jitteruv * vec2(params.renderSize));','ivec2 InputPos = clamp(ivec2(Jitteruv * vec2(params.renderSize)),ivec2(0),ivec2(params.renderSize)-1);')
        source=source.replace('texelFetch(InputColor,','fetchColor(')
        helper='vec4 fetchColor(ivec2 at,int lod){return texelFetch(InputColor,clamp(at,ivec2(0),textureSize(InputColor,lod)-1),lod);}\n'
        source=source.replace('void main()',helper+'void main()',1)
        output='Output';size='displaySize'
        suffix='imageStore(result,ivec2(gl_GlobalInvocationID.xy),Output);imageStore(history,ivec2(gl_GlobalInvocationID.xy),Output);'
        declaration='layout(binding=10,rgba16f) uniform writeonly image2D result;layout(binding=11,rgba16f) uniform writeonly image2D history;'
    prefix='layout(local_size_x=8,local_size_y=8) in;\n'+declaration+'\n'
    source=source.replace('void main()',prefix+'void main()',1)
    if stable and name=='upscale':
        source=source.replace('kernelbias *= 0.5f;','kernelbias = min(kernelbias, 1.0); // Thor: bound the radial kernel to positive normalization mass.\n    kernelbias *= 0.5f;')
    if full and name=='upscale':
        source=source.replace('if (false)  //maybe disable this for ultra performance, true could generate more realistic output','if (params.sameCameraFrmNum!=0u) // Thor: use upstream corner samples for a still camera')
    source=re.sub(r'void main\(\)\s*\{','void main(){\n if(any(greaterThanEqual(gl_GlobalInvocationID.xy,params.'+size+')))return;\n vec2 texCoord=(vec2(gl_GlobalInvocationID.xy)+.5)/vec2(params.'+size+');\n vec4 '+output+';\n',source,count=1)
    end=source.rfind('}');source=source[:end]+suffix+'\n'+source[end:]
    assert 'layout(location' not in source and 'params.scaleRatio' not in source
    return source

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('source',type=pathlib.Path);p.add_argument('output',type=pathlib.Path);p.add_argument('--stable',action='store_true');p.add_argument('--full',action='store_true');a=p.parse_args()
    for name in ('convert','upscale'):
        (a.output/f'{name}.comp').write_text(adapt((a.source/f'sgsr2_{name}.fs').read_text(),name,a.stable,a.full),encoding='utf-8')
