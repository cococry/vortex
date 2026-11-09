#!/usr/bin/env python3
"""Check that comparison mode emits only stage probes; detailed/off remain intact."""
import pathlib, re, subprocess, tempfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
headers=[('src/core/profiling.h','VT'),('profiling/dwl/include/util/profiling.h','WLR'),('profiling/dwl/dwl/profiling.h','WLR')]
with tempfile.TemporaryDirectory() as directory:
 d=pathlib.Path(directory)
 for header,prefix in headers:
  source=d/'probe.c';assembly=d/'probe.s'
  source.write_text(f'''#include "{ROOT/header}"
void probe(void) {{
 {prefix}_PROFILE_SCOPE();
 {prefix}_PROFILE_SCOPE_NAMED("detail");
 {prefix}_PROFILE_COMPARE("compare.frame");
 {prefix}_PROFILE_BEGIN(explicit_zone);
 {prefix}_PROFILE_END(explicit_zone);
}}
''')
  for mode,flags,expected in [('off',[],0),('detailed',['-DTRACY_ENABLE'],3),('comparison',['-DTRACY_ENABLE',f'-D{prefix}_PROFILE_COMPARISON'],1)]:
   subprocess.run(['cc','-std=c11','-D_POSIX_C_SOURCE=200809L','-O2','-S','-I'+str(ROOT/'subprojects/tracy/public'),*flags,str(source),'-o',str(assembly)],check=True)
   text=assembly.read_text()
   calls=len(re.findall(r'\b(?:call|jmp)\s+___tracy_emit_zone_begin(?:_callstack)?(?:@PLT)?\b',text))
   if calls!=expected:raise RuntimeError(f'{header}/{mode}: {calls} zone calls, expected {expected}')
   timestamps=len(re.findall(r'\b(?:call|jmp)\s+___tracy_emit_zone_value(?:@PLT)?\b',text))
   if timestamps!=(1 if mode=='comparison' else 0):raise RuntimeError(f'{header}/{mode}: incorrect coarse timestamp probes')
   print(f'{header}: {mode}: {calls} zone calls OK')

# Compile the actual Runara Vulkan implementation, including named driver-call
# zones. Coarse/release must erase all its detailed probes.
with tempfile.TemporaryDirectory() as directory:
 assembly=pathlib.Path(directory)/'runara-vulkan.s'
 for mode,flags in [('off',[]),('detailed',['-DTRACY_ENABLE']),('comparison',['-DTRACY_ENABLE','-DVT_PROFILE_COMPARISON'])]:
  subprocess.run(['cc','-std=c11','-D_POSIX_C_SOURCE=200809L','-O2','-S',
   '-I'+str(ROOT/'subprojects/runara'),
   '-I'+str(ROOT/'subprojects/runara/include'),
   '-I'+str(ROOT/'subprojects/runara/vendor/vma/include'),
   '-I'+str(ROOT/'subprojects/tracy/public'),
   '-include',str(ROOT/'src/core/profiling.h'),
   '-DRN_PROFILE_SCOPE=VT_PROFILE_SCOPE',
   '-DRN_PROFILE_SCOPE_NAMED=VT_PROFILE_SCOPE_NAMED',
   *flags,str(ROOT/'subprojects/runara/src/vulkan/backend.c'),'-o',str(assembly)],check=True)
  text=assembly.read_text()
  calls=len(re.findall(r'\b(?:call|jmp)\s+___tracy_emit_zone_begin(?:_callstack)?(?:@PLT)?\b',text))
  if (calls > 0) != (mode=='detailed'):
   raise RuntimeError(f'Runara Vulkan/{mode}: incorrect detailed probes ({calls})')
  if mode=='detailed' and 'Vulkan: present' not in text:
   raise RuntimeError('Runara Vulkan: named presentation scope missing')
  print(f'Runara Vulkan: {mode}: {calls} zone calls OK')
