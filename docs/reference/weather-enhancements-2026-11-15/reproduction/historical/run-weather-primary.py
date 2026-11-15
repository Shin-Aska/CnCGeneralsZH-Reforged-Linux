from pathlib import Path
import subprocess,json,sys,time
repo=Path('/mnt/sandisk-2tb/CnCGeneralsZH-Reforged-Linux');base=repo/'build-linux/weather-enhancements-evidence';runner=repo/'docs/reference/weather-enhancements-2026-11-15/reproduction/run_weather.py';inventory=base/'benchmark-admission.json';ad=json.loads(inventory.read_text());outroot=base/'primary-runs';outroot.mkdir(exist_ok=False)
paths={'baseline-on':(base/'baseline/package/bin/generals',base/'baseline/package/share/zero-hour-reforged/overlay'),'enhanced-on':(base/'candidate-v4/generals',base/'candidate-v4/overlay'),'enhanced-off':(base/'candidate-v4/generals',base/'candidate-v4/overlay')}
orders={1:['baseline-on','enhanced-on','enhanced-off'],2:['enhanced-off','enhanced-on','baseline-on'],3:['enhanced-on','baseline-on','enhanced-off']};results=[]
for kind in ('rain','sand','snow'):
 case=ad['cases'][kind]
 for repeat in (1,2,3):
  for label in orders[repeat]:
   binary,overlay=paths[label];out=outroot/f'{kind}-{label}-r{repeat}';artifact=ad['artifacts'][label]
   cmd=[sys.executable,str(runner),'--candidate',str(binary),'--overlay',str(overlay),'--root',ad['retail_root'],'--out',str(out),'--kind',kind,'--variant',artifact['variant'],'--audio','--idle-ai','--repeat',str(repeat),'--first',str(case['first']),'--last',str(case['last']),'--width',str(case['width']),'--height',str(case['height']),'--sha256',artifact['sha256'],'--admission',str(inventory),'--label',label,'--vulkan-icd',ad['vulkan_icd'],'--run']
   print('PRIMARY_START',kind,label,repeat,flush=True);start=time.monotonic();run=subprocess.run(cmd,cwd=repo)
   result=dict(kind=kind,label=label,repeat=repeat,returncode=run.returncode,wall_seconds=time.monotonic()-start,manifest=str(out/'manifest.json'),runner_command=cmd);results.append(result);(base/'primary-run-order.json').write_text(json.dumps(results,indent=2)+'\n');print('PRIMARY_END',kind,label,repeat,'return',run.returncode,'wall',round(result['wall_seconds'],2),flush=True)
   if run.returncode:raise SystemExit('Primary rejected; preserved outputs and stopping for investigation')
print('PRIMARY_COMPLETE',len(results),flush=True)
