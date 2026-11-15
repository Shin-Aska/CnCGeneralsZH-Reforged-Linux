#!/usr/bin/env python3
"""Compare completed private native timer artifacts; never executes the game."""
import argparse,csv,hashlib,json,re,statistics
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--left',type=Path,required=True);p.add_argument('--right',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--particle-qualification',action='store_true');p.add_argument('--visual-confirmed',action='store_true');a=p.parse_args()
def load(folder):return json.loads((folder/'manifest.json').read_text())
l,r=load(a.left),load(a.right);a.out.mkdir(parents=True,exist_ok=True)
checks={'same_candidate':l['executable_sha256']==r['executable_sha256'],'same_scene':l['scenario_sha256']==r['scenario_sha256'],'same_resolution':(l['width'],l['height'],l['msaa'])==(r['width'],r['height'],r['msaa']),'same_window':l['timing']==r['timing'],'same_crc':l['crc']==r['crc'],'both_complete':l['accepted_timing_run'] and r['accepted_timing_run'],'same_sync':l['sync']==r['sync']}
report={'status':'in_progress','checks':checks,'left':str(a.left.resolve()),'right':str(a.right.resolve()),'candidate_sha256':l['executable_sha256'],'manifest_sha256':{str(folder.resolve()):hashlib.sha256((folder/'manifest.json').read_bytes()).hexdigest() for folder in [a.left,a.right]},'scope':'Matched native end-to-end offscreen timer intervals; no hardware timestamp or interactive FPS claim.','parser_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
if a.particle_qualification:
 checks['qualification_pictures_present']=bool(l.get('qualification_picture_present')) and bool(r.get('qualification_picture_present'));checks['pictures_visually_confirmed']=a.visual_confirmed;report['visual_review_status']='Explicit visual confirmation supplied after image inspection' if a.visual_confirmed else 'Pending inspection of both post-window qualification images; counts alone do not prove visibility';report['qualification_pictures']={'left':l.get('qualification_pictures',[]),'right':r.get('qualification_pictures',[])};
 def normalized_profile(folder):
  return '\n'.join(line for line in (folder/'user/Options.ini').read_text().splitlines() if not re.match(r'^\s*MaxParticleCount\s*=',line,re.I))
 checks['same_profile_except_documented_cpu_cap']=normalized_profile(a.left)==normalized_profile(a.right);report['cpu_cap_policy']={'left_cpu_cap':l['cpu_capacity'],'right_cpu_cap':r['cpu_capacity'],'reason':'Unchanged game admits native particles only beyond the CPU particle ceiling. Compare exact same retained total and emitter work; this is a native overflow comparison, not identical CPU cap.'};checks['cpu_gpu_variants']={l['benchmark_variant'],r['benchmark_variant']}=={'cpu','gpu'}
 gpu=l if l['benchmark_variant']=='gpu' else r;cpu=r if l['benchmark_variant']=='gpu' else l
 window=gpu['timing'];cp={x['frame']:x['live'] for x in cpu['cpu_particles'] if window['first']<=x['frame']<=window['last']};gc={x['frame']:x['live'] for x in gpu['cpu_particles'] if window['first']<=x['frame']<=window['last']};gg={x['frame']:x['live'] for x in gpu['gpu_particles'] if window['first']<=x['frame']<=window['last']};frames=sorted(set(cp)&set(gc)&set(gg));rows=[{'frame':f,'cpu_total':cp[f],'gpu_cpu_fallback':gc[f],'gpu_native':gg[f],'gpu_total':gc[f]+gg[f],'equal':cp[f]==gc[f]+gg[f]} for f in frames]
 checks['at_least_five_count_samples']=len(frames)>=5;checks['exact_retained_populations']=bool(rows) and all(x['equal'] for x in rows);checks['native_work_positive']=bool(rows) and all(x['gpu_native']>0 for x in rows);checks['no_live_ring_evictions']=all(x['evicted']==0 for x in gpu['gpu_particles'] if window['first']<=x['frame']<=window['last']);checks['no_cpu_backend_gpu_records']=not cpu['gpu_particles'];checks['same_emitter_work']=True
 for m in [l,r]:
  log=(Path(report['left'] if m is l else report['right'])/'engine.log').read_text(errors='replace');emitters=20 if m['suite']!='particles100k' else 370
  checks['same_emitter_work'] &= bool(re.search(r"SCENARIO:.*particles.*?%d of %d"%(emitters,emitters),log))
  costs=m.get('particle_cost',[]);drop=[]
  for line in costs:
   match=re.search(r'not drawn: ([\d.]+) polygons refused.*?([\d.]+) particles past the per-system limit',line)
   if match:drop.append((float(match[1]),float(match[2])))
  checks['no_sort_or_system_drops_'+m['benchmark_variant']]=bool(drop) and all(x==(0,0) for x in drop)
 if gpu['suite']=='particles-fixed':checks['exact_target_5000']=bool(rows) and all(x['cpu_total']==5000 and x['gpu_total']==5000 for x in rows);checks['stable_fixed_population']=len({x['cpu_total'] for x in rows})==1 and len({x['gpu_total'] for x in rows})==1
 report['population_samples']=rows;report['fair_particle_comparison_qualified']=all(checks.values())
 with (a.out/'particle-populations.csv').open('w') as stream:
  writer=csv.DictWriter(stream,fieldnames=['frame','cpu_total','gpu_cpu_fallback','gpu_native','gpu_total','equal']);writer.writeheader();writer.writerows(rows)
report['passed']=all(checks.values());pending_visual=a.particle_qualification and not a.visual_confirmed and all(v for k,v in checks.items() if k!='pictures_visually_confirmed');report['status']='accepted_pair' if report['passed'] else 'pending_visual_review' if pending_visual else 'rejected_pair';summary=[]
for m,label in [(l,'left'),(r,'right')]:
 for value in m.get('metrics',[]):summary.append({'side':label,'variant':m['benchmark_variant'],'sync':m['sync'],**value})
with (a.out/'comparison.csv').open('w') as stream:
 writer=csv.DictWriter(stream,fieldnames=['side','variant','sync','metric','samples','mean','median','p95','minimum','maximum']);writer.writeheader();writer.writerows(summary)
(a.out/'comparison.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'status':report['status'],'checks':checks}));raise SystemExit(0 if report['passed'] else 2 if pending_visual else 1)
