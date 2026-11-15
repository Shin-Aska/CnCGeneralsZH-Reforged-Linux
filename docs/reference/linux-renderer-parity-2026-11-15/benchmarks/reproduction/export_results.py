#!/usr/bin/env python3
"""Export accepted benchmark data and exact reproduction tools; never runs the game."""
import argparse,csv,hashlib,json,shlex,shutil,statistics
from collections import defaultdict
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--runs',nargs='+',type=Path,required=True);p.add_argument('--qualification',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
qualification=json.loads(a.qualification.read_text());assert qualification.get('fair_particle_comparison_qualified'),'Particle population/visibility qualification must pass first'
candidate=qualification['candidate_sha256'];groups=defaultdict(list);runs=[]
for folder in a.runs:
 m=json.loads((folder/'manifest.json').read_text());assert m['accepted_timing_run'] and m['purpose']=='timed' and not m['resource_audit'];assert m['executable_sha256']==candidate and m['final_candidate_declared'];groups[(m['suite'],m['benchmark_variant'],m['sync'])].append((folder,m));runs.append((folder,m))
for suite,variants in [('bundle',['off','on']),('particles-fixed',['cpu','gpu'])]:
 for variant in variants:assert sorted(m['repeat'] for f,m in groups[(suite,variant,False)])==[1,2,3],f'Missing/duplicate primary repeats: {suite}/{variant}'
 for repeat in [1,2,3]:
  left=next(m for f,m in groups[(suite,variants[0],False)] if m['repeat']==repeat);right=next(m for f,m in groups[(suite,variants[1],False)] if m['repeat']==repeat);assert left['crc']==right['crc'] and left['scenario_sha256']==right['scenario_sha256']
for folder,m in runs:
 if m['suite']=='particles-fixed':
  cp={x['frame']:x['live'] for x in m['cpu_particles'] if 900<=x['frame']<=1499};gp={x['frame']:x['live'] for x in m['gpu_particles'] if 900<=x['frame']<=1499}
  assert len(cp)>=5 and all(c+gp.get(f,0)==5000 for f,c in cp.items()),'Timed populations differ from qualified5000'
  assert all(gp.get(f,0)>0 for f in cp) if m['benchmark_variant']=='gpu' else not gp
  assert all(x['evicted']==0 for x in m['gpu_particles'] if 900<=x['frame']<=1499)
a.out.mkdir(parents=True,exist_ok=False);(a.out/'runs').mkdir();repro=a.out/'reproduction';repro.mkdir();pooled=[];identities=[]
first_snapshot=runs[0][0]/'reproduction'
shutil.copy2(Path(__file__),repro/'export_results.py')
for name in ['run_benchmark.py','compare_runs.py']:shutil.copy2(first_snapshot/name,repro/name)
shutil.copytree(first_snapshot/'repro_tools',repro/'repro_tools');shutil.copytree(first_snapshot/'Data',repro/'Data')
for folder,m in runs:
 # Refuse a mixed runner/helper revision; accepted repeats must be reproducible by one exact tool set.
 assert hashlib.sha256((folder/'reproduction/run_benchmark.py').read_bytes()).hexdigest()==hashlib.sha256((repro/'run_benchmark.py').read_bytes()).hexdigest()
 assert m['helper_sha256']==runs[0][1]['helper_sha256']
 dest=a.out/'runs'/folder.name;dest.mkdir();shutil.copy2(folder/'manifest.json',dest/'manifest.json');shutil.copy2(folder/'timing.csv',dest/'timing.csv');shutil.copy2(folder/'summary.csv',dest/'summary.csv');shutil.copy2(folder/'user/Options.ini',dest/'Options.ini')
 for section in ['Scenarios','Cinema']:
  source=folder/'overlay'/section
  if source.exists() and not source.is_symlink():shutil.copytree(source,dest/section)
 if m.get('private_particle_fixture'):(dest/'private-particle-fixture.json').write_text(json.dumps(m['private_particle_fixture'],indent=2)+'\n')
 (dest/'environment.json').write_text(json.dumps(m['environment'],indent=2)+'\n')
 command=['python3','./reproduction/run_benchmark.py','--candidate','$GENERALS','--sha256',candidate,'--approved-final','--root','$GAME_ROOT','--overlay','$BASE_OVERLAY','--suite',m['suite'],'--variant',m['benchmark_variant'],'--repeat',str(m['repeat']),'--out','$RUN_OUT/'+folder.name,'--run']
 if m['suite']!='bundle':command+=['--fair-qualification','$QUALIFICATION','--native-cpu-cap',str(m['cpu_capacity'])]
 if m['sync']:command+=['--sync']
 shell=' '.join('"'+x+'"' if x.startswith('$') else shlex.quote(x) for x in command);(dest/'reproduce-command.sh').write_text('# Set GENERALS, GAME_ROOT, BASE_OVERLAY, RUN_OUT and QUALIFICATION explicitly.\n'+shell+'\n')
 identities.append({'run':folder.name,'candidate_sha256':candidate,'runner_sha256':m['runner_sha256'],'profile_sha256':m['profile_sha256'],'scenario_sha256':m['scenario_sha256'],'crc':m['crc'],'hardware_before':m['hardware_before'],'hardware_after':m['hardware_after'],'resource_counters':m['resource_counters']})
for (suite,variant,sync),values in sorted(groups.items()):
 columns=defaultdict(list);repeat_means=defaultdict(list)
 for folder,m in values:
  with (folder/'timing.csv').open() as stream:
   for row in csv.DictReader(stream):
    for key in ['frame_ms','work_ms','device_draw_ms','device_flush_ms','device_present_ms','swapchain_wait_ms','offscreen_wait_ms','fence_ms','draws','flushes']:columns[key].append(float(row[key]))
  for metric in m['metrics']:repeat_means[metric['metric']].append(metric['mean'])
 for metric,points in columns.items():
  points.sort();means=repeat_means[metric];pooled.append({'suite':suite,'variant':variant,'sync':sync,'metric':metric,'unit':'count' if metric in ['draws','flushes'] else 'ms','repeats':len(values),'samples':len(points),'mean':statistics.mean(points),'median':statistics.median(points),'p95':points[min(len(points)-1,int(len(points)*.95))],'minimum':points[0],'maximum':points[-1],'repeat_mean_min':min(means),'repeat_mean_max':max(means),'repeat_mean_stdev':statistics.stdev(means) if len(means)>1 else 0})
with (a.out/'results.csv').open('w') as stream:
 writer=csv.DictWriter(stream,fieldnames=list(pooled[0]));writer.writeheader();writer.writerows(pooled)
(a.out/'run-identities.json').write_text(json.dumps(identities,indent=2)+'\n');shutil.copy2(a.qualification,a.out/'particle-qualification.json')
for side in ['left','right']:
 source=Path(qualification[side]);dest=a.out/('qualification-'+side);dest.mkdir();shutil.copy2(source/'manifest.json',dest/'manifest.json')
 for picture in qualification['qualification_pictures'][side]:
  for kind in ['bmp','png']:
   original=Path(picture[kind]);assert hashlib.sha256(original.read_bytes()).hexdigest()==picture[kind+'_sha256'];shutil.copy2(original,dest/original.name)
README=f'''Accepted local native Linux renderer benchmarks. Candidate SHA256: `{candidate}`.

`results.csv` contains all primary repeats pooled by suite/variant, with mean/median/p95 and repeat-mean spread. The original per-run samples and summaries remain in `runs/`. These are native end-to-end offscreen present intervals at1920×1080/MSAA0, not hardware GPU timestamps or displayed FPS. SYNC rows, if present, are separately labelled serialized diagnostics. Every primary variant has exactly three repeats; no best-run selection was used.

The nonClassic option-bundle pair holds assets, water, shadows, model-state policy and GPU particles fixed. Only the documented controllable enhancement options/post chain differ. The fixed5000 particle pair compares CPUcap16384/ring0 against the explicitly documented lower CPU ceiling/native ring policy, because the unchanged manager sends only CPU overflow to the GPU. It passed exact stable retained-population/admission/drop/CRC checks and explicit post-window visibility inspection before timing. Qualification images and exact qualification JSON are included. Small-load GPU regressions, if measured, are part of the result; no unmeasured break-even or100k speed claim follows.

Exact runner/parser/helper snapshots and original authored camera/scenario inputs are in `reproduction/`. Each run includes its exact private profile, scenario/camera files, environment and complete portable launch command. For particles, the fixture metadata records the original installed template hash and exact field overrides; the runner reads the reader's own retail archive without changing it. Provide your matching original game root and final packaged overlay; retail art is not redistributed here. Python3/Pillow and the normal game SDL/Vulkan runtime are required, as on the test host.

Set `GENERALS`, `GAME_ROOT`, `BASE_OVERLAY`, `RUN_OUT` and `QUALIFICATION` explicitly before using a run's `reproduce-command.sh` from this directory. To regenerate qualification first, use the exact runner with `--suite particles-fixed --repeat 0 --run --approved-final`: run `--variant cpu` at its defaultCPUcap16384, then `--variant gpu --native-cpu-cap 1`. Supply the same candidate/root/base-overlay arguments shown in the per-run commands, and choose fresh output folders. Run the exact parser on those folders with `--particle-qualification`, inspect both post-window images, then rerun with `--visual-confirmed` only if visible. The qualification must use this same candidate/fixture/profile. Timed particle runs require its accepted `comparison.json`. Do not supply visual confirmation for an invisible workload.

No installation, clock/power tuning, publication, commits or original game-data writes were performed. Raw rejected/qualification runs remain in the local build evidence tree; they are excluded from the published primary distributions. See the upstream local tracker and final report for feature scope and remaining platform limitations.
'''
(a.out/'README.md').write_text(README);print(json.dumps({'exported':str(a.out),'runs':len(runs),'candidate':candidate,'rows':len(pooled)}))
