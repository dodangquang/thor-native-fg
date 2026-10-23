"""Local SGSR2-only research build; dependencies are pinned separately.

Requires prebuilt ARM64EC FreeType in deps/build/freetype-win.
No installation or game changes. Vulkan precompiled shaders require regeneration
after the motion-vector patch; use this build for D3D11-on-12 / D3D12 tests only.
"""
import concurrent.futures as futures
import datetime
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path('E:/ThorNative/research/sloptiscaler-arm64ec')
DEPS = Path('E:/ThorNative/research/sloptiscaler-deps')
TC = Path('E:/DevTools/llvm-mingw-20260908-ucrt-x86_64/bin')
OUT = DEPS / 'build/optiscaler-win'
OBJ = OUT / 'obj'
SHIM = OUT / 'caseshim'
CXX = TC / 'arm64ec-w64-mingw32-clang++.exe'

def run(args, log, cwd=ROOT):
    result = subprocess.run([str(a) for a in args], cwd=cwd,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    Path(log).write_bytes(result.stdout)
    if result.returncode:
        raise RuntimeError(f'Failed: {args[0]}; see {log}\n' + result.stdout.decode(errors='replace')[-6000:])
    return result.stdout.decode(errors='replace')

def main():
    for folder in (OUT, OBJ, SHIM):
        folder.mkdir(parents=True, exist_ok=True)
    assert (DEPS / 'build/freetype-win/libfreetype.a').is_file()
    (SHIM / 'xess_dbg.h').write_text('#include "' + (ROOT / 'external/xess/inc/xess/xess_debug.h').as_posix() + '"\n')
    commit = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
    (SHIM / 'resource_build_date.h').write_text(f'#pragma once\n#define VER_BUILD_DATE "{datetime.date.today()}"\n')
    (SHIM / 'resource_build_commit.h').write_text(f'#pragma once\n#define VER_BUILD_COMMIT "{commit[:8]}-thor-mvfix"\n')
    detour_objects = []
    for name in ('detours', 'modules', 'disasm', 'image', 'creatwth', 'disolarm64'):
        obj = OUT / f'detours_{name}.o'
        run([CXX, '-c', f'{name}.cpp', '-o', obj, '-I.', '-O2', '-DWIN32_LEAN_AND_MEAN', '-Wno-everything'],
            OUT / f'detours_{name}.log', DEPS / 'Detours/src')
        detour_objects.append(obj)
    run([TC / 'llvm-ar.exe', 'rcs', OUT / 'libdetours.a', *detour_objects], OUT / 'detours-archive.log')
    definition = OUT / 'd3d12_extra.def'
    definition.write_text('LIBRARY d3d12.dll\nEXPORTS\nD3D12GetInterface\n')
    run([TC / 'arm64ec-w64-mingw32-dlltool.exe', '-d', definition, '-l', OUT / 'libd3d12_extra.a', '-m', 'arm64ec'], OUT / 'd3d12-import.log')
    includes = [SHIM, DEPS / 'DirectXMath/Inc', ROOT / 'compat/arm64ec', ROOT / 'OptiScaler/include/imgui']
    includes += [ROOT / line.strip() for line in (ROOT / 'arm64ec/include-flags.txt.paths').read_text().splitlines() if line.strip()]
    flags = ['-std=c++23', '-O2', *['-I' + p.as_posix() for p in includes], '-include',
             (ROOT / 'compat/arm64ec/opti_sal_compat.h').as_posix(), '-DWIN32', '-DNDEBUG', '-D_WINDOWS',
             '-D_USRDLL', '-DIMGUI_DISABLE_SSE', '-DUNICODE', '-D_UNICODE', '-Wno-c++11-narrowing']
    sources = sorted(p for p in (ROOT / 'OptiScaler').rglob('*.cpp')
                     if 'library' not in p.relative_to(ROOT).parts and p.name != 'imgui_impl_uwp.cpp')
    sources += sorted((ROOT / 'compat/arm64ec').rglob('*.cpp'))
    objects = [OBJ / (p.relative_to(ROOT).as_posix().replace('/', '_')[:-4] + '.o') for p in sources]
    (OUT / 'sources.json').write_text(json.dumps([p.as_posix() for p in sources], indent=2))
    def compile_one(pair):
        source, obj = pair
        if '--clean' not in sys.argv[1:] and obj.is_file() and obj.stat().st_mtime >= source.stat().st_mtime:
            txt = source.read_text(errors='replace')
            if not any(h in txt for h in ('dx11_with_dx12', 'SGSR2', 'IFeature_Dx11wDx12')) or obj.stat().st_mtime >= newest_hdr:
                return source.relative_to(ROOT).as_posix() + ' (cached)'
        run([CXX, *flags, '-c', source, '-o', obj], obj.with_suffix('.log'))
        return source.relative_to(ROOT).as_posix()
    newest_hdr = max(p.stat().st_mtime for p in (ROOT / 'OptiScaler').rglob('*.h'))
    failures = []
    with futures.ThreadPoolExecutor(max_workers=4) as pool:
        pending = [pool.submit(compile_one, pair) for pair in zip(sources, objects)]
        for i, future in enumerate(futures.as_completed(pending), 1):
            try:
                name = future.result()
                print(f'[{i}/{len(sources)}] {name}', flush=True)
            except Exception as exc:
                failures.append(str(exc))
                print(str(exc), flush=True)
    if failures:
        raise RuntimeError(f'{len(failures)} compilation failures; see object logs')
    rc = OUT / 'OptiScaler_utf8.rc'
    rc.write_text((ROOT / 'OptiScaler/OptiScaler.rc').read_text(encoding='utf-16'), encoding='utf-8')
    resource = OUT / 'zz_resource.res'
    run([TC / 'arm64ec-w64-mingw32-windres.exe', '-I', ROOT / 'OptiScaler', '-I', SHIM,
         '-DOPTISCALER_BUILD_METADATA', rc, '-O', 'coff', '-o', resource], OUT / 'resource.log')
    symbols = set()
    vk_symbols = set()
    for i, obj in enumerate(objects):
        raw = run([TC / 'llvm-nm.exe', obj], OUT / f'nm-{i}.log')
        for line in raw.splitlines():
            parts = line.split()
            if len(parts) >= 2:
                kind, name = parts[-2], parts[-1].removeprefix('#')
                if kind == 'T':
                    symbols.add(name)
                if kind in ('U', 'w') and re.fullmatch(r'vk[A-Za-z0-9]+', name):
                    vk_symbols.add(name)
    syms_file = OUT / 'all_syms.txt'
    syms_file.write_text('\n'.join(sorted(symbols)) + '\n')
    vk_def = OUT / 'vulkan-1.def'
    vk_def.write_text('LIBRARY vulkan-1.dll\nEXPORTS\n' + '\n'.join(sorted(vk_symbols)) + '\n')
    run([TC / 'arm64ec-w64-mingw32-dlltool.exe', '-d', vk_def, '-l', OUT / 'libvulkan-1.a', '-m', 'arm64ec'], OUT / 'vulkan-import.log')
    exports = OUT / 'Source_arm64ec.def'
    mapped = run([sys.executable, ROOT / 'arm64ec/gen_def.py', ROOT / 'OptiScaler/Source.def', syms_file, exports], OUT / 'exports.log')
    print(mapped, flush=True)
    if not re.search(r', 0 unresolved', mapped):
        raise RuntimeError('Unresolved exports; refusing incomplete DLL')
    dll = OUT / 'OptiScaler.dll'
    args = ['-shared', '-o', dll, *objects, resource, exports, '-L' + OUT.as_posix(),
            '-L' + (DEPS / 'build/freetype-win').as_posix(), '-ldetours', '-lfreetype', '-lvulkan-1', '-ld3d12_extra',
            *['-l' + lib for lib in ('d3d11', 'd3d12', 'dxgi', 'dxguid', 'd3dcompiler', 'winhttp', 'dbghelp', 'dwmapi',
                                     'gdi32', 'imm32', 'shell32', 'user32', 'version', 'ole32', 'oleaut32', 'uuid')],
            '-static-libgcc', '-static-libstdc++', '-Wl,--error-limit=0']
    response = OUT / 'link.rsp'
    response.write_text('\n'.join('"' + str(arg).replace('\\', '/') + '"' for arg in args))
    run([CXX, '@' + response.as_posix()], OUT / 'link.log')
    headers = run([TC / 'llvm-readobj.exe', '--file-headers', '--coff-imports', dll], OUT / 'dll-inspection.txt')
    if 'IMAGE_FILE_MACHINE_ARM64EC' not in headers:
        raise RuntimeError('Unexpected DLL architecture')
    print(f'Built {dll}; SHA256={hashlib.sha256(dll.read_bytes()).hexdigest()}', flush=True)

if __name__ == '__main__':
    main()
