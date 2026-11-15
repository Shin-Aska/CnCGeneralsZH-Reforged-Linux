#!/usr/bin/env python3
"""Private benchmark preparation/execution. No game source or original data writes."""
import argparse,csv,hashlib,importlib.util,json,math,os,platform,re,shutil,statistics,subprocess,sys,time
from pathlib import Path
SCRIPT=Path(__file__).resolve();ROOT=next((p for p in SCRIPT.parents if (p/'GeneralsMD/Code').is_dir()),SCRIPT.parent);TOOLS=SCRIPT.parent/'repro_tools' if (SCRIPT.parent/'repro_tools').is_dir() else ROOT/'GeneralsMD/Code/Tools';sys.path.insert(0,str(TOOLS))
spec=importlib.util.spec_from_file_location('native_fixture',TOOLS/'native-particle-reach-check.py');fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
FEATURE_ROWS=['Firelight','HemiAmbient','AmbientOcclusion','Weather','Atmosphere','MapLight','HeatHaze','TeamGlow','WaterFoam','GroundMarks','WaterSplashes','StructureCollapse']
def write(path,value):path.write_text(json.dumps(value,indent=2)+'\n')
def private_child(folder,name):
 path=folder/name
 if path.is_symlink():
  original=path.resolve();path.unlink();path.mkdir()
  for entry in original.iterdir():(path/entry.name).symlink_to(entry.resolve(),target_is_directory=entry.is_dir())
 else:path.mkdir(exist_ok=True)
 return path
def stock_burst(root):
 spec=importlib.util.spec_from_file_location('bigfile',TOOLS/'bigfile.py');big=importlib.util.module_from_spec(spec);spec.loader.exec_module(big)
 for archive in ['PatchINI.big','INIZH.big']:
  with (root/archive).open('rb') as stream:
   for name,offset,size in list(big._read_index(stream)):
    if name.lower().replace('\\','/')!='data/ini/particlesystem.ini':continue
    stream.seek(offset);text=stream.read(size).decode('latin1');match=re.search(r'^ParticleSystem CINE_JetLenzflareExhaust\s*\n.*?^End\s*$',text,re.M|re.S)
    if not match:continue
    original=match.group(0).replace('\r\n','\n');altered=original.replace('ParticleSystem CINE_JetLenzflareExhaust','ParticleSystem BenchmarkStableBurst')
    fields={'SystemLifetime':'0','Lifetime':'100000.00 100000.00','BurstDelay':'99999.00 99999.00','BurstCount':'250.00 250.00','Size':'5.00 5.00','SizeRate':'0.00 0.00','SizeRateDamping':'1.00 1.00','Alpha1':'0.40 0.40 0','Alpha2':'0.00 0.00 0','Alpha3':'0.00 0.00 0'}
    for key,value in fields.items():altered,n=re.subn(r'(?m)^\s*'+key+r'\s*=.*$',f'  {key} = {value}',altered);assert n==1
    return original,altered,fields,archive
 raise RuntimeError('Original cinematic particle template missing')
def hardware():
 result={'platform':platform.platform(),'cpu_models':sorted(set(re.findall(r'^model name\s*:\s*(.+)$',Path('/proc/cpuinfo').read_text(),re.M)))}
 if shutil.which('nvidia-smi'):
  command=['nvidia-smi','--query-gpu=name,uuid,driver_version,memory.total,memory.used,temperature.gpu,pstate,power.draw,clocks.current.graphics,clocks.current.memory','--format=csv,noheader']
  run=subprocess.run(command,text=True,capture_output=True);result['nvidia_smi']={'command':command,'returncode':run.returncode,'stdout':run.stdout,'stderr':run.stderr}
 return result
def summarize(path,first,last):
 with path.open() as stream:rows=list(csv.DictReader(stream))
 frames=[int(row['logic_frame']) for row in rows];expected=list(range(first,last+1))
 if frames!=expected:raise RuntimeError(f'Timing window incomplete/nonconsecutive: {len(frames)} samples; expected {len(expected)}')
 summaries=[]
 for metric in ['frame_ms','work_ms','device_draw_ms','device_flush_ms','device_present_ms','swapchain_wait_ms','offscreen_wait_ms','fence_ms','draws','flushes']:
  values=sorted(float(row[metric]) for row in rows)
  if not all(math.isfinite(v) and v>=0 for v in values):raise RuntimeError('Invalid timing values: '+metric)
  summaries.append({'metric':metric,'samples':len(values),'mean':statistics.mean(values),'median':statistics.median(values),'p95':values[min(len(values)-1,int(len(values)*.95))],'minimum':values[0],'maximum':values[-1]})
 return summaries
p=argparse.ArgumentParser();p.add_argument('--candidate',type=Path,required=True);p.add_argument('--sha256',required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--suite',choices=['bundle','particles-fixed','particles-continuous','particles100k'],required=True);p.add_argument('--variant',choices=['off','on','cpu','gpu'],required=True);p.add_argument('--repeat',type=int,default=1);p.add_argument('--run',action='store_true');p.add_argument('--approved-final',action='store_true');p.add_argument('--sync',action='store_true');p.add_argument('--resource-audit',action='store_true');p.add_argument('--fair-qualification',type=Path);p.add_argument('--native-cpu-cap',type=int,default=16384);p.add_argument('--root',type=Path,default=ROOT/'build-linux/campaign-jitter-evidence/default-single-player/replay-tmp/replay-check.yTdOAL/root');p.add_argument('--overlay',type=Path,default=ROOT/'build-linux/battle-marks-evidence/overlay');a=p.parse_args()
for name in ['candidate','out','root','overlay']:setattr(a,name,getattr(a,name).resolve())
if fixture.digest(a.candidate)!=a.sha256:raise SystemExit('Immutable candidate hash mismatch')
if a.run and a.repeat>0 and not a.approved_final:raise SystemExit('Timed execution requires explicit final-candidate approval; repeat0 qualification is separate')
if a.suite=='bundle' and a.variant not in ['off','on']:raise SystemExit('Bundle variants are off/on')
if a.suite!='bundle' and a.variant not in ['cpu','gpu']:raise SystemExit('Particle variants are cpu/gpu')
first,last,limit=(2700,3299,3400) if a.suite=='bundle' else (900,1499,1600)
fixture.CASES['benchmark']=('looks4','Maps/Twilight Flame/Twilight Flame.map',(760,3668),limit,last) if a.suite=='bundle' else ('particles100k','Maps/Alpine Assault/Alpine Assault.map',(760,850),limit,last)
capacity=150000 if a.suite=='particles100k' else a.native_cpu_cap if a.suite!='bundle' and a.variant=='gpu' else 16384;gpu=(262144 if a.suite=='particles100k' else 8192) if a.variant=='gpu' else 0
args=argparse.Namespace(root=a.root,overlay=a.overlay,generals=a.candidate,out=a.out,case='benchmark',variant='on',run=a.run,debug=False,chain='bloom,fxaa' if a.suite=='bundle' and a.variant=='on' else 'off',gpu_capacity=gpu,cpu_capacity=capacity,closed_form_check=False,require_gpu_work=False,require_reach_work=False,passive_trace=False,limit=limit,shot=last,timeout=900)
m,user,executable=fixture.prepare(args);cmd=m['command'];at=cmd.index('-screenshot');del cmd[at:at+2]
cmd[cmd.index('-xres')+1]='1920';cmd[cmd.index('-yres')+1]='1080';cmd.append('-noshroud')
if a.suite!='bundle' and a.repeat==0:
 cmd.extend(['-screenshot',str(last+51)]);m['qualification_picture_frame']=last+51;m['qualification_picture_scope']='Untimed admission/visibility evidence after the sample window; excluded from published timing aggregates.'
if a.suite=='bundle':
 cmd.remove('-observer');at=cmd.index('-side');cmd[at+2]='FactionAmericaSuperWeaponGeneral';at=cmd.index('-side',at+1);cmd[at+2]='FactionGLA';cmd[cmd.index('-seed')+1]='7'
 folder=private_child(a.out/'overlay','Cinema');source=TOOLS.parent/'Data/Cinema/looks4.txt';(folder/source.name).unlink(missing_ok=True);text='\n'.join(line for line in source.read_text().splitlines() if not re.match(r'^\s*[\d.]+\s+(shot|end)\b',line))+'\n';(folder/source.name).write_text(text);cmd.extend(['-cinema','looks4']);m['cinema_original_sha256']=fixture.digest(source);m['cinema_private_sha256']=fixture.digest(folder/source.name)
else:
 scenario=a.out/'overlay/Scenarios/particles100k.txt'
 if a.suite=='particles-continuous':scenario.write_text('30 particles 0 CINE_JetLenzflareExhaust 20 760 850 25\n')
 if a.suite=='particles-fixed':
  original,block,fields,archive=stock_burst(a.root);data=private_child(a.out/'overlay','Data');ini=private_child(data,'INI');path=ini/'ParticleSystemReforged.ini';prior=path.read_text();path.unlink();path.write_text(prior+'\n; Private fair-workload fixture, original assets unchanged.\n'+block+'\n');scenario.write_text('30 particles 0 BenchmarkStableBurst 20 760 850 25\n');m['private_particle_fixture']={'source_archive':archive,'original_block_sha256':hashlib.sha256(original.encode()).hexdigest(),'overrides':fields,'particle_ini_sha256':fixture.digest(path),'target_emitters':20,'target_particles':5000,'qualification':'Prepared only; parser/admission/population equality must be measured before fair timing claim.'}
 m['scenario_sha256']=fixture.digest(scenario)
# Both primary profiles remain nonClassic with identical geometry/assets/shadows/model policy.
options='Resolution = 1920 1080\nClassicGraphics = no\nDetailedModels = yes\nHDTextures = yes\nWaterStyle = 0\nCloudStyle = 0\nLightingStyle = 0\nShadowQuality = 0\nUseShadowVolumes = yes\nUseShadowDecals = yes\nStaticGameLOD = Custom\nIdealStaticGameLOD = High\nDynamicLOD = no\nTextureReduction = 0\nSmoothMotion = no\nVSync = no\nMSAA = 0\nMaxParticleCount = '+str(capacity)+'\n'
on=a.suite=='bundle' and a.variant=='on'
options+=''.join(f'{row} = '+('yes' if on else 'no')+'\n' for row in FEATURE_ROWS)+f'InternalHdr = {"yes" if on else "no"}\nBloom = {4 if on else 0}\n'
(user/'Options.ini').write_text(options);m['profile_sha256']=fixture.digest(user/'Options.ini')
env=m['environment'];env.update(ZH_GPU_TIMING='0,1',ZH_GPU_TIMING_LOGIC=f'{first},{last}',ZH_GPU_TIMING_CSV=str(a.out/'timing.csv'),ZH_GPU_TIMING_SYNC='1' if a.sync else '0')
if a.resource_audit:env['ZH_GPU_CREATION_LOG']='1'
m.update(suite=a.suite,benchmark_variant=a.variant,repeat=a.repeat,width=1920,height=1080,msaa=0,sync=a.sync,resource_audit=a.resource_audit,final_candidate_declared=a.approved_final,timing={'first':first,'last':last,'expected_samples':last-first+1,'warmup_logic_frames':first-1,'basis':'Existing native end-to-end offscreen present intervals; not hardware GPU timestamps','sync_note':'Serialized whole-engine fence diagnostic; never frame-rate/FPS claim' if a.sync else 'Unsynchronized primary throughput timing'},command=cmd,scenario_sha256=fixture.digest(a.out/'overlay/Scenarios'/('looks4.txt' if a.suite=='bundle' else 'particles100k.txt')))
checkpoint=a.candidate.parent/'checkpoint.json'
if checkpoint.exists():m['candidate_checkpoint']=json.loads(checkpoint.read_text())
m['purpose']='resource-audit' if a.resource_audit else 'qualification' if a.repeat==0 else 'timed'
reproduction=a.out/'reproduction';reproduction.mkdir();shutil.copy2(SCRIPT,reproduction/'run_benchmark.py');shutil.copy2(SCRIPT.parent/'compare_runs.py',reproduction/'compare_runs.py');m['runner_sha256']=fixture.digest(reproduction/'run_benchmark.py');m['parser_snapshot_sha256']=fixture.digest(reproduction/'compare_runs.py')
helpers=reproduction/'repro_tools';helpers.mkdir();m['helper_sha256']={}
for name in ['native-particle-reach-check.py','portable-cosmetics-check.py','control_client.py','bigfile.py']:
 shutil.copy2(TOOLS/name,helpers/name);m['helper_sha256'][name]=fixture.digest(helpers/name)
for sub,names in [('Scenarios',['looks4.txt','particles100k.txt']),('Cinema',['looks4.txt'])]:
 folder=reproduction/'Data'/sub;folder.mkdir(parents=True,exist_ok=True)
 for name in names:shutil.copy2(TOOLS.parent/'Data'/sub/name,folder/name)
if a.run and a.suite!='bundle' and a.repeat>0 and not a.resource_audit:
 if not a.fair_qualification:raise RuntimeError('Timed particle pair requires an accepted exact-population qualification artifact')
 qualification=json.loads(a.fair_qualification.read_text())
 if not qualification.get('fair_particle_comparison_qualified') or qualification['candidate_sha256']!=a.sha256:raise RuntimeError('Particle qualification failed or belongs to another candidate')
 qualified_manifests=[json.loads((Path(qualification[key])/'manifest.json').read_text()) for key in ['left','right']]
 matching=[q for q in qualified_manifests if q['benchmark_variant']==a.variant]
 if len(matching)!=1 or not all(q['suite']==a.suite and q['scenario_sha256']==m['scenario_sha256'] and q['profile_sha256']==m['profile_sha256'] and q['cpu_capacity']==m['cpu_capacity'] and q['gpu_capacity']==m['gpu_capacity'] for q in matching):raise RuntimeError('Particle fixture/profile/admission policy differs from its qualified variant')
 m['fair_qualification_sha256']=fixture.digest(a.fair_qualification)
write(a.out/'manifest.json',m)
if not a.run:print(json.dumps({'prepared':str(a.out),'candidate':a.sha256,'samples':last-first+1}));raise SystemExit(0)
m['hardware_before']=hardware();execution_env=os.environ.copy()
for key in ['ZH_GPU_DEBUG','VK_INSTANCE_LAYERS','ZH_GPU_CREATION_LOG','ZH_GPU_TIMING_COLD','ZH_GPU_TIMING_COLD_MARKERS']:execution_env.pop(key,None)
execution_env.update(env);started=time.monotonic()
with (a.out/'run.log').open('w') as stream:
 run=subprocess.run(cmd,cwd=a.out,env=execution_env,stdout=stream,stderr=subprocess.STDOUT,timeout=900)
m['returncode']=run.returncode;m['wall_seconds_including_load']=round(time.monotonic()-started,3);m['hardware_after']=hardware();text=(a.out/'run.log').read_text(errors='replace')
for path in [user/'Logs/nativecheck_DebugLogFile.txt',executable.parent/'nativecheck_DebugLogFile.txt']:
 if path.exists():(a.out/'engine.log').write_bytes(path.read_bytes());text+='\n'+path.read_text(errors='replace');break
m['crc']=re.findall(r'HEADLESS CRC AT LIMIT:.*',text);m['validation_errors']=[line for line in text.splitlines() if 'VUID-' in line or 'Validation Error' in line];m['timer_status']=re.findall(r'PosixDevice9 timing: LOGIC[^\n]+',text)
m['cpu_particles']=[dict(frame=int(f),live=int(n),systems=int(s)) for f,n,s in re.findall(r'PARTICLES frame (\d+): (\d+) particles in (\d+) systems',text)]
m['gpu_particles']=[dict(frame=int(f),live=int(n),spawned=int(s),ring=int(r),evicted=int(e),systems=int(y)) for f,n,s,r,e,y in re.findall(r'GPUPARTICLES frame (\d+): live (\d+) spawned (\d+) ring (\d+) evicted (\d+) systems (\d+)',text)]
m['resource_counters']=[line for line in text.splitlines() if line.startswith(('GPUPARTICLES SDL:','PosixDevice9: ')) and ('recorded' in line or 'flushes' in line or 'buffer-bytes' in line)];m['particle_cost']=[line for line in text.splitlines() if 'HEADLESS PARTICLECOST:' in line];m['program_counts']=[{'program':name,'draws':int(count)} for name,count in re.findall(r'PosixDevice9:\s+drawn with (.+): (\d+)',text)]
m['qualification_pictures']=[]
if a.suite!='bundle' and a.repeat==0:
 from PIL import Image
 pictures=sorted(user.glob('sshot*.bmp'))
 for index,picture in enumerate(pictures):
  portable=a.out/('qualification-visible-%d.png'%index);Image.open(picture).save(portable)
  m['qualification_pictures'].append({'bmp':str(picture),'bmp_sha256':fixture.digest(picture),'png':str(portable),'png_sha256':fixture.digest(portable),'requested_frame':m['qualification_picture_frame']})
 m['qualification_picture_present']=len(pictures)==1
try:m['metrics']=summarize(a.out/'timing.csv',first,last)
except Exception as error:m['timing_error']=str(error)
m['timing_complete']=not m.get('timing_error') and bool(m['timer_status']) and all('status=complete' in line for line in m['timer_status']);m['accepted_timing_run']=run.returncode==0 and bool(m['crc']) and m['timing_complete'] and not m['validation_errors'] and not a.resource_audit
if 'metrics'in m:
 columns=['suite','variant','repeat','sync','candidate_sha256','width','height','first','last','crc','metric','samples','mean','median','p95','minimum','maximum']
 with (a.out/'summary.csv').open('w') as stream:
  writer=csv.DictWriter(stream,fieldnames=columns);writer.writeheader()
  for row in m['metrics']:writer.writerow(dict(suite=a.suite,variant=a.variant,repeat=a.repeat,sync=a.sync,candidate_sha256=a.sha256,width=1920,height=1080,first=first,last=last,crc=';'.join(m['crc']),**row))
if a.suite!='bundle' and a.repeat==0:m['accepted_timing_run'] &= m['qualification_picture_present']
write(a.out/'manifest.json',m);print(json.dumps({'accepted_timing_run':m['accepted_timing_run'],'crc':m['crc'],'timer_status':m['timer_status'],'timing_error':m.get('timing_error')}));raise SystemExit(0 if m['accepted_timing_run'] else 1)
