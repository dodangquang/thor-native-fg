"""Replace only the interpolation array after compiling/validating its SPIR-V."""
from pathlib import Path
import re
import struct
import subprocess
root=Path(__file__).resolve().parents[2]
bin=Path('E:/DevTools/Android/Sdk/ndk/27.3.13750724/shader-tools/windows-x86_64')
spv=root/'build/interpolate.spv'
subprocess.run([str(bin/'glslc.exe'),'--target-env=vulkan1.1','-I',str(root/'shaders'),'-O',str(root/'shaders/interpolate.comp'),'-o',str(spv)],check=True)
subprocess.run([str(bin/'spirv-val.exe'),'--target-env','vulkan1.1',str(spv)],check=True)
raw=spv.read_bytes(); words=struct.unpack('<'+'I'*(len(raw)//4),raw)
header=root/'build/generated/shaders.hpp'
text,count=re.subn(r'static const uint32_t interpolate_spv\[\]=\{[^}]+\};',
                  'static const uint32_t interpolate_spv[]={'+','.join(f'0x{n:08x}u' for n in words)+'};',header.read_text())
assert count==1, f'Expected exactly one array, got {count}'
header.write_text(text)
print('Interpolation SPIR-V compiled, validated and header updated')
