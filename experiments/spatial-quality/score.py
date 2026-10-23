"""Score actual production GPU readbacks against independent rendered references.

Numbers diagnose this synthetic corpus; do not turn them into universal quality,
native-game benefit, displayed FPS or input-latency claims.
"""
import argparse
import hashlib
import json
import math
import pathlib
import re

import numpy as np
from PIL import Image, ImageDraw, ImageFont

MODELS = ['bilinear', 'gsr1', 'neural2d']
LUMA = np.array([.299, .587, .114], dtype='f4')


def bilinear(source, width, height):
    # Independent host reference, half-pixel centers, clamp-to-edge, RGBA8 UNORM.
    sh, sw, _ = source.shape
    x = (np.arange(width, dtype='f8') + .5) * sw / width - .5
    y = (np.arange(height, dtype='f8') + .5) * sh / height - .5
    x0, y0 = np.floor(x).astype('i4'), np.floor(y).astype('i4')
    fx, fy = x - x0, y - y0
    x1, y1 = np.clip(x0 + 1, 0, sw - 1), np.clip(y0 + 1, 0, sh - 1)
    x0, y0 = np.clip(x0, 0, sw - 1), np.clip(y0, 0, sh - 1)
    top = source[y0[:, None], x0] * (1 - fx[None, :, None]) + source[y0[:, None], x1] * fx[None, :, None]
    bottom = source[y1[:, None], x0] * (1 - fx[None, :, None]) + source[y1[:, None], x1] * fx[None, :, None]
    return np.rint(top * (1 - fy[:, None, None]) + bottom * fy[:, None, None]).clip(0, 255).astype('u1')


def source_limits(source, width, height):
    sh, sw, _ = source.shape
    x = ((2 * np.arange(width) + 1) * sw - width) // (2 * width)
    y = ((2 * np.arange(height) + 1) * sh - height) // (2 * height)
    neighbors = [source[np.clip(y[:, None] + dy, 0, sh - 1), np.clip(x + dx, 0, sw - 1), :3]
                 for dy in range(2) for dx in range(2)]
    return np.minimum.reduce(neighbors), np.maximum.reduce(neighbors)


def blur(image):
    # Variance subtracts two nearly equal moments in flat regions. FP32 caused
    # a measurable SSIM bias on the independent constant-image control.
    image = np.asarray(image, dtype='f8')
    positions = np.arange(-5, 6, dtype='f8')
    weights = np.exp(-positions * positions / (2 * 1.5 ** 2));weights /= weights.sum()
    for axis in [0, 1]:
        pad = [(0, 0), (0, 0)];pad[axis] = (5, 5)
        padded = np.pad(image, pad, mode='reflect')
        output = np.zeros_like(image)
        for offset, weight in enumerate(weights):
            selection = [slice(None), slice(None)];selection[axis] = slice(offset, offset + image.shape[axis])
            output += padded[tuple(selection)] * weight
        image = output
    return image


def ssim(reference, test):
    reference, test = np.asarray(reference, dtype='f8'), np.asarray(test, dtype='f8')
    mu_ref, mu_test = blur(reference), blur(test)
    var_ref = np.maximum(blur(reference * reference) - mu_ref * mu_ref, 0)
    var_test = np.maximum(blur(test * test) - mu_test * mu_test, 0)
    covariance = blur(reference * test) - mu_ref * mu_test
    values = ((2 * mu_ref * mu_test + .01 ** 2) * (2 * covariance + .03 ** 2) /
              ((mu_ref * mu_ref + mu_test * mu_test + .01 ** 2) * (var_ref + var_test + .03 ** 2)))
    # Do not score the window's reflected border.
    return float(values[5:-5, 5:-5].mean())


def psnr(mse):
    return -10 * math.log10(mse) if mse else 'inf'


def metrics(reference, output):
    height, width, _ = reference.shape
    rgb_ref, rgb_test = reference[..., :3].astype('f4') / 255, output[..., :3].astype('f4') / 255
    y_ref, y_test = rgb_ref @ LUMA, rgb_test @ LUMA
    # Four display pixels excluded in every case, independent of model and scale.
    inside = (slice(4, -4), slice(4, -4))
    mse_rgb = float(np.mean((rgb_ref[inside] - rgb_test[inside]) ** 2))
    mse_y = float(np.mean((y_ref[inside] - y_test[inside]) ** 2))
    edge = np.zeros((height, width), dtype=bool)
    edge[1:-1, 1:-1] = (np.abs(y_ref[1:-1, 2:] - y_ref[1:-1, :-2]) +
                        np.abs(y_ref[2:, 1:-1] - y_ref[:-2, 1:-1])) > .05
    edge[:4] = edge[-4:] = False;edge[:, :4] = edge[:, -4:] = False
    edge_mse = float(np.mean((y_ref[edge] - y_test[edge]) ** 2)) if edge.any() else None
    hud = (slice(max(1, int(height * .02)), max(2, int(height * .105))),
           slice(max(1, int(width * .0125)), max(2, int(width * .35))))
    return dict(rgb_mse=mse_rgb, rgb_psnr_db=psnr(mse_rgb), y_mse=mse_y, y_psnr_db=psnr(mse_y),
                y_ssim_11x11_sigma1_5=ssim(y_ref, y_test), edge_y_mse=edge_mse,
                hud_y_mae=float(np.mean(np.abs(y_ref[hud] - y_test[hud]))),
                alpha_max_error=int(np.max(np.abs(output[..., 3].astype('i2') - reference[..., 3])))), y_test - y_ref


def aggregate(rows):
    rgb = float(np.mean([row['rgb_mse'] for row in rows]));y = float(np.mean([row['y_mse'] for row in rows]))
    edge = [row['edge_y_mse'] for row in rows if row['edge_y_mse'] is not None]
    return dict(cases=len(rows), rgb_psnr_from_mean_case_mse_db=psnr(rgb), y_psnr_from_mean_case_mse_db=psnr(y),
                mean_y_ssim=float(np.mean([row['y_ssim_11x11_sigma1_5'] for row in rows])),
                mean_hud_y_mae=float(np.mean([row['hud_y_mae'] for row in rows])),
                mean_edge_y_mse=float(np.mean(edge)) if edge else None)


def comparison_sheet(directory, case, reference, results):
    # Fixed central geometry crop, no search for whichever model looks best.
    width, height = case['output'];cx, cy = int(width * .52), int(height * .56)
    extent = min(256, width, height);box = (cx - extent // 2, cy - extent // 2, cx + extent // 2, cy + extent // 2)
    font_path = pathlib.Path('C:/Windows/Fonts/arial.ttf')
    font = ImageFont.truetype(str(font_path), 17) if font_path.is_file() else ImageFont.load_default()
    sheet = Image.new('RGB', ((1 + len(results)) * 300, 346), '#18212c');draw = ImageDraw.Draw(sheet)
    for index, (label, pixels) in enumerate([('reference', reference), *results.items()]):
        image = Image.fromarray(pixels).convert('RGB').crop(box).resize((300, 300), Image.Resampling.NEAREST)
        sheet.paste(image, (index * 300, 38));draw.text((index * 300 + 8, 8), label, font=font, fill='white')
    sheet.save(directory / f'case-{case["id"]:03d}-comparison.png')


def main():
    parser = argparse.ArgumentParser();parser.add_argument('fixtures', type=pathlib.Path);parser.add_argument('gpu', type=pathlib.Path)
    parser.add_argument('--log', type=pathlib.Path);parser.add_argument('--bounded', action='store_true');args = parser.parse_args()
    models = MODELS + (['bounded'] if args.bounded else [])
    manifest = json.loads((args.fixtures / 'manifest.json').read_text())
    if hashlib.sha256((args.fixtures / 'fixtures.bin').read_bytes()).hexdigest() != manifest['fixtures_sha256']:
        raise ValueError('Fixture manifest checksum mismatch')
    per_case = [];errors = {};times = []
    if args.log:
        for line in args.log.read_text(encoding='utf-8-sig').splitlines():
            if 'Validation Error' in line or 'SYNC-HAZARD' in line or 'VUID-' in line:
                raise ValueError('Validation failed; do not score this GPU run')
            if 'SPATIAL_LAB case=' in line:times.append(dict(re.findall(r'(\w+)=([^\s]+)', line)))
    for case in manifest['cases']:
        index = case['id'];width, height = case['output']
        source = np.array(Image.open(args.fixtures / case['source']).convert('RGBA'))
        reference = np.array(Image.open(args.fixtures / case['reference']).convert('RGBA'))
        for pixels, key in [(source, 'source_sha256'), (reference, 'reference_sha256')]:
            if hashlib.sha256(pixels.tobytes()).hexdigest() != case[key]:raise ValueError(f'PNG pixels changed: {index}')
        row = dict(case=case, models={});results = {}
        for model in models:
            path = args.gpu / f'case-{index:03d}-{model}.rgba8'
            if path.stat().st_size != width * height * 4:raise ValueError(f'GPU output size mismatch: {path}')
            output = np.fromfile(path, dtype='u1').reshape(height, width, 4)
            value, error = metrics(reference, output);value['output_sha256'] = hashlib.sha256(output.tobytes()).hexdigest()
            if value['alpha_max_error']:raise ValueError(f'Alpha corrupted: {path}')
            if model == 'bilinear':
                expected = bilinear(source, width, height)
                value['cpu_bilinear_max_byte_error'] = int(np.max(np.abs(expected.astype('i2') - output.astype('i2'))))
                if value['cpu_bilinear_max_byte_error'] > 1:raise ValueError(f'Independent bilinear control mismatch: {value}')
            if model == 'bounded':
                low, high = source_limits(source, width, height)
                rgb = output[..., :3].astype('i2')
                violation = int(np.maximum(np.maximum(low.astype('i2') - rgb, rgb - high.astype('i2')), 0).max())
                value['source_range_max_byte_violation'] = violation
                if violation:raise ValueError(f'Final neural bound failed: {index}, {violation}')
            row['models'][model] = value;results[model] = output
            errors[(case['kind'], tuple(case['input']), tuple(case['output']), model, case['frame'])] = error
        per_case.append(row)
        # First frame in every size/kind plus diagnostic chart; deterministic selection.
        if case['frame'] == 0:comparison_sheet(args.gpu, case, reference, results)
    summary = {model:aggregate([row['models'][model] for row in per_case]) for model in models}
    groups = {}
    for kind in sorted({row['case']['kind'] for row in per_case}):
        groups[kind] = {model:aggregate([row['models'][model] for row in per_case if row['case']['kind'] == kind]) for model in models}
    temporal = {model:[] for model in models}
    for key, error in errors.items():
        kind, size, out, model, frame = key;prior = errors.get((kind, size, out, model, frame - 1))
        if prior is not None:temporal[model].append(float(np.mean(np.abs(error[4:-4, 4:-4] - prior[4:-4, 4:-4]))))
    identity_path = args.gpu / 'run-identity.json'
    report = dict(fixtures_sha256=manifest['fixtures_sha256'], corpus_limits=manifest['limits'], source_samples=manifest['source_samples'], reference_samples=manifest['reference_samples'],pose_set=manifest.get('pose_set','initial development corpus'),run_identity=json.loads(identity_path.read_text()) if identity_path.exists() else None,
                  metric_protocol='Gamma-encoded RGB/BT.601 Y; four-display-pixel PSNR border; SSIM11 Gaussian sigma1.5 with5pixel border. Aggregate PSNR derives from mean case MSE, not mean PSNR.',
                  temporal_limits='Unwarped temporal reconstruction-error delta; includes motion/error changes, not an isolated perceptual flicker score.',
                  summary=summary, by_kind=groups,
                  temporal_error_delta_mae={model:dict(pairs=len(values),mean=float(np.mean(values)) if values else None) for model, values in temporal.items()},
                  gpu_times=times, cases=per_case)
    (args.gpu / 'report.json').write_text(json.dumps(report, indent=2, allow_nan=False), encoding='utf-8')
    print(json.dumps(dict(summary=summary, by_kind=groups, temporal_error_delta_mae=report['temporal_error_delta_mae']), indent=2))


if __name__ == '__main__':main()
