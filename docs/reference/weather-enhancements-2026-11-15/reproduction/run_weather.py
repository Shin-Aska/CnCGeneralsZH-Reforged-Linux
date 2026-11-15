#!/usr/bin/env python3
"""Isolated deterministic weather qualification/benchmark. Never edits installed data."""
import argparse, csv, hashlib, json, math, os, platform, re, shutil, statistics, subprocess, time
from pathlib import Path
import generate_map as fixture

def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(8 * 1024 * 1024), b''): h.update(chunk)
    return h.hexdigest()

def write(path, value): path.write_text(json.dumps(value, indent=2) + '\n')

def inventory_digest(folder):
    entries={str(path.relative_to(folder)):{'sha256':digest(path),'size':path.stat().st_size}
        for path in sorted(folder.rglob('*')) if path.is_file()}
    return hashlib.sha256(json.dumps(entries,sort_keys=True,separators=(',',':')).encode()).hexdigest()

def hardware():
    result={'platform':platform.platform()}
    if shutil.which('nvidia-smi'):
        command=['nvidia-smi','--query-gpu=name,uuid,driver_version,memory.total,memory.used,temperature.gpu,pstate,power.draw,clocks.current.graphics,clocks.current.memory','--format=csv,noheader']
        run=subprocess.run(command,text=True,capture_output=True)
        result['nvidia_smi']=dict(command=command,returncode=run.returncode,stdout=run.stdout,stderr=run.stderr)
    return result

def map_bytes(kind, night):
    dictionary, ascii_value, waypoint = fixture.MapWriter.dictionary, fixture.MapWriter.ascii, fixture.write_waypoint
    def dictionary_override(writer, value):
        if set(value) == {'weather', 'compression'}: value = dict(value, weather=1 if kind == 'snow' else 0)
        dictionary(writer, value)
    def ascii_override(writer, value):
        if value == 'DirtType1': value = {'sand':'SandMediumType1', 'snow':'DirtSnowType1'}.get(kind, value)
        ascii_value(writer, value)
    def objects(writer, number, x, y):
        waypoint(writer, number, x, y)
        if number != 2: return
        for index, (name, x, y) in enumerate([
            ('AmericaPowerPlant', 700, 740), ('AmericaVehicleHumvee', 810, 700),
            ('TreeFir01B', 440, 700), ('TreeFir01B', 480, 720), ('TreeFir01B', 950, 690)]):
            with writer.chunk('Object', 3):
                for v in (x, y, 37.5, 0): writer.real(v)
                writer.integer(0); writer.ascii(name)
                writer.dictionary({'objectInitialHealth':100, 'objectEnabled':True, 'objectPowered':True,
                    'objectRecruitableAI':True, 'originalOwner':'teamPlyrCivilian',
                    'uniqueID':'WeatherSubject%d'%index, 'objectDestructible':True})
    fixture.MapWriter.dictionary = dictionary_override; fixture.MapWriter.ascii = ascii_override; fixture.write_waypoint = objects
    result = fixture.build_map(inspection_yard=True)
    # GlobalLighting is the last nonempty chunk before WaypointsList. Keep the normal map writer's
    # exact binary layout; change its authored day slot and neutral light only for night qualification.
    if night:
        import struct
        marker = struct.pack('<i', 2) + struct.pack('<9f', .45,.45,.45,.55,.55,.55,-.5,.5,-1)
        pos = result.index(marker)
        result = result[:pos] + struct.pack('<i', 4) + result[pos+4:]
        lights = bytearray(result)
        for slot in range(4):
            for channel, value in enumerate((.10,.14,.24,.05,.07,.12,-.5,.5,-1,.10,.14,.24,.05,.07,.12,-.5,.5,-1)):
                struct.pack_into('<f', lights, pos+4+slot*216+channel*4, value)
        result = bytes(lights)
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('candidate','overlay','root','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--kind',choices=('rain','sand','snow'),required=True)
    p.add_argument('--variant',choices=('on','off','classic','classic-off','authored','authored-off','night'),default='on')
    p.add_argument('--repeat',type=int,default=0); p.add_argument('--run',action='store_true')
    p.add_argument('--shot',type=int,default=600); p.add_argument('--first',type=int,default=601)
    p.add_argument('--last',type=int,default=2400);p.add_argument('--width',type=int,default=1920);p.add_argument('--height',type=int,default=1080)
    p.add_argument('--audio',action='store_true');p.add_argument('--gdb',type=Path)
    p.add_argument('--night',action='store_true');p.add_argument('--atmosphere',choices=('on','off'),default='on')
    p.add_argument('--maplight',choices=('on','off'),default='on')
    p.add_argument('--cinema',type=Path);p.add_argument('--freecam',nargs=5,type=float)
    p.add_argument('--passive',action='store_true',help='Rejected diagnostic: takeover human seats change the observer/shroud view')
    p.add_argument('--idle-ai',action='store_true',help='Keep ordinary AI observer ownership, one starting cash and ten-minute peace')
    p.add_argument('--sha256',help='Required immutable executable identity for primary timing')
    p.add_argument('--admission',type=Path,help='Frozen matched artifact/fixture/CRC/helper admission inventory')
    p.add_argument('--label',choices=('baseline-on','enhanced-on','enhanced-off'))
    p.add_argument('--vulkan-icd',help='Explicit host Vulkan ICD; otherwise preserve loader environment/default discovery')
    a=p.parse_args()
    for key in ('candidate','overlay','root','out'):setattr(a,key,getattr(a,key).resolve())
    candidate_sha=digest(a.candidate)
    if a.sha256 and candidate_sha!=a.sha256:raise SystemExit('Candidate hash mismatch')
    if a.repeat>0 and not a.sha256:raise SystemExit('Primary timing requires --sha256')
    if a.repeat>0 and a.passive:raise SystemExit('Takeover changes observer visibility and is not admitted for primary timing')
    if a.run and a.repeat>0 and (not a.admission or not a.label):raise SystemExit('Primary execution requires --admission and --label')
    a.out.mkdir(parents=True,exist_ok=False);user=a.out/'user';user.mkdir(); overlay=a.out/'overlay';overlay.mkdir()
    log_prefix='weather_'+hashlib.sha256(str(a.out).encode()).hexdigest()[:12]+'_'
    for entry in a.overlay.iterdir():
        if entry.name not in ('Maps','Scenarios','Cinema'): (overlay/entry.name).symlink_to(entry, target_is_directory=entry.is_dir())
    maps=overlay/'Maps/WeatherEvidence';maps.mkdir(parents=True)
    (maps/'WeatherEvidence.map').write_bytes(map_bytes(a.kind,a.night or a.variant=='night'))
    if a.variant.startswith('authored'):(maps/'map.ini').write_text('Weather\n SnowEnabled = yes\n SnowTexture = EXRainDrop.tga\nEnd\n')
    scenarios=overlay/'Scenarios';scenarios.mkdir()
    (scenarios/'weather_evidence.txt').write_text('; Fixed passive scene, no timed scenario actions.\n')
    options=f'''Resolution = {a.width} {a.height}
ClassicGraphics = {'yes' if a.variant.startswith('classic') else 'no'}
Weather = {'no' if a.variant in ('off','classic-off','authored-off') else 'yes'}
DetailedModels = yes
HDTextures = yes
WaterStyle = 1
CloudStyle = 0
LightingStyle = 0
ShadowQuality = 0
UseShadowVolumes = yes
UseShadowDecals = yes
StaticGameLOD = Custom
IdealStaticGameLOD = High
DynamicLOD = no
TextureReduction = 0
SmoothMotion = no
VSync = no
MSAA = 0
MaxParticleCount = 3000
Atmosphere = {'yes' if a.atmosphere=='on' else 'no'}
MapLight = {'yes' if a.maplight=='on' else 'no'}
AmbientOcclusion = no
InternalHdr = no
Bloom = 0
MusicVolume = 0
SFXVolume = 80
AmbientVolume = 70
VoiceVolume = 0
'''
    (user/'Options.ini').write_text(options)
    command=[str(a.candidate),'-root',str(a.root),'-overlay',str(overlay),'-offscreen','-multiInstance','-win',
        '-xres',str(a.width),'-yres',str(a.height),'-msaa','0','-quickstart','-noshellmap','-nologo','-novideo',
        '-noFPSLimit','-noDynamicLOD','-turbo','-camera','640','600','-maxframes',str(a.last+120),
        '-particlecap','3000','-gpuparticles','0','-dx11post','off','-logPrefix',log_prefix,
        '-map','Maps/WeatherEvidence/WeatherEvidence.map','-autoskirmish','2','-observer','-seed','7',
        '-side','0','FactionAmerica','-side','1','FactionChina','-scenario','weather_evidence']
    if not a.audio:command.append('-noaudio')
    if a.passive:command.append('-takeover')
    if a.idle_ai:command+=['-startingcash','1','-peacetime','10']
    # Existing capture-clock admission requires a pending -screenshot at startup. In timing
    # runs arm it AFTER the quit limit, so no capture/readback occurs in a timing run at all.
    capture_frame=a.shot if a.repeat==0 and not a.cinema else a.last+180
    command+=['-screenshot',str(capture_frame)]
    if a.freecam:command+=['-freecam']+[str(value) for value in a.freecam]
    if a.cinema:
        cinema=overlay/'Cinema';cinema.mkdir();shutil.copy2(a.cinema,cinema/'weather_camera.txt');command+=['-cinema','weather_camera']
    if a.gdb:command=['gdb','-nx','--batch','--return-child-result','-x',str(a.gdb.resolve()),'--args']+command
    env=dict(ZH_USER_DATA_DIR=str(user),ZH_UNATTENDED='1',ZH_AUDIO_BACKEND='null',SDL_VIDEODRIVER='offscreen',
        SDL_GPU_DRIVER='vulkan',ZH_OFFSCREEN_HZ='0',
        ZH_SUN_SHADOW_CAPTURE_CLOCK='logic',ZH_WATER_CAPTURE_CLOCK='picture')
    if a.vulkan_icd:env['VK_DRIVER_FILES']=a.vulkan_icd
    elif os.environ.get('VK_DRIVER_FILES'):env['VK_DRIVER_FILES']=os.environ['VK_DRIVER_FILES']
    if a.audio:env['ZH_ALLOW_AUDIO']='1'
    if a.repeat>0:env.update(ZH_GPU_TIMING='0,1',ZH_GPU_TIMING_LOGIC=f'{a.first},{a.last}',ZH_GPU_TIMING_CSV=str(a.out/'timing.csv'),ZH_GPU_TIMING_SYNC='0')
    manifest=dict(command=command,environment=env,candidate_sha256=candidate_sha,kind=a.kind,variant=a.variant,repeat=a.repeat,
        map_sha256=digest(maps/'WeatherEvidence.map'),profile_sha256=digest(user/'Options.ini'),first=a.first,last=a.last,
        night=a.night or a.variant=='night',atmosphere=a.atmosphere,maplight=a.maplight,freecam=a.freecam,
        passive=a.passive,audio=a.audio,
        idle_ai=a.idle_ai,
        capture_clock_arm_frame=capture_frame,capture_outside_timing_window=a.repeat<=0 or capture_frame>a.last,
        capture_clock_arm_inert=a.repeat>0 or bool(a.cinema),
        runner_sha256=digest(Path(__file__).resolve()),generator_sha256=digest(Path(fixture.__file__).resolve()),log_prefix=log_prefix,
        scenario_sha256=digest(scenarios/'weather_evidence.txt'),warmup_logic_frames=a.first-1,
        profile_common_sha256=hashlib.sha256(''.join(line+'\n' for line in options.splitlines() if not line.startswith('Weather =')).encode()).hexdigest(),
        overlay_inventory_sha256=inventory_digest(a.overlay),label=a.label,
        sample_window_simulated_seconds=(a.last-a.first+1)/60,
        expected_samples=a.last-a.first+1,width=a.width,height=a.height,platform=platform.platform(),
        cpu=sorted(set(re.findall(r'^model name\s*:\s*(.+)$',Path('/proc/cpuinfo').read_text(),re.M))),
        weather_clock='Existing deterministic W3D capture clock: one logic frame = 1/60s; armed at startup by an existing screenshot request',
        timing_basis='Native end-to-end offscreen present intervals, not GPU timestamps/FPS; '+('real null-backend audio mixer enabled' if a.audio else 'audio disabled: render-only cost'))
    admission=None
    if a.admission:
        admission=json.loads(a.admission.read_text());case=admission['cases'][a.kind]
        assert admission['qualified'] and a.label in admission['artifacts']
        artifact=admission['artifacts'][a.label]
        assert candidate_sha==artifact['sha256'] and a.variant==artifact['variant'],'Artifact/Weather variant mismatch'
        for key in ('runner_sha256','generator_sha256','overlay_inventory_sha256'):
            assert manifest[key]==admission[key],key+' differs from frozen admission'
        for key in ('map_sha256','scenario_sha256','profile_common_sha256','first','last','width','height','audio','idle_ai'):
            value=getattr(a,key) if key in ('audio','idle_ai') else manifest[key]
            assert value==case[key],key+' differs from qualified fixture'
        assert not a.gdb and not a.cinema and not a.freecam and not a.night and a.variant in ('on','off'),'Unqualified diagnostic/view override in primary'
        for key in ('phase_helper','phase_source','phase_tool_source','retail_inventory'):
            assert digest(Path(admission[key+'_path']))==admission[key+'_sha256'],key+' identity mismatch'
        assert str(a.root)==admission['retail_root'],'Retail root differs from admitted asset inventory'
        assert env.get('VK_DRIVER_FILES')==admission['vulkan_icd'],'Vulkan ICD differs from admitted device selection'
        retail=json.loads(Path(admission['retail_inventory_path']).read_text())
        for name,entry in retail['files'].items():assert (a.root/name).stat().st_size==entry['size'],'Retail file size changed: '+name
        manifest.update(admission_sha256=digest(a.admission),rain_seed=case.get('rain_seed'),
            phase_helper_sha256=admission['phase_helper_sha256'],phase_source_sha256=admission['phase_source_sha256'],
            phase_tool_source_sha256=admission['phase_tool_source_sha256'],
            retail_inventory_sha256=admission['retail_inventory_sha256'],expected_crc=case['expected_crc'],expected_crc_frame=case['expected_crc_frame'])
    write(a.out/'manifest.json',manifest)
    if not a.run:return
    engine_sources=(user/'Logs'/(log_prefix+'DebugLogFile.txt'),a.candidate.parent/(log_prefix+'DebugLogFile.txt'))
    assert not any(path.exists() for path in engine_sources),'Unique per-run log prefix collision'
    execution=os.environ.copy()
    for key in ('ZH_GPU_DEBUG','VK_INSTANCE_LAYERS','ZH_GPU_CREATION_LOG'):execution.pop(key,None)
    execution.update(env);manifest['hardware_before']=hardware();start=time.monotonic()
    with (a.out/'run.log').open('w') as stream:run=subprocess.run(command,cwd=a.out,env=execution,stdout=stream,stderr=subprocess.STDOUT,timeout=600)
    manifest.update(returncode=run.returncode,wall_seconds=time.monotonic()-start,hardware_after=hardware(),candidate_after_sha256=digest(a.candidate))
    log=(a.out/'run.log').read_text(errors='replace')
    for engine in engine_sources:
        if engine.exists():
            shutil.copy2(engine,a.out/'engine.log');log+='\n'+engine.read_text(errors='replace')
            manifest['engine_log_provenance']=dict(path=str(engine),new_for_run=True,sha256=digest(engine));break
    manifest['crc']=re.findall(r'HEADLESS CRC AT LIMIT:.*',log)
    manifest['validation_errors']=[x for x in log.splitlines() if 'VUID-' in x or 'Validation Error' in x]
    manifest['timer_status']=re.findall(r'PosixDevice9 timing: LOGIC[^\n]+',log)
    if a.repeat>0:
        try:
            with (a.out/'timing.csv').open() as stream:rows=list(csv.DictReader(stream))
            assert [int(r['logic_frame']) for r in rows]==list(range(a.first,a.last+1)), 'Incomplete/nonconsecutive timing window'
            metrics={}
            for key in ('frame_ms','work_ms','device_draw_ms','device_flush_ms','device_present_ms','swapchain_wait_ms','offscreen_wait_ms','fence_ms','draws','flushes'):
                values=sorted(float(r[key]) for r in rows);assert all(math.isfinite(x) and x>=0 for x in values)
                metrics[key]=dict(samples=len(values),mean=statistics.mean(values),median=statistics.median(values),p95=values[math.ceil(.95*len(values))-1],minimum=values[0],maximum=values[-1])
            manifest['metrics']=metrics
            manifest['timing_csv_sha256']=digest(a.out/'timing.csv')
        except Exception as error:manifest['timing_error']=str(error)
    else:
        from PIL import Image
        manifest['pictures']=[]
        for i,path in enumerate(sorted(user.glob('sshot*.bmp'))):
            png=a.out/f'picture-{i}.png';Image.open(path).save(png);manifest['pictures'].append(dict(path=str(png),sha256=digest(png)))
    if admission:
        parsed=re.findall(r'HEADLESS CRC AT LIMIT: (0x[0-9A-Fa-f]+) at frame (\d+)',log)
        manifest['crc_admitted']=len(set(parsed))==1 and parsed[0][0].upper()==manifest['expected_crc'].upper() and int(parsed[0][1])==manifest['expected_crc_frame']
        manifest['overlay_after_inventory_sha256']=inventory_digest(a.overlay)
    manifest['accepted']=run.returncode==0 and bool(manifest['crc']) and not manifest['validation_errors'] and manifest['candidate_after_sha256']==candidate_sha and (
        bool(manifest.get('metrics')) and bool(manifest['timer_status']) and all('status=complete' in line for line in manifest['timer_status']) if a.repeat>0 else bool(manifest.get('pictures')))
    if admission:manifest['accepted'] &= manifest['crc_admitted'] and manifest['overlay_after_inventory_sha256']==manifest['overlay_inventory_sha256']
    write(a.out/'manifest.json',manifest)
    print(json.dumps({key:manifest.get(key) for key in ('accepted','returncode','crc','timing_error','pictures')}))
    raise SystemExit(0 if manifest['accepted'] else 1)

if __name__=='__main__':main()
