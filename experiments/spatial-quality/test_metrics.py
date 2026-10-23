"""Known numeric controls for the quality measurement (no trained-model goldens)."""
import unittest

import numpy as np

from score import bilinear, metrics, psnr, ssim, source_limits


class MetricControls(unittest.TestCase):
    def test_half_pixel_clamp(self):
        source = np.repeat(np.array([[0, 64], [128, 192]], dtype='u1')[..., None], 4, axis=-1)
        source[..., 3] = 255
        expected = np.array([[0, 16, 48, 64], [32, 48, 80, 96],
                             [96, 112, 144, 160], [128, 144, 176, 192]], dtype='u1')
        actual = bilinear(source, 4, 4)
        np.testing.assert_array_equal(actual[..., 0], expected)
        np.testing.assert_array_equal(actual[..., 3], np.full((4, 4), 255))

    def test_identity(self):
        x = np.arange(32 * 32, dtype='f4').reshape(32, 32) / (32 * 32)
        self.assertAlmostEqual(ssim(x, x), 1, places=6)
        self.assertEqual(psnr(0), 'inf')

    def test_uniform_one_byte_error(self):
        reference = np.full((32, 32, 4), 100, dtype='u1');reference[..., 3] = 255
        test = reference.copy();test[..., :3] += 1
        result, _ = metrics(reference, test)
        self.assertAlmostEqual(result['rgb_psnr_db'], 48.1308, places=3)
        self.assertAlmostEqual(result['hud_y_mae'], 1 / 255, places=6)
        self.assertEqual(result['alpha_max_error'], 0)

    def test_constant_ssim(self):
        a = np.full((32, 32), .4, dtype='f4');b = np.full((32, 32), .6, dtype='f4')
        expected = (2 * .4 * .6 + .01 ** 2) / (.4 ** 2 + .6 ** 2 + .01 ** 2)
        self.assertAlmostEqual(ssim(a, b), expected, places=4)

    def test_alpha_is_not_hidden_by_rgb(self):
        a = np.full((32, 32, 4), 127, dtype='u1');b = a.copy();b[12, 15, 3] = 0
        result, _ = metrics(a, b)
        self.assertEqual(result['rgb_psnr_db'], 'inf')
        self.assertEqual(result['alpha_max_error'], 127)

    def test_odd_size_center_footprint(self):
        y, x = np.indices((29, 31));source=np.stack((x, y, x ^ y, np.full_like(x,255)),axis=-1).astype('u1')
        low, high = source_limits(source, 57, 41)
        np.testing.assert_array_equal(low[20, 28], [15,14,0])
        np.testing.assert_array_equal(high[20, 28], [16,15,31])


if __name__ == '__main__':unittest.main()
