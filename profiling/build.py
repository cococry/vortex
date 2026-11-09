#!/usr/bin/env python3
"""Build matching comparison or profiling-disabled binaries, serially."""
import argparse, os, pathlib, subprocess, sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--mode',choices=['comparison','off'],default='comparison')
a=p.parse_args()
if a.mode=='off' and not (ROOT/'profiling/dwl/dwl/build-profiling.sh').is_file():
 p.error('configure a profiling build first to download the pinned wlroots/dwl sources')
directory=ROOT/('build-comparison' if a.mode=='comparison' else 'build-control')
command=['meson','setup']
if (directory/'build.ninja').exists():command+=['--reconfigure']
command += [str(directory),str(ROOT),'-Dprofiling='+('true' if a.mode=='comparison' else 'false'),'-Dprofiling_comparison='+('true' if a.mode=='comparison' else 'false'),'-Dbackends=all','-Drenderers=all','-Dbuildtype=release','-Drunara:c_args=[]']
if a.mode=='comparison':command+=['-Dtracy:optimization=2','-Dtracy:default_library=shared']
subprocess.run(command,check=True)
subprocess.run(['meson','compile','-C',str(directory),'-j','1'],check=True)
env=os.environ.copy();env['TRACY_SOURCE']=str(ROOT/'subprojects/tracy');env['PROFILE_BUILD_TYPE']='release';env['PROFILE_ENABLED']='true' if a.mode=='comparison' else 'false';env['PROFILE_COMPARISON']='true' if a.mode=='comparison' else 'false'
subprocess.run(['sh',str(ROOT/'profiling/dwl/dwl/build-profiling.sh')],env=env,check=True)

if a.mode=='comparison':
 subprocess.run([sys.executable,str(ROOT/'profiling/check_builds.py')],check=True)
