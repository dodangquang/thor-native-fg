"""Independent CPU interpretation of upstream CNN matrices and resampling.

Generates small correctness fixtures; never used to render application frames.
"""
import hashlib
import pathlib
import re
import struct
import sys

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent


def resize(image, width, height):
    old_h, old_w, _ = image.shape
    y, x = np.indices((height, width))
    px, py = (x + .5) * old_w / width - .5, (y + .5) * old_h / height - .5
    x0, y0 = np.floor(px).astype(int), np.floor(py).astype(int)
    fx, fy = (px - x0)[..., None], (py - y0)[..., None]
    xx, yy = x0.clip(0, old_w - 1), y0.clip(0, old_h - 1)
    x1, y1 = (x0 + 1).clip(0, old_w - 1), (y0 + 1).clip(0, old_h - 1)
    return ((image[yy, xx] * (1 - fx) + image[yy, x1] * fx) * (1 - fy)
            + (image[y1, xx] * (1 - fx) + image[y1, x1] * fx) * fy).astype('f4')


def reconstruct(rgba, sections):
    current = rgba.astype('f4') / 255
    height, width, _ = current.shape
    source = current.copy()
    for index, section in enumerate(sections):
        terms = re.findall(r'mat4\(([^)]+)\)\s*\*\s*go_([01])\(([-\d.]+),\s*([-\d.]+)\)', section)
        assert len(terms) == (9 if index == 0 else 18)
        bias = re.search(r'result\s*\+=\s*vec4\(([^)]+)\)', section)
        result = np.broadcast_to(np.array(bias.group(1).split(','), dtype='f4'), current.shape).copy()
        padded = np.pad(current, ((1, 1), (1, 1), (0, 0)), mode='edge')
        for values, branch, x, y in terms:
            matrix = np.array(values.split(','), dtype='f4').reshape(4, 4).T
            xx, yy = int(float(x)) + 1, int(float(y)) + 1
            sample = padded[yy:yy + height, xx:xx + width]
            if index:
                sample = np.maximum(sample if branch == '0' else -sample, 0)
            result += sample @ matrix.T
        # Reference preserves FP32 math. The actual GPU stores hidden features
        # in RGBA16F, so a small output tolerance must cover that representation.
        current = result
    y, x = np.indices((height * 2, width * 2))
    output = resize(source, width * 2, height * 2)
    output[..., :3] += current[y // 2, x // 2, (y % 2) * 2 + x % 2][..., None]
    return np.rint(np.clip(output, 0, 1) * 255).astype('u1')


def main():
    shader = (ROOT / 'third_party/anime4k/Anime4K_Upscale_CNN_x2_S.glsl').read_bytes()
    assert hashlib.sha256(shader).hexdigest() == '4c53ec2e287908f7ee7bcb266b0170421626d663576468b7d7dafc62962649a4'
    sections = re.split(r'^//!DESC ', shader.decode(), flags=re.M)[1:5]
    specs = [(32, 32, 0, 0, 32, 32, 64, 64), (32, 32, 0, 0, 32, 32, 64, 64),
             (32, 32, 0, 0, 32, 32, 48, 48), (40, 40, 4, 5, 32, 32, 48, 48),
             (40, 40, 4, 5, 32, 32, 80, 80), (35, 34, 2, 3, 31, 29, 57, 41)]
    output = pathlib.Path(sys.argv[1])
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('wb') as file:
        file.write(struct.pack('<8sI', b'THORNSR1', len(specs)))
        for index, spec in enumerate(specs):
            sw, sh, cx, cy, width, height, ow, oh = spec
            y, x = np.indices((sh, sw))
            source = np.stack([(x * 7 + y * 3 + index * 11) % 256,
                               (x * 2 + y * 13) % 256, (x * 11 ^ y * 5) % 256,
                               np.full_like(x, 255) if index == 0 else (x * 9 + y * 17) % 256], axis=-1).astype('u1')
            crop = source[cy:cy + height, cx:cx + width]
            neural = reconstruct(crop, sections)
            if (ow, oh) != (width * 2, height * 2):
                neural = np.rint(resize(neural.astype('f4'), ow, oh)).clip(0, 255).astype('u1')
                neural[..., 3] = np.rint(resize(crop.astype('f4'), ow, oh)[..., 3]).clip(0, 255).astype('u1')
            file.write(struct.pack('<8I', *spec))
            file.write(source.tobytes())
            file.write(neural.tobytes())
    print(f'Prepared {len(specs)} independent neural SR fixtures with alpha/crop/odd sizes and2x/1.5x/2.5x resize')


if __name__ == '__main__':
    main()
