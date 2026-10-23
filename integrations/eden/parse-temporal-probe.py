"""Summarize actual Eden attachment traces; do not infer temporal semantics."""
import argparse
import json
import pathlib
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('log', type=pathlib.Path)
parser.add_argument('--out', type=pathlib.Path, required=True)
args = parser.parse_args()
root = pathlib.Path(__file__).resolve().parents[2]
surface = root / 'research/eden-thor/src/video_core/surface.h'
formats = re.findall(r'PIXEL_FORMAT_ELEM\(([A-Z][A-Z0-9_]+),', surface.read_text(encoding='utf-8'))
def pixel_format(value):
    return formats[value] if value < len(formats) else 'Invalid' if value == 255 else f'Unknown({value})'
frames = []
frame = target = None
for line in args.log.read_text(encoding='utf-8', errors='replace').splitlines():
    header = re.search(r'THOR_TEMPORAL_PROBE token=(\d+) frame=(\d+) final_color=([0-9a-f]+) (\d+)x(\d+) targets=(\d+) overflow=(\d+)', line)
    if header:
        token, number, image, width, height, count, overflow = header.groups()
        frame = dict(token=int(token), frame=int(number), final_color=image,
                     final_extent=[int(width), int(height)], declared_targets=int(count),
                     overflow_draws=int(overflow), targets=[])
        frames.append(frame)
        target = None
        continue
    match = re.search(r'THOR_TARGET fb=([0-9a-f]+) (\d+)x(\d+) colors=(\d+) samples=(\d+) depth=([0-9a-f]+) depth_format=(\d+) draws=(\d+) depth_tests=(\d+) depth_writes=(\d+)', line)
    if frame and match:
        fb, width, height, colors, samples, depth, fmt, draws, tests, writes = match.groups()
        target = dict(framebuffer=fb, render_area=[int(width), int(height)],
                      declared_colors=int(colors), samples=int(samples), depth=depth,
                      depth_format=pixel_format(int(fmt)), draws=int(draws),
                      depth_tests=int(tests), depth_writes=int(writes), colors=[])
        frame['targets'].append(target)
        continue
    depth_size = re.search(r'THOR_DEPTH_SIZE (\d+)x(\d+)', line)
    if target and depth_size:
        target['depth_view_extent'] = list(map(int, depth_size.groups()))
    match = re.search(r'THOR_COLOR slot=(\d+) image=([0-9a-f]+) format=(\d+)(?: extent=(\d+)x(\d+))?', line)
    if target and match:
        slot, image, fmt, width, height = match.groups()
        color = dict(slot=int(slot), image=image, format=pixel_format(int(fmt)))
        if width is not None:
            color['view_extent'] = [int(width), int(height)]
        target['colors'].append(color)
for frame in frames:
    frame['parsed_complete'] = len(frame['targets']) == frame['declared_targets'] and all(
        len(target['colors']) == target['declared_colors'] for target in frame['targets'])
    frame['capture_complete'] = frame['parsed_complete'] and frame['overflow_draws'] == 0
    frame['direct_final_color_targets'] = [target['framebuffer'] for target in frame['targets']
        if any(color['image'] == frame['final_color'] for color in target['colors'])]
result = dict(scope='Attachment metadata only; depth/motion/jitter correctness NOT established',
              source=str(args.log), frames=frames)
args.out.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
if not frames:
    raise SystemExit('No probe frames found; no successful capture established')
for frame in frames:
    print(f"frame={frame['frame']} extent={frame['final_extent']} targets={len(frame['targets'])} "
          f"overflow_draws={frame['overflow_draws']} parsed_complete={frame['parsed_complete']} "
          f"direct_final_matches={len(frame['direct_final_color_targets'])}")
for target in frames[0]['targets']:
    if target['depth'] != '0' and target['declared_colors']:
        print(f"color+depth candidate {target['render_area']} {target['depth_format']} "
              f"draws={target['draws']} formats={[c['format'] for c in target['colors']]}")
