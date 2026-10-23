"""Export Qualcomm's frozen QuickSRNet Small 2x weights without executing pickle code.

Requires numpy, onnx and onnxruntime in the experiment Python environment.
The checkpoint parser accepts only the storage/tensor/container symbols used by
this exact SHA-pinned checkpoint; it never imports torch or its optimizer.
"""
import argparse
import collections
import hashlib
import io
import json
import pathlib
import pickle
import sys
import struct
import zipfile

import numpy as np
import onnx
from onnx import TensorProto, helper, numpy_helper

CHECKPOINT_SHA = 'd95d70f1d2366cb9c28d99f8c7aa5bb07e1ffeaf5d7e30d9c66ab0fa28c6d0f8'
CHECKPOINT_URL = 'https://github.com/quic/aimet-model-zoo/releases/download/phase_2_january_artifacts/quicksrnet_small_2x_checkpoint_float32.pth.tar'


class FloatStorage:
    pass


class TensorValue:
    # Optimizer metadata uses parameter objects as dictionary keys. Keep tensor
    # identity hashable while parsing, then extract only the model's arrays.
    def __init__(self, array):
        self.array = array


class OpaqueOptimizer:
    def __init__(self, *args, **kwargs):
        pass


def tensor(storage, offset, shape, stride, *_):
    if len(shape) > 8 or len(shape) != len(stride) or offset < 0:
        raise ValueError('Invalid tensor metadata')
    if any(n < 0 or s < 0 for n, s in zip(shape, stride)):
        raise ValueError('Negative shape/stride')
    last = offset + sum(max(0, n - 1) * s for n, s in zip(shape, stride))
    if last >= storage.size:
        raise ValueError('Tensor exceeds storage')
    return TensorValue(np.ndarray(shape, dtype='<f4', buffer=storage, offset=offset * 4,
                                  strides=tuple(s * 4 for s in stride)).copy())


class WeightsOnly(pickle.Unpickler):
    def __init__(self, archive):
        self.archive = archive
        super().__init__(io.BytesIO(archive.read('archive/data.pkl')))

    def find_class(self, module, name):
        allowed = {
            ('collections', 'OrderedDict'): collections.OrderedDict,
            ('collections', 'defaultdict'): collections.defaultdict,
            ('__builtin__', 'dict'): dict,
            ('torch', 'FloatStorage'): FloatStorage,
            ('torch._utils', '_rebuild_tensor_v2'): tensor,
            ('torch._utils', '_rebuild_parameter'): lambda value, *_: value,
            # The training metadata stores the optimizer class. It is not run.
            ('torch.optim.adam', 'Adam'): OpaqueOptimizer,
        }
        if (module, name) not in allowed:
            raise ValueError(f'Unsupported pickle symbol {module}.{name}')
        return allowed[module, name]

    def persistent_load(self, item):
        kind, dtype, key, _location, count = item
        if kind != 'storage' or dtype is not FloatStorage or not str(key).isdigit():
            raise ValueError('Unsupported storage')
        raw = self.archive.read(f'archive/data/{key}')
        if len(raw) != count * 4 or len(raw) > 8 * 1024 * 1024:
            raise ValueError('Unexpected storage size')
        return np.frombuffer(raw, dtype='<f4')


def load_weights(path):
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != CHECKPOINT_SHA:
        raise ValueError('Checkpoint SHA256 mismatch')
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        state = WeightsOnly(archive).load()['state_dict']
    layers = []
    for name, channels in [('cnn.0', (32, 3)), ('cnn.2', (32, 32)),
                           ('cnn.4', (32, 32)), ('conv_last', (12, 32))]:
        weights, bias = state[name + '.weight'].array, state[name + '.bias'].array
        assert weights.shape == (*channels, 3, 3) and bias.shape == (channels[0],)
        assert np.isfinite(weights).all() and np.isfinite(bias).all()
        layers.append((weights, bias))
    assert len(state) == 8
    return layers


def export(layers, width, height, quantized, half=False):
    nodes, values = [], []
    def const(name, array):
        values.append(numpy_helper.from_array(np.asarray(array), name))
        return name
    scalar = np.float16 if half else np.float32
    zero = const('clip_zero', scalar(0))
    one = const('clip_one', scalar(1))
    scale = const('activation_scale', np.float32(1 / 255))
    offset = const('activation_zero', np.uint8(0))
    def qdq(name, source):
        nodes.append(helper.make_node('QuantizeLinear', [source, scale, offset], [name + '_q']))
        nodes.append(helper.make_node('DequantizeLinear', [name + '_q', scale, offset], [name]))
        return name
    current = qdq('input_dq', 'image') if quantized else 'image'
    for index, (original_weights, original_bias) in enumerate(layers):
        weights, bias = original_weights.copy(), original_bias.copy()
        if index == 3:
            # PyTorch PixelShuffle is CRD; QNN uses DCR DepthToSpace. Reorder the
            # output channels, not spatial pixels or input feature channels.
            order = np.arange(12).reshape(3, 4).T.reshape(-1)
            weights, bias = weights[order], bias[order]
        prefix = f'layer{index}'
        if quantized:
            weight_scale = np.maximum(np.abs(weights).max(axis=(1, 2, 3)) / 127, 1e-12).astype('f4')
            quant = np.clip(np.rint(weights / weight_scale[:, None, None, None]), -127, 127).astype('i1')
            wq = const(prefix + '_wq', quant)
            ws = const(prefix + '_ws', weight_scale)
            wz = const(prefix + '_wz', np.zeros(weights.shape[0], dtype='i1'))
            weight_name = prefix + '_weight'
            nodes.append(helper.make_node('DequantizeLinear', [wq, ws, wz], [weight_name], axis=0))
            bias_scale = (weight_scale / 255).astype('f4')
            bq = const(prefix + '_bq', np.rint(bias / bias_scale).astype('i4'))
            bs = const(prefix + '_bs', bias_scale)
            bz = const(prefix + '_bz', np.zeros(bias.size, dtype='i4'))
            bias_name = prefix + '_bias'
            nodes.append(helper.make_node('DequantizeLinear', [bq, bs, bz], [bias_name], axis=0))
        else:
            weight_name = const(prefix + '_weight', weights.astype(scalar))
            bias_name = const(prefix + '_bias', bias.astype(scalar))
        convolution = prefix + '_conv'
        nodes.append(helper.make_node('Conv', [current, weight_name, bias_name], [convolution],
                                      pads=[1, 1, 1, 1], strides=[1, 1], kernel_shape=[3, 3]))
        if quantized:
            # All upstream hidden/output activations are Hardtanh(0,1). Bounded
            # uint8 QDQ implements that clipping; no calibration range is guessed.
            current = qdq(prefix + '_activation', convolution)
        else:
            current = prefix + '_activation'
            nodes.append(helper.make_node('Clip', [convolution, zero, one], [current]))
    nodes.append(helper.make_node('DepthToSpace', [current], ['shuffled' if quantized else 'output'],
                                  blocksize=2, mode='DCR'))
    if quantized:
        qdq('output', 'shuffled')
    dtype = TensorProto.FLOAT16 if half else TensorProto.FLOAT
    # Keep the graph free of unused constants so runtime logs describe actual work.
    used = {name for node in nodes for name in node.input}
    graph = helper.make_graph(nodes, 'Thor_QuickSRNet_Small_2x',
        [helper.make_tensor_value_info('image', dtype, [1, 3, height, width])],
        [helper.make_tensor_value_info('output', dtype, [1, 3, 2 * height, 2 * width])],
        [value for value in values if value.name in used])
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid('', 13)],
                              producer_name='Thor neural SR experiment', ir_version=8)
    onnx.checker.check_model(model)
    return model


def reference(layers, image):
    # Independent direct convolution and original CRD PixelShuffle, not the
    # reordered ONNX graph. Small correctness fixtures only, never a game path.
    current = image[0].transpose(1, 2, 0)
    for weights, bias in layers:
        padded = np.pad(current, ((1, 1), (1, 1), (0, 0)))
        windows = np.lib.stride_tricks.sliding_window_view(padded, (3, 3), axis=(0, 1))
        current = np.clip(np.tensordot(windows, weights, axes=([2, 3, 4], [1, 2, 3])) + bias, 0, 1)
    h, w, _ = current.shape
    return current.reshape(h, w, 3, 2, 2).transpose(2, 0, 3, 1, 4).reshape(1, 3, h * 2, w * 2)


def packed_weights(layers):
    vectors, headers = [], []
    count = 0
    for index, (weights, bias) in enumerate(layers):
        if index == 3:
            order = np.arange(12).reshape(3, 4).T.reshape(-1)
            weights, bias = weights[order], bias[order]
        outputs, inputs, _, _ = weights.shape
        groups = (inputs + 3) // 4
        padded = np.zeros((outputs, groups * 4, 3, 3), dtype='f4')
        padded[:, :inputs] = weights
        columns = padded.reshape(outputs // 4, 4, groups, 4, 3, 3).transpose(0, 4, 5, 2, 3, 1).reshape(-1, 4).copy()
        headers.append((inputs, outputs, count, count + len(columns)))
        vectors.extend([columns, bias.reshape(-1, 4)])
        count += len(columns) + outputs // 4
    return struct.pack('<8sI', b'THORQS2\0', count) + b''.join(struct.pack('<4I', *h) for h in headers) + np.concatenate(vectors).astype('<f4').tobytes()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('checkpoint', type=pathlib.Path)
    parser.add_argument('output', type=pathlib.Path)
    parser.add_argument('--width', type=int, default=32)
    parser.add_argument('--height', type=int, default=32)
    args = parser.parse_args()
    if not 16 <= args.width <= 1920 or not 16 <= args.height <= 1080:
        raise ValueError('Dimensions outside experimental range')
    args.output.mkdir(parents=True, exist_ok=True)
    layers = load_weights(args.checkpoint)
    (args.output / 'quicksrnet-packed.bin').write_bytes(packed_weights(layers))
    tag = f'{args.width}x{args.height}'
    for mode in ['fp32', 'fp16', 'int8']:
        onnx.save(export(layers, args.width, args.height, mode == 'int8', mode == 'fp16'), args.output / f'quicksrnet-{tag}-{mode}.onnx')
    metadata = dict(checkpoint_url=CHECKPOINT_URL, checkpoint_sha256=CHECKPOINT_SHA,
                    architecture='QuickSRNetSmall 2x, 32 channels, 2 intermediate layers',
                    trained_scale=2, temporal=False, quantization='bounded activation uint8, per-channel symmetric int8 weights',
                    input=[1, 3, args.height, args.width])
    if args.width <= 64 and args.height <= 64:
        import onnxruntime as ort
        y, x = np.indices((args.height, args.width))
        image = np.stack([(x * 7 + y * 3) % 256, (x * 2 + y * 13) % 256,
                          (x * 11 ^ y * 5) % 256]).astype('f4')[None] / 255
        expected = reference(layers, image)
        errors = {}
        for mode in ['fp32', 'fp16', 'int8']:
            options = ort.SessionOptions()
            options.log_severity_level = 3
            session = ort.InferenceSession(str(args.output / f'quicksrnet-{tag}-{mode}.onnx'), sess_options=options, providers=['CPUExecutionProvider'])
            actual = session.run(None, {'image': image.astype('f2') if mode == 'fp16' else image})[0].astype('f4')
            difference = actual - expected
            errors[mode] = dict(max_abs=float(np.abs(difference).max()), mae=float(np.abs(difference).mean()),
                                psnr=float(-10 * np.log10(max(1e-16, np.mean(difference ** 2)))))
            if mode == 'fp32' and errors[mode]['max_abs'] > 1e-5:
                raise ValueError(f'Export differs from direct upstream reference: {errors[mode]}')
        image.tofile(args.output / f'input-{tag}.f32')
        rgba = np.full((args.height, args.width, 4), 255, dtype='u1')
        rgba[:, :, :3] = np.rint(image[0].transpose(1, 2, 0) * 255).astype('u1')
        rgba.tofile(args.output / f'input-{tag}.rgba8')
        expected.tofile(args.output / f'expected-{tag}.f32')
        metadata['reference_errors'] = errors
    (args.output / f'model-{tag}.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
    print(json.dumps(metadata, indent=2))


if __name__ == '__main__':
    main()
