"""Author-owned paired raster scenes; low-res is rendered independently, not resized HR.

These fixtures diagnose spatial reconstruction, not actual-game compatibility or
perceptual DLSS equivalence. No external artwork, fonts, dataset or weights enter them.
"""
import argparse
import hashlib
import json
import pathlib
import struct

import numpy as np
from PIL import Image


GLYPHS = {
    'H': ('101', '101', '111', '101', '101'),
    'P': ('110', '101', '110', '100', '100'),
    'F': ('111', '100', '110', '100', '100'),
    'S': ('111', '100', '111', '001', '111'),
    '0': ('111', '101', '101', '101', '111'),
    '1': ('010', '110', '010', '010', '111'),
    '6': ('111', '100', '111', '101', '111'),
    ' ': ('000',) * 5,
}
HUD = np.zeros((5, 4 * len('HP100 FPS60')), dtype='f4')
for index, char in enumerate('HP100 FPS60'):
    HUD[:, index * 4:index * 4 + 3] = np.array([list(row) for row in GLYPHS[char]], dtype='f4')


def normal(v):
    return v / np.maximum(np.sqrt((v * v).sum(axis=-1, keepdims=True)), 1e-8)


def scene_3d(u, v, frame, kind):
    aspect = u.shape[1] / u.shape[0]
    dx = (u * 2 - 1) * aspect * .58
    dy = (1 - v * 2) * .58 - .18
    rays = normal(np.stack((dx, dy, np.ones_like(u)), axis=-1))
    origin = np.array([frame * .008, 1.35, -3.5], dtype='f4')
    color = np.stack((.12 + .06 * v, .21 + .10 * v, .32 + .13 * v), axis=-1)
    floor_t = -origin[1] / np.where(rays[..., 1] < -1e-5, rays[..., 1], -1e-5)
    valid_floor = (rays[..., 1] < 0) & (floor_t < 22)
    point = origin + rays * floor_t[..., None]
    x, z = point[..., 0], point[..., 2]
    checker = ((np.floor(x * 2) + np.floor(z * 2)) % 2).astype('f4')
    grout = ((np.mod(x * 2, 1) < .035) | (np.mod(z * 2, 1) < .035)).astype('f4')
    grain = .025 * np.sin(x * 90 + np.sin(z * 17)) * np.cos(z * 74)
    floor = (.16 + checker * .15 + grain - grout * .10)[..., None] * np.array([.85, .95, 1.0], dtype='f4')
    fog = np.clip(floor_t / 28, 0, .7)[..., None]
    floor = floor * (1 - fog) + color * fog
    color = np.where(valid_floor[..., None], floor, color)
    depth = np.where(valid_floor, floor_t, np.inf)
    light = normal(np.array([-1.0, 2.5, -1.5], dtype='f4'))
    for center, radius, base in [((-.85, .66, .3), .66, (.48, .10, .035)),
                                 ((.85, .51, 1.45), .51, (.035, .24, .44)),
                                 ((.2 + frame * .008, .32, -.55), .30, (.25, .40, .045))]:
        center = np.array(center, dtype='f4')
        oc = origin - center
        b = (rays * oc).sum(axis=-1)
        discriminant = b * b - ((oc * oc).sum() - radius * radius)
        distance = -b - np.sqrt(np.maximum(discriminant, 0))
        mask = (discriminant >= 0) & (distance > 0) & (distance < depth)
        hit = origin + rays * distance[..., None]
        n = normal(hit - center)
        diffuse = np.maximum((n * light).sum(axis=-1), 0)
        specular = np.maximum((normal(light - rays) * n).sum(axis=-1), 0) ** 70
        stripes = .82 + .18 * np.sin(hit[..., 1] * 64 + hit[..., 0] * 18)
        sphere = np.array(base, dtype='f4') * (stripes * (.23 + .77 * diffuse))[..., None] + specular[..., None] * .6
        color = np.where(mask[..., None], sphere, color)
        depth = np.where(mask, distance, depth)
    # A foreground fence and a textured card exercise thin features/disocclusion.
    t = (1.0 - origin[2]) / rays[..., 2]
    p = origin + rays * t[..., None]
    card = (np.abs(p[..., 0] - 1.8) < .5) & (p[..., 1] > .2) & (p[..., 1] < 1.8) & (t < depth)
    tex = .5 + .35 * np.sin(p[..., 0] * 32) * np.cos(p[..., 1] * 29)
    color = np.where(card[..., None], np.stack((tex * .7, tex * .3, tex * .1), axis=-1), color)
    t = (-.95 - origin[2]) / rays[..., 2]
    p = origin + rays * t[..., None]
    fence = (p[..., 1] > .08) & (p[..., 1] < .7) & (np.abs(p[..., 0]) < 1.8) & (np.mod(p[..., 0] * 7, 1) < .10) & (t < depth)
    color = np.where(fence[..., None], np.array([.045, .055, .065], dtype='f4'), color)
    if kind == 'dark3d':
        color *= .12
        color[..., 2] += .006
    return color


def scene_2d(u, v, frame):
    x, y = (u - .5 - frame * .0007) * 16 / 9, v - .5
    color = np.stack((.12 + v * .22, .31 + v * .15, .5 + v * .07), axis=-1)
    bands = np.sin(x * 19 + np.sin(y * 7))
    color += (bands > .8)[..., None] * .06
    head = (x * x / .22 ** 2 + y * y / .32 ** 2) < 1
    outline = (x * x / .23 ** 2 + y * y / .33 ** 2) < 1
    color = np.where(outline[..., None], np.array([.012, .018, .025]), color)
    color = np.where(head[..., None], np.array([.78, .51, .29]), color)
    for eye in [-.09, .09]:
        outline = ((x - eye) ** 2 / .05 ** 2 + (y + .045) ** 2 / .036 ** 2) < 1
        inside = ((x - eye) ** 2 / .043 ** 2 + (y + .045) ** 2 / .028 ** 2) < 1
        pupil = ((x - eye + frame * .0005) ** 2 / .014 ** 2 + (y + .045) ** 2 / .025 ** 2) < 1
        color = np.where(outline[..., None], .01, color)
        color = np.where(inside[..., None], .9, color)
        color = np.where(pupil[..., None], np.array([.015, .12, .2]), color)
    hair = head & (y < -.16 + .025 * np.sin(x * 67))
    color = np.where(hair[..., None], np.array([.022, .035, .055]), color)
    smile = (np.abs(y - .12 - x * x * 1.2) < .005) & (np.abs(x) < .095)
    color = np.where(smile[..., None], .015, color)
    return color.astype('f4')


def render(width, height, kind, frame, samples):
    total = np.zeros((height, width, 3), dtype='f4')
    offsets = [.5] if samples == 1 else [.25, .75]
    for oy in offsets:
        for ox in offsets:
            u, v = np.meshgrid((np.arange(width, dtype='f4') + ox) / width,
                               (np.arange(height, dtype='f4') + oy) / height)
            if kind in ('textured3d', 'dark3d'):
                color = scene_3d(u, v, frame, kind)
            elif kind == 'lineart2d':
                color = scene_2d(u, v, frame)
            elif kind == 'chart':
                x, y = (u - .5) * 2, (v - .5) * 2
                frequency = 15 + u * 100
                pattern = (np.sin((x + frame * .001) * frequency * 6.28) > 0).astype('f4')
                circles = np.sin((x * x + y * y) * 95 + frame * .02) > 0
                color = np.repeat(np.where(v < .5, pattern, circles)[..., None], 3, axis=-1) * .7 + .03
            else:
                color = np.stack((u * .8, v * .8, u * v * .8), axis=-1)
            # HUD uses author-owned glyph masks in normalized scene coordinates.
            box = (u > .0125) & (u < .35) & (v > .02) & (v < .105)
            color = np.where(box[..., None], np.array([.012, .018, .025]), color)
            hx = np.floor((u * 640 - 14) / 2).astype('i4')
            hy = np.floor((v * 360 - 14) / 2).astype('i4')
            text = (hx >= 0) & (hx < HUD.shape[1]) & (hy >= 0) & (hy < 5)
            text &= HUD[np.clip(hy, 0, 4), np.clip(hx, 0, HUD.shape[1] - 1)] > 0
            color = np.where(text[..., None], np.array([.82, .95, .76]), color)
            total += np.clip(color, 0, 1)
    linear = total / len(offsets) ** 2
    rgb = np.where(linear <= .0031308, linear * 12.92, 1.055 * linear ** (1 / 2.4) - .055)
    rgba = np.empty((height, width, 4), dtype='u1')
    rgba[..., :3] = np.rint(np.clip(rgb, 0, 1) * 255).astype('u1')
    rgba[..., 3] = 255
    return rgba


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('directory', type=pathlib.Path)
    parser.add_argument('--tiny', action='store_true', help='Two tiny fixtures for validation/control plumbing')
    parser.add_argument('--aa-input', action='store_true', help='Render four subpixel samples per low-res pixel')
    parser.add_argument('--holdout-poses', action='store_true', help='Different poses from the initial development corpus')
    args = parser.parse_args()
    args.directory.mkdir(parents=True, exist_ok=True)
    specs = []
    if args.tiny:
        specs = [('gradient', 0, 32, 24, 64, 48), ('textured3d', 0, 48, 27, 72, 41)]
    elif args.holdout_poses:
        for kind, frames in [('textured3d', range(-11, -7)), ('lineart2d', range(15, 19)), ('dark3d', range(41, 45))]:
            specs.extend((kind, frame, 320, 180, 640, 360) for frame in frames)
        specs.extend([('textured3d', 13, 480, 270, 720, 405), ('lineart2d', -15, 480, 270, 720, 405),
                      ('textured3d', 20, 1024, 576, 1920, 1080), ('lineart2d', -19, 1024, 576, 1920, 1080)])
    else:
        for kind, frames in [('textured3d', 8), ('lineart2d', 8), ('chart', 4), ('dark3d', 4), ('gradient', 1)]:
            specs.extend((kind, frame, 320, 180, 640, 360) for frame in range(frames))
        specs.extend(('textured3d', frame, 480, 270, 720, 405) for frame in range(4))
        specs.extend((kind, 0, 960, 540, 1920, 1080) for kind in ('textured3d', 'lineart2d'))
        specs.extend((kind, 0, 1024, 576, 1920, 1080) for kind in ('textured3d', 'lineart2d'))
        specs.append(('textured3d', 0, 320, 180, 960, 540))
    records = []
    binary = args.directory / 'fixtures.bin'
    with binary.open('wb') as out:
        out.write(struct.pack('<8sI', b'THORQLT1', len(specs)))
        for index, (kind, frame, width, height, ow, oh) in enumerate(specs):
            source = render(width, height, kind, frame, 4 if args.aa_input else 1)
            reference = render(ow, oh, kind, frame, 4)
            source_name, reference_name = f'case-{index:03d}-input.png', f'case-{index:03d}-reference.png'
            Image.fromarray(source).save(args.directory / source_name)
            Image.fromarray(reference).save(args.directory / reference_name)
            out.write(struct.pack('<4I', width, height, ow, oh))
            out.write(source.tobytes())
            records.append(dict(id=index, kind=kind, frame=frame, input=[width, height], output=[ow, oh],
                                source=source_name, reference=reference_name,
                                source_sha256=hashlib.sha256(source.tobytes()).hexdigest(),
                                reference_sha256=hashlib.sha256(reference.tobytes()).hexdigest()))
            print(f'Prepared case{index} {kind} frame{frame} {width}x{height}->{ow}x{oh}', flush=True)
    manifest = dict(version=1, author_owned=True, source_samples=4 if args.aa_input else 1, reference_samples=4,
                    pose_set='additional unseen poses, same synthetic scene families' if args.holdout_poses else 'initial development corpus',
                    sampling='Independent ray/analytic scene evaluation; linear supersampling then sRGB encoding',
                    limits='Synthetic, not native-game/DLSS equivalence; finite sampling may alias, same small scene families',
                    generator_sha256=hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),
                    fixtures_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(), cases=records)
    (args.directory / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
