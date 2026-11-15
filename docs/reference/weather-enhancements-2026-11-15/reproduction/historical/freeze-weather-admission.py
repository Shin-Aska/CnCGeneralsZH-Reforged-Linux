from pathlib import Path
import json,hashlib,subprocess,re,datetime,shutil
repo=Path('/mnt/sandisk-2tb/CnCGeneralsZH-Reforged-Linux');b=repo/'build-linux/weather-enhancements-evidence';repro=repo/'docs/reference/weather-enhancements-2026-11-15/reproduction';sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
# Preserve the actual full-run log rotated by the diagnostic short check; primary prefixes never reuse.
long=b/'candidate-v4/weather_eaa6a94f4060_DebugLogFilePrev.txt';short=b/'candidate-v4/weather_eaa6a94f4060_DebugLogFile.txt'
if long.exists():shutil.copy2(long,b/'admission-headless-rain-enhanced-on/engine.log')
if short.exists():shutil.copy2(short,b/'admission-headless-short/engine.log')
results=json.loads((b/'headless-admission-results.json').read_text());cases={}
for kind in ('rain','sand','snow'):
 group=[x for x in results if x['kind']==kind];assert len(group)==3
 crcs={tuple(x['crc'][0]) for x in group};assert len(crcs)==1 and all(x['returncode']==0 for x in group)
 crc,frame=crcs.pop();metas=[json.loads(Path(x['manifest']).read_text()) for x in group]
 keys=('map_sha256','scenario_sha256','profile_common_sha256','first','last','width','height','audio','idle_ai')
 case={key:metas[0][key] for key in keys}
 for m in metas:
  assert all(m[key]==case[key] for key in keys)
  assert m['runner_sha256']==sha(repro/'run_weather.py') and m['generator_sha256']==sha(repro/'generate_map.py')
 case.update(expected_crc=crc,expected_crc_frame=int(frame),rain_seed=2096055520 if kind=='rain' else None,headless_admission_manifests=[x['manifest'] for x in group])
 cases[kind]=case
visible=json.loads((b/'admission-visible-final-rain/manifest.json').read_text());reference=json.loads((b/'admission-visible-baseline-rain/manifest.json').read_text())
assert visible['accepted'] and reference['accepted'] and visible['map_sha256']==reference['map_sha256']==cases['rain']['map_sha256']
assert visible['profile_common_sha256']==reference['profile_common_sha256']==cases['rain']['profile_common_sha256']
assert visible['crc']==reference['crc']==['HEADLESS CRC AT LIMIT: 0xA64E68FC at frame 820']
proof=b/'admission-visible-final-rain/run.log';assert 'RAIN_SEED=2096055520' in proof.read_text()
assert 'HEADLESS CRC AT LIMIT: 0xA64E68FC at frame 820' in (b/'admission-headless-short/engine.log').read_text()
retail=b/'retail-assets.json';inventory=json.loads(retail.read_text());root=Path(inventory['retail_root']);started=datetime.datetime.now(datetime.timezone.utc).isoformat();changed=[];total=0
for name,entry in inventory['files'].items():
 p=root/name;total+=p.stat().st_size
 if sha(p)!=entry['sha256']:changed.append(name)
verification=dict(inventory_sha256=sha(retail),started_utc=started,finished_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=len(inventory['files']),bytes=total,changed=changed)
(b/'retail-verification-before.json').write_text(json.dumps(verification,indent=2)+'\n');assert not changed
baseline_meta=json.loads((b/'admission-headless-rain-baseline-on/manifest.json').read_text());final_meta=json.loads((b/'admission-headless-rain-enhanced-on/manifest.json').read_text());assert baseline_meta['overlay_inventory_sha256']==final_meta['overlay_inventory_sha256']
ad={'schema':1,'qualified':True,'scope':'Matched full endpoint headless CRCs plus actual visible baseline/final observer admission; every primary must independently match the endpoint CRC/frame.',
 'runner_sha256':sha(repro/'run_weather.py'),'generator_sha256':sha(repro/'generate_map.py'),'overlay_inventory_sha256':final_meta['overlay_inventory_sha256'],
 'phase_helper_path':str(b/'rain-series'),'phase_helper_sha256':sha(b/'rain-series'),'phase_source_path':str(repo/'GeneralsMD/Code/GameEngineDevice/Include/W3DDevice/GameClient/WeatherParticleMath.h'),'phase_source_sha256':sha(repo/'GeneralsMD/Code/GameEngineDevice/Include/W3DDevice/GameClient/WeatherParticleMath.h'),
 'phase_tool_source_path':str(repro/'rain_series.cpp'),'phase_tool_source_sha256':sha(repro/'rain_series.cpp'),'phase_window_helper_path':str(b/'candidate-v4/rain-window-helper'),'phase_window_helper_sha256':sha(b/'candidate-v4/rain-window-helper'),
 'retail_inventory_path':str(retail),'retail_inventory_sha256':sha(retail),'retail_root':str(root),'retail_full_hash_policy':'Fully verify content at admission and after all primary runs; each run checks pinned inventory and file sizes.',
 'vulkan_icd':'/usr/share/vulkan/icd.d/nvidia_icd.json','clock_frame_offset':1,'clock_formula':'floor((logic_frame-1)*1000/60)/1000','clock_stamp_stage':'takeReadsForRender stamps TheGameLogic frame; phase samples describe the current draw, not earlier screenshot-request entry.',
 'rain_seed_proof':{'artifact_sha256':visible['candidate_sha256'],'map_sha256':visible['map_sha256'],'observed_seed':2096055520,'run_log_path':str(proof),'run_log_sha256':sha(proof),'manifest_path':str(b/'admission-visible-final-rain/manifest.json'),'manifest_sha256':sha(b/'admission-visible-final-rain/manifest.json')},
 'artifacts':{'baseline-on':{'sha256':baseline_meta['candidate_sha256'],'variant':'on'},'enhanced-on':{'sha256':final_meta['candidate_sha256'],'variant':'on'},'enhanced-off':{'sha256':final_meta['candidate_sha256'],'variant':'off'}},'cases':cases,
 'source_patch_sha256':sha(b/'candidate-v4/source.patch'),'baseline_overlay_note':'Dereferenced frozen accepted staged package; no mutable input overlay symlinks.'}
path=b/'benchmark-admission.json';path.write_text(json.dumps(ad,indent=2)+'\n');path.chmod(0o444)
print(json.dumps({'admission':str(path),'sha256':sha(path),'cases':{k:{x:v[x] for x in ('expected_crc','expected_crc_frame','map_sha256','rain_seed')} for k,v in cases.items()},'retail_verified_files':verification['files']},indent=2))
