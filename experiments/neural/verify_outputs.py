"""Compare actual Thor GPU readbacks to independent CPU model interpretations.

These are implementation-correctness checks, not perceptual SR quality scores
against a native-resolution game. Keep that distinction in reported PSNR.
"""
import argparse
import json
import pathlib

import numpy as np


def error(actual, expected):
    difference = actual.astype('f4') - expected.astype('f4')
    return dict(max_abs=float(np.abs(difference).max()), mae=float(np.abs(difference).mean()),
                psnr=float(-10 * np.log10(max(1e-16, np.mean(difference ** 2)))))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('research', type=pathlib.Path)
    args = parser.parse_args()
    root = args.research
    expected = np.fromfile(root / 'quicksrnet/models/expected-32x32.f32', dtype='f4')
    result = {}
    for mode, limit in [('fp32', 1e-4), ('fp16', .02)]:
        actual = np.fromfile(root / f'quicksrnet/gpu-32x32-{mode}.f32', dtype='f4')
        assert actual.shape == expected.shape
        metrics = error(actual, expected)
        assert metrics['max_abs'] < limit, f'QNN GPU {mode} model conversion differs'
        result['qnn_gpu_' + mode] = metrics
    for mode, name in [('naive', 'vulkan-32x32.rgba8'), ('shared', 'vulkan-shared-32x32.rgba8')]:
        rgba = np.fromfile(root / 'quicksrnet' / name, dtype='u1').reshape(64, 64, 4)
        actual = rgba[:, :, :3].transpose(2, 0, 1).reshape(-1).astype('f4') / 255
        metrics = error(actual, expected)
        assert metrics['max_abs'] < 2 / 255
        assert (rgba[:, :, 3] == 255).all()
        result['quicksrnet_vulkan_' + mode] = metrics
    expected_anime = np.fromfile(root / 'anime4k/generated/expected-32x32.f32', dtype='f4').reshape(64, 64, 4)
    actual_anime = np.fromfile(root / 'anime4k/vulkan-32x32.rgba8', dtype='u1').reshape(64, 64, 4)
    metrics = error(actual_anime[:, :, :3] / 255, expected_anime[:, :, :3])
    metrics['max_byte_difference'] = int(np.abs(actual_anime.astype('i4') - np.rint(expected_anime * 255).astype('i4')).max())
    assert metrics['max_byte_difference'] <= 1, 'Adapted CNN shader differs from upstream matrix reference'
    assert np.array_equal(actual_anime[:, :, 3], np.rint(expected_anime[:, :, 3] * 255).astype('u1'))
    result['anime4k_vulkan'] = metrics
    profile = json.loads((root / 'quicksrnet/gpu32-fp32-profile.json').read_text())
    providers = sorted({entry.get('args', {}).get('provider') for entry in profile if entry.get('args', {}).get('provider')})
    assert providers == ['QNNExecutionProvider'], f'Unexpected execution providers: {providers}'
    result['profile_providers'] = providers
    (root / 'neural-output-verification.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))
    print('PASS model implementation comparisons; gameplay/native-quality validation remains required')


if __name__ == '__main__':
    main()
