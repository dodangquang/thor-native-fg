"""Collect only this headless test package's results, checking fixture/run identity."""
import argparse
import json
import pathlib
import re
import subprocess
import tarfile


def main():
    parser = argparse.ArgumentParser();parser.add_argument('fixtures', type=pathlib.Path);parser.add_argument('directory', type=pathlib.Path);parser.add_argument('instrumentation', type=pathlib.Path)
    parser.add_argument('--adb', default='E:/DevTools/Android/Sdk/platform-tools/adb.exe');parser.add_argument('--serial', default='c2b670a0');parser.add_argument('--bounded', action='store_true');args = parser.parse_args()
    models = ['bilinear', 'gsr1', 'neural2d'] + (['bounded'] if args.bounded else [])
    manifest = json.loads((args.fixtures / 'manifest.json').read_text())
    run = args.instrumentation.read_text(encoding='utf-8-sig')
    pid = re.search(r'INSTRUMENTATION_RESULT: pid=(\d+)', run)
    checksum = re.search(r'INSTRUMENTATION_RESULT: fixtures_sha256=(\w+)', run)
    native = re.search(r'INSTRUMENTATION_RESULT: native_sha256=(\w+)', run)
    if not pid or not checksum or checksum.group(1) != manifest['fixtures_sha256'] or 'INSTRUMENTATION_CODE: -1' not in run:
        raise ValueError('Instrumentation did not complete successfully for these fixtures')
    args.directory.mkdir(parents=True, exist_ok=True);adb = [args.adb, '-s', args.serial]
    if args.bounded and not native:raise ValueError('Missing verified native-library identity')
    (args.directory / 'run-identity.json').write_text(json.dumps(dict(pid=int(pid.group(1)),fixtures_sha256=checksum.group(1),native_sha256=native.group(1) if native else None,models=models),indent=2),encoding='utf-8')
    log = subprocess.check_output(adb + ['logcat', '-d', '--pid=' + pid.group(1), '-v', 'threadtime']).decode('utf-8', errors='replace')
    (args.directory / 'validation-log.txt').write_text(log, encoding='utf-8')
    expected = f'SPATIAL_LAB_RESULT cases={len(manifest["cases"])} outputs={len(manifest["cases"]) * len(models)} failures=0 validation=1'
    if expected not in log or 'Loaded layer VK_LAYER_KHRONOS_validation' not in log:
        raise ValueError('Missing successful result or validation-layer evidence')
    if re.search(r'Validation Error|VUID-|SYNC-HAZARD|Fatal signal|FATAL EXCEPTION', log):
        raise ValueError('Validation/runtime error; retained log, do not score')
    expected_files = {f'case-{case["id"]:03d}-{model}.rgba8':case['output'][0] * case['output'][1] * 4 for case in manifest['cases'] for model in models}
    archive_path = args.directory / 'outputs.tar'
    with archive_path.open('wb') as output:
        subprocess.run(adb + ['exec-out', 'run-as', 'com.thor.nativefg.spatialqa', 'tar', '-c', '-f', '-', 'files/results'], stdout=output, check=True)
    found = set()
    with tarfile.open(archive_path) as archive:
        for member in archive:
            if not member.isfile():continue
            name = pathlib.PurePosixPath(member.name).name
            if name not in expected_files or member.size != expected_files[name] or name in found:
                raise ValueError(f'Unexpected output member: {member.name}')
            (args.directory / name).write_bytes(archive.extractfile(member).read());found.add(name)
    if found != set(expected_files):raise ValueError('Missing GPU outputs')
    print(f'SPATIAL_COLLECTION pid={pid.group(1)} outputs={len(found)} fixtures_sha256={checksum.group(1)} validation_errors=0')


if __name__ == '__main__':main()
