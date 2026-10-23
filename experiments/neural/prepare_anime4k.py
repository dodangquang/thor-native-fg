"""Adapt the pinned MIT-licensed Anime4K CNN S to Vulkan compute for experiments.

This is a line-art-trained single-image network, not a general DLSS replacement.
Hidden/output math is kept; source alpha is preserved rather than sharpened.
"""
import argparse
import hashlib
import pathlib
import re

import numpy as np

SHA = '4c53ec2e287908f7ee7bcb266b0170421626d663576468b7d7dafc62962649a4'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('upstream', type=pathlib.Path)
    parser.add_argument('output', type=pathlib.Path)
    args = parser.parse_args()
    raw = args.upstream.read_bytes()
    if hashlib.sha256(raw).hexdigest() != SHA:
        raise ValueError('Unexpected upstream shader SHA256')
    source = raw.decode('utf-8')
    sections = re.split(r'^//!DESC ', source, flags=re.M)[1:]
    if len(sections) != 5:
        raise ValueError('Unexpected upstream pass count')
    notice = source[:source.index('//!DESC')]
    args.output.mkdir(parents=True, exist_ok=True)
    prefix = '''#version 450
layout(local_size_x=8,local_size_y=8) in;
layout(binding=0) uniform sampler2D inputImage;
layout(binding=1,rgba16f) uniform writeonly image2D outputImage;
layout(push_constant) uniform Params {ivec2 size;} p;
'''
    for index, section in enumerate(sections[:4]):
        binding = re.search(r'^//!BIND (\w+)', section, re.M).group(1)
        body = section[section.index('#define'):]
        body = '\n'.join(line for line in body.splitlines() if not line.startswith('//!'))
        body = body.replace('vec4 hook()', 'vec4 infer()')
        sample = f'#define {binding}_texOff(offset) texelFetch(inputImage,clamp(ivec2(gl_GlobalInvocationID.xy)+ivec2(offset),ivec2(0),p.size-1),0)\n'
        entry = '\nvoid main(){ivec2 at=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(at,p.size)))return;imageStore(outputImage,at,infer());}\n'
        (args.output / f'anime{index}.comp').write_text(prefix + notice + sample + body + entry, encoding='utf-8')
    final = '''#version 450
layout(local_size_x=8,local_size_y=8) in;
layout(binding=0) uniform sampler2D inputImage;
layout(binding=1,rgba8) uniform writeonly image2D outputImage;
layout(binding=2) uniform sampler2D sourceImage;
layout(push_constant) uniform Params {ivec2 size;} p;
void main(){
 ivec2 at=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(at,p.size*2)))return;
 int phase=(at.y%2)*2+at.x%2;
 float residual=texelFetch(inputImage,at/2,0)[phase];
 vec4 source=textureLod(sourceImage,(vec2(at)+0.5)/vec2(p.size*2),0);
 imageStore(outputImage,at,vec4(source.rgb+residual,source.a));
}
'''
    (args.output / 'anime4.comp').write_text(final.split('void main')[0] + notice + 'void main' + final.split('void main')[1], encoding='utf-8')
    # Independent CPU interpretation of upstream matrices in column-major GLSL
    # order. Clamp-to-edge sampling matches the mpv texture sampler contract.
    rgba = np.fromfile(args.output.parent.parent / 'quicksrnet/models/input-32x32.rgba8', dtype='u1').reshape(32, 32, 4).astype('f4') / 255
    current = rgba
    for index, section in enumerate(sections[:4]):
        terms = re.findall(r'mat4\(([^)]+)\)\s*\*\s*go_([01])\(([-\d.]+),\s*([-\d.]+)\)', section)
        if len(terms) != (9 if index == 0 else 18):
            raise ValueError('Unexpected matrix count')
        bias = re.search(r'result\s*\+=\s*vec4\(([^)]+)\)', section)
        result = np.broadcast_to(np.array(bias.group(1).split(','), dtype='f4'), current.shape).copy()
        padded = np.pad(current, ((1, 1), (1, 1), (0, 0)), mode='edge')
        for values, branch, x, y in terms:
            matrix = np.array(values.split(','), dtype='f4').reshape(4, 4).T
            xx, yy = int(float(x)) + 1, int(float(y)) + 1
            sample = padded[yy:yy + 32, xx:xx + 32]
            if index:
                sample = np.maximum(sample if branch == '0' else -sample, 0)
            result += sample @ matrix.T
        current = result
    at_y, at_x = np.indices((64, 64))
    position_x, position_y = (at_x + .5) / 2 - .5, (at_y + .5) / 2 - .5
    x0, y0 = np.floor(position_x).astype(int), np.floor(position_y).astype(int)
    fx, fy = (position_x - x0)[..., None], (position_y - y0)[..., None]
    source = (rgba[y0.clip(0, 31), x0.clip(0, 31)] * (1 - fx) + rgba[y0.clip(0, 31), (x0 + 1).clip(0, 31)] * fx) * (1 - fy)
    source += (rgba[(y0 + 1).clip(0, 31), x0.clip(0, 31)] * (1 - fx) + rgba[(y0 + 1).clip(0, 31), (x0 + 1).clip(0, 31)] * fx) * fy
    source[..., :3] += current[at_y // 2, at_x // 2, (at_y % 2) * 2 + at_x % 2][..., None]
    np.clip(source, 0, 1).astype('f4').tofile(args.output / 'expected-32x32.f32')
    print('Prepared five Vulkan CNN passes and independent32x32 RGB/alpha reference')


if __name__ == '__main__':
    main()
