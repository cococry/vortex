#!/usr/bin/env python3
"""Audit the default comparison builds before acquiring measurements."""
import json, pathlib, shlex, subprocess, hashlib
ROOT=pathlib.Path(__file__).resolve().parents[1]
builds=[(ROOT/'build-comparison','VT'),(ROOT/'profiling/dwl/build-comparison','WLR')]
clients=[]
for build,prefix in builds:
 rows=json.loads((build/'compile_commands.json').read_text())
 client=next(r for r in rows if r['file'].endswith('TracyClient.cpp'))
 words=shlex.split(client['command'])
 for required in ['-O2','-fPIC','-pthread','-std=c++17','-DTRACY_ENABLE']:
  if required not in words:raise RuntimeError(f'{build}: Tracy missing {required}')
 if any(w.startswith('-O') and w!='-O2' for w in words):raise RuntimeError(f'{build}: conflicting optimization')
 if '.so.p' not in client['command']:raise RuntimeError(f'{build}: Tracy must be shared')
 for row in rows:
  if row['file'].endswith('.c') and not row['file'].lstrip('./').startswith('subprojects/'):
   if '-D'+prefix+'_PROFILE_COMPARISON' not in shlex.split(row['command']):
    raise RuntimeError(f'{build}: C source missing comparison flag: {row["file"]}')
 source=(pathlib.Path(client['directory'])/client['file']).resolve().parent
 digest=hashlib.sha256()
 for file in sorted(source.rglob('*')):
  if file.is_file() and file.suffix in ['.h','.hpp','.c','.cpp']:
   digest.update(str(file.relative_to(source)).encode());digest.update(file.read_bytes())
 clients.append(digest.hexdigest())
 print(f'{build.name}: shared Tracy -O2, C comparison flags, source {clients[-1][:16]} OK')
if len(set(clients))!=1:raise RuntimeError('Tracy public source trees differ')
for binary in [ROOT/'build-comparison/vortex',ROOT/'profiling/dwl/dwl/build-comparison/dwl']:
 deps=subprocess.check_output(['ldd',str(binary)],text=True)
 if 'tracy' not in deps.lower():raise RuntimeError(f'{binary}: shared Tracy dependency missing')
print('comparison build audit passed')
