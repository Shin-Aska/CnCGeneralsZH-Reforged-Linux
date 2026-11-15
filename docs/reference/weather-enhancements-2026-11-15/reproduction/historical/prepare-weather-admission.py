from pathlib import Path
import subprocess,json,os,sys,re,hashlib
repo=Path('/mnt/sandisk-2tb/CnCGeneralsZH-Reforged-Linux');base=repo/'build-linux/weather-enhancements-evidence';runner=repo/'docs/reference/weather-enhancements-2026-11-15/reproduction/run_weather.py';retail='/mnt/passport/E-Drive/Linux Steam Library/steamapps/common/Command & Conquer Generals - Zero Hour'
artifacts={'baseline-on':(base/'baseline/package/bin/generals',base/'baseline/package/share/zero-hour-reforged/overlay','on'),'enhanced-on':(base/'candidate-v4/generals',base/'candidate-v4/overlay','on'),'enhanced-off':(base/'candidate-v4/generals',base/'candidate-v4/overlay','off')}
results=[]
for kind in ('rain','sand','snow'):
 last=25200 if kind=='rain' else 2400
 for label,(binary,overlay,variant) in artifacts.items():
  out=base/('admission-headless-'+kind+'-'+label)
  sha=hashlib.sha256(binary.read_bytes()).hexdigest()
  prepare=[sys.executable,str(runner),'--candidate',str(binary),'--overlay',str(overlay),'--root',retail,'--out',str(out),'--kind',kind,'--variant',variant,'--audio','--idle-ai','--repeat','1','--first','601','--last',str(last),'--sha256',sha,'--label',label,'--vulkan-icd','/usr/share/vulkan/icd.d/nvidia_icd.json']
  subprocess.run(prepare,cwd=repo,check=True)
  m=json.loads((out/'manifest.json').read_text());cmd=m['command'];cmd.remove('-offscreen');cmd+=['-headless','-noaudio'];env=os.environ.copy();env.update(m['environment']);env.pop('ZH_ALLOW_AUDIO',None)
  for key in ('ZH_GPU_TIMING','ZH_GPU_TIMING_LOGIC','ZH_GPU_TIMING_CSV','ZH_GPU_TIMING_SYNC'):env.pop(key,None)
  with (out/'headless-run.log').open('w') as stream:run=subprocess.run(cmd,cwd=out,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=120)
  text=(out/'headless-run.log').read_text(errors='replace')
  for log in [out/'user/Logs'/(m['log_prefix']+'DebugLogFile.txt'),binary.parent/(m['log_prefix']+'DebugLogFile.txt')]:
   if log.exists():text+='\n'+log.read_text(errors='replace');break
  crc=re.findall(r'HEADLESS CRC AT LIMIT: (0x[0-9A-Fa-f]+) at frame (\d+)',text)
  result=dict(kind=kind,label=label,returncode=run.returncode,crc=crc,manifest=str(out/'manifest.json'),command=cmd,headless_no_gpu_log='PosixDevice9:' not in text)
  results.append(result);(base/'headless-admission-results.json').write_text(json.dumps(results,indent=2)+'\n');print('HEADLESS',kind,label,run.returncode,crc,flush=True)
