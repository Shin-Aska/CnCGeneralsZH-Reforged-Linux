from pathlib import Path
import subprocess,json,sys
repo=Path('/mnt/sandisk-2tb/CnCGeneralsZH-Reforged-Linux');base=repo/'build-linux/weather-enhancements-evidence';runner=repo/'docs/reference/weather-enhancements-2026-11-15/reproduction/run_weather.py'
root='/mnt/passport/E-Drive/Linux Steam Library/steamapps/common/Command & Conquer Generals - Zero Hour'
cases=[('rain-pond','rain',[]),('rain-day','rain',['--freecam','640','220','170','90','-18']),('rain-night','rain',['--night','--freecam','640','220','170','90','-18']),('rain-atmosphere-off','rain',['--atmosphere','off','--freecam','640','220','170','90','-18']),('sand-night','sand',['--night','--freecam','340','380','360','90','-60']),('sand-atmosphere-off','sand',['--atmosphere','off','--freecam','340','380','360','90','-60']),('snow-day','snow',['--freecam','640','220','170','90','-18']),('snow-night','snow',['--night','--freecam','640','220','170','90','-18']),('snow-atmosphere-off','snow',['--atmosphere','off','--freecam','640','220','170','90','-18']),('classic-on','rain',['--variant','classic']),('classic-off','rain',['--variant','classic-off']),('authored-on','rain',['--variant','authored']),('authored-off','rain',['--variant','authored-off']),('rain-dry-maplight-off','rain',['--maplight','off','--last','3450','--shot','3334'])]
results=[]
for name,kind,extra in cases:
 out=base/('v4-matrix-'+name)
 cmd=[sys.executable,str(runner),'--candidate',str(base/'candidate-v4/generals'),'--overlay',str(base/'candidate-v4/overlay'),'--root',root,'--out',str(out),'--kind',kind,'--audio','--idle-ai','--last','700','--shot','600','--width','1280','--height','720','--gdb','/tmp/weather-v4-snapshot.gdb','--run']
 if name=='rain-pond':extra+=['--freecam','640','360','240','90','-60']
 cmd+=extra
 run=subprocess.run(cmd,cwd=repo)
 results.append(dict(name=name,returncode=run.returncode,manifest=str(out/'manifest.json')))
 (base/'v4-matrix-results.json').write_text(json.dumps(results,indent=2)+'\n')
 print('MATRIX',name,run.returncode,flush=True)
