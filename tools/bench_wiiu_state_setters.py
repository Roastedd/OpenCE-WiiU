#!/usr/bin/env python3
"""Run the asset-free PowerPC replay with a baseline source snapshot.

Counts use separately instrumented builds; elapsed times use uninstrumented
executables in alternating order. No console or game assets are accessed.
"""
import argparse,importlib.util,json,statistics,time,os,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description='Measure exact state-tracking work and translated replay elapsed time. This does not measure Wii U frame time.')
p.add_argument('--baseline-source',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--pairs',type=int,default=9)
p.add_argument('--frames',type=int,default=2470)
a=p.parse_args()
if not 1<=a.pairs<=100 or not 1<=a.frames<=100000:p.error('invalid pair/frame bounds')
if not os.environ.get('D3D8_REPLAY_TARGET') or not os.environ.get('D3D8_REPLAY_RUNNER'):p.error('set the PowerPC replay target and runner as documented in test_wiiu_d3d8_replay.py')
OUT=a.output.resolve();OUT.mkdir(parents=True,exist_ok=False)
spec=importlib.util.spec_from_file_location('replay_test',ROOT/'tools/test_wiiu_d3d8_replay.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
relative='port/wiiu/src/d3d8_gx2.c';current=(ROOT/relative).read_text(encoding='latin-1');old=a.baseline_source.read_text(encoding='latin-1')
wrapper=OUT/'measure_main.c';wrapper.write_text('#define main original_replay_main\n#include "'+str(ROOT/'port/wiiu/tests/d3d8_replay/replay_main.c')+'"\n#undef main\nextern unsigned long replay_render_marks,replay_texture_marks,replay_state_visits;\nint main(int argc,char **argv){int r=original_replay_main(argc,argv);printf("render-marks %lu texture-marks %lu state-visits %lu\\n",replay_render_marks,replay_texture_marks,replay_state_visits);return r;}\n')
original_units=m.REPLAY_UNITS[:]
def instrument(s):
 s='unsigned long replay_render_marks,replay_texture_marks,replay_state_visits;\n'+s
 for before,after in [
  ('render_states_dirty[state / 32] |= 1u << (state % 32);','{ replay_render_marks++; render_states_dirty[state / 32] |= 1u << (state % 32); }'),
  ('texture_states_dirty[stage] |= 1u << type;','{ replay_texture_marks++; texture_states_dirty[stage] |= 1u << type; }'),
  ('index = word * 32 + (unsigned long)__builtin_ctz(marked);','index = word * 32 + (unsigned long)__builtin_ctz(marked); replay_state_visits++;'),
  ('index = (unsigned long)__builtin_ctz(marked);','index = (unsigned long)__builtin_ctz(marked); replay_state_visits++;')]:
  assert s.count(before)==1,(before,s.count(before));s=s.replace(before,after)
 return s
builds={}
for name,source in [('baseline',old),('candidate',current)]:
 for measured in [False,True]:
  key=name+('-counts' if measured else '')
  folder=OUT/key;folder.mkdir(exist_ok=True);(folder/'sources').mkdir(exist_ok=True)
  m.REPLAY_UNITS=original_units[:-1]+[str(wrapper)] if measured else original_units
  builds[key]=m.build(folder,[(relative,current,instrument(source) if measured else source)])
  print('built',key,flush=True)
results={'baseline_source_sha256':hashlib.sha256(old.encode('latin-1')).hexdigest(),'candidate_source_sha256':hashlib.sha256(current.encode('latin-1')).hexdigest(),'frames':a.frames,'pairs':a.pairs,'counts':{},'timings':[],'limits':'QEMU host replay; not console GPU/CPU time or b30 frame time.'}
for name in ['baseline','candidate']:
 results['counts'][name]=m.replay(builds[name+'-counts'],'sync','lag',1,frames=a.frames)
for i in range(a.pairs):
 for name in (['baseline','candidate'] if i%2==0 else ['candidate','baseline']):
  start=time.perf_counter();v=m.replay(builds[name],'sync','lag',1,frames=a.frames);dt=time.perf_counter()-start
  results['timings'].append({'pair':i,'name':name,'seconds':dt,'digest':v['digest'],'locks':v['locks']});print(i,name,dt,flush=True)
results['medians']={name:statistics.median(r['seconds'] for r in results['timings'] if r['name']==name) for name in ['baseline','candidate']}
assert results['counts']['baseline']['digest']==results['counts']['candidate']['digest']
if a.frames==m.FRAMES:assert results['counts']['candidate']['digest']==m.DIGEST
assert results['counts']['baseline']['locks']==results['counts']['candidate']['locks']
if a.frames==m.FRAMES:assert results['counts']['candidate']['locks']==m.LOCKS['sync']
(OUT/'replay-measurements.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(results['counts'],indent=2));print(results['medians'])
