#!/usr/bin/env python3
"""Build matching comparison or profiling-disabled binaries using parallel build jobs."""
import argparse, os, pathlib, subprocess, sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--mode',choices=['comparison','off'],default='comparison')
p.add_argument('-j', '--jobs', type=int, default=os.cpu_count() or 1)
a=p.parse_args()
if a.jobs < 1: p.error('--jobs must be positive')
if a.mode=='off' and not (ROOT/'profiling/dwl/dwl/dwl.c').is_file():
 p.error('configure a profiling build first to download the pinned wlroots/dwl sources')
directory=ROOT/('build-comparison' if a.mode=='comparison' else 'build-control')
from build_support import configure, run_build
options = dict(profiling=a.mode=='comparison', profiling_comparison=a.mode=='comparison',
               backends='all', renderers='all', buildtype='release')
options['runara:c_args'] = []
if a.mode=='comparison':
 options.update({'tracy:optimization':'2', 'tracy:default_library':'shared'})
print(f'Vortex ({a.mode}): checking/building with {a.jobs} jobs', flush=True)
configure(directory, ROOT, options)
run_build(['meson','compile','-C',str(directory),'-j',str(a.jobs)], f'Vortex ({a.mode})')
env=os.environ.copy();env['PROFILE_BUILD_JOBS']=str(a.jobs);env['TRACY_SOURCE']=str(ROOT/'subprojects/tracy');env['PROFILE_BUILD_TYPE']='release';env['PROFILE_ENABLED']='true' if a.mode=='comparison' else 'false';env['PROFILE_COMPARISON']='true' if a.mode=='comparison' else 'false'
subprocess.run([sys.executable,str(ROOT/'profiling/build_dwl.py')],env=env,check=True)
