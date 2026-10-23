"""Require unchanged controls and compare bounded GPU output to an independent clamp."""
import argparse
import json
import pathlib

import numpy as np
from PIL import Image

from score import source_limits


def main():
    parser = argparse.ArgumentParser();parser.add_argument('fixtures', type=pathlib.Path);parser.add_argument('baseline', type=pathlib.Path);parser.add_argument('candidate', type=pathlib.Path);args = parser.parse_args()
    manifest = json.loads((args.fixtures / 'manifest.json').read_text());components = 0;controls = 0;bounded_components = 0;records = []
    for case in manifest['cases']:
        index = case['id'];width, height = case['output'];row = dict(id=index, control_max_byte_error={})
        for model in ['bilinear', 'gsr1', 'neural2d']:
            filename = f'case-{index:03d}-{model}.rgba8'
            before, after = [np.fromfile(directory / filename, dtype='u1') for directory in [args.baseline, args.candidate]]
            if len(before) != width * height * 4 or len(after) != len(before):raise ValueError('Control size mismatch')
            difference = int(np.abs(before.astype('i2') - after.astype('i2')).max());row['control_max_byte_error'][model] = difference
            if difference:raise ValueError(f'Changed control output: {index} {model} max={difference}')
            components += len(after);controls += 1
        source = np.array(Image.open(args.fixtures / case['source']).convert('RGBA'))
        raw = np.fromfile(args.candidate / f'case-{index:03d}-neural2d.rgba8', dtype='u1').reshape(height, width, 4)
        bounded = np.fromfile(args.candidate / f'case-{index:03d}-bounded.rgba8', dtype='u1').reshape(height, width, 4)
        low, high = source_limits(source, width, height);expected = raw.copy();expected[..., :3] = np.maximum(low, np.minimum(high, expected[..., :3]))
        row['bounded_cpu_max_byte_error'] = int(np.abs(expected.astype('i2') - bounded.astype('i2')).max())
        if row['bounded_cpu_max_byte_error']:raise ValueError(f'GPU final clamp differs from CPU definition: {row}')
        bounded_components += bounded.size;records.append(row)
    report = dict(fixtures_sha256=manifest['fixtures_sha256'], controls=controls, control_components=components,
                  bounded_components=bounded_components, control_max_byte_error=0, bounded_cpu_max_byte_error=0, cases=records)
    (args.candidate / 'control-comparison.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(f'SPATIAL_CONTROLS controls={controls} components={components} max_byte_error=0 bounded_components={bounded_components} bounded_cpu_max_byte_error=0')


if __name__ == '__main__':main()
