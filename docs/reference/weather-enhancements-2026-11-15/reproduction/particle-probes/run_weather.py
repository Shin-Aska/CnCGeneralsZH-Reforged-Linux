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
    p.add_argument('--variant',choices=('on','off','classic','authored','night'),default='on')
    p.add_argument('--repeat',type=int,default=0); p.add_argument('--run',action='store_true')
    p.add_argument('--shot',type=int,default=600); p.add_argument('--first',type=int,default=601)
    p.add_argument('--last',type=int,default=2400);p.add_argument('--width',type=int,default=1920);p.add_argument('--height',type=int,default=1080)
    p.add_argument('--audio',action='store_true');p.add_argument('--gdb',type=Path)
    p.add_argument('--shape',choices=('pond','river'),default='pond');p.add_argument('--shroud',action='store_true');p.add_argument('--video',nargs=2,type=int);p.add_argument('--idle-ai',action='store_true')
    p.add_argument('--night',action='store_true');p.add_argument('--atmosphere',choices=('on','off'),default='on')
    p.add_argument('--maplight',choices=('on','off'),default='on')
    p.add_argument('--cinema',type=Path);p.add_argument('--freecam',nargs=5,type=float)
    p.add_argument('--passive',action='store_true',help='Use existing -takeover seats to keep the long timing fixture idle')
    a=p.parse_args()
    if a.shape=='river':
        def write_river(writer):
            writer.integer(1);writer.ascii('WeatherSlopedRiver');writer.ascii('Weather sloped river')
            writer.integer(1);writer.body.extend(bytes((1,1)));writer.integer(0);writer.integer(4)
            for point in ((350,420,18),(350,520,18),(930,520,78),(930,420,78)):
                for value in point:writer.integer(value)
        fixture.write_water=write_river
    for key in ('candidate','overlay','root','out'):setattr(a,key,getattr(a,key).resolve())
    a.out.mkdir(parents=True,exist_ok=False);user=a.out/'user';user.mkdir(); overlay=a.out/'overlay';overlay.mkdir()
    for entry in a.overlay.iterdir():
        if entry.name not in ('Maps','Scenarios','Cinema'): (overlay/entry.name).symlink_to(entry, target_is_directory=entry.is_dir())
    maps=overlay/'Maps/WeatherEvidence';maps.mkdir(parents=True)
    (maps/'WeatherEvidence.map').write_bytes(map_bytes(a.kind,a.night or a.variant=='night'))
    if a.variant=='authored':(maps/'map.ini').write_text('Weather\n SnowEnabled = yes\n SnowTexture = EXRainDrop.tga\nEnd\n')
    scenarios=overlay/'Scenarios';scenarios.mkdir()
    (scenarios/'weather_evidence.txt').write_text('; Fixed passive scene, no timed scenario actions.\n')
    options=f'''Resolution = {a.width} {a.height}
ClassicGraphics = {'yes' if a.variant=='classic' else 'no'}
Weather = {'no' if a.variant=='off' else 'yes'}
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
        '-particlecap','3000','-gpuparticles','0','-dx11post','off','-logPrefix','weather_',
        '-map','Maps/WeatherEvidence/WeatherEvidence.map','-autoskirmish','2','-observer','-seed','7',
        '-side','0','FactionAmerica','-side','1','FactionChina','-scenario','weather_evidence']
    if a.video:command+=['-video',str(a.video[0]),str(a.video[1]),'particle_weather']
    if a.idle_ai:command+=['-startingcash','1','-peacetime','10']
    if a.passive:command.append('-takeover')
    if a.shroud:
        if '-noshroud' in command:command.remove('-noshroud')
        command.remove('-observer')
    if not a.audio:command.append('-noaudio')
    if a.repeat==0:command+=['-screenshot',str(a.last+60 if a.cinema else a.shot)]
    if a.freecam:command+=['-freecam']+[str(value) for value in a.freecam]
    if a.cinema:
        cinema=overlay/'Cinema';cinema.mkdir();shutil.copy2(a.cinema,cinema/'weather_camera.txt');command+=['-cinema','weather_camera']
    if a.gdb:command=['gdb','-nx','--batch','--return-child-result','-x',str(a.gdb.resolve()),'--args']+command
    env=dict(ZH_USER_DATA_DIR=str(user),ZH_UNATTENDED='1',ZH_AUDIO_BACKEND='null',SDL_VIDEODRIVER='offscreen',
        SDL_GPU_DRIVER='vulkan',VK_DRIVER_FILES='/usr/share/vulkan/icd.d/nvidia_icd.json',ZH_OFFSCREEN_HZ='0',
        ZH_SUN_SHADOW_CAPTURE_CLOCK='logic',ZH_WATER_CAPTURE_CLOCK='picture')
    if a.audio:env['ZH_ALLOW_AUDIO']='1'
    if a.repeat>0:env.update(ZH_GPU_TIMING='0,1',ZH_GPU_TIMING_LOGIC=f'{a.first},{a.last}',ZH_GPU_TIMING_CSV=str(a.out/'timing.csv'),ZH_GPU_TIMING_SYNC='0')
    manifest=dict(command=command,environment=env,candidate_sha256=digest(a.candidate),kind=a.kind,variant=a.variant,repeat=a.repeat,
        map_sha256=digest(maps/'WeatherEvidence.map'),profile_sha256=digest(user/'Options.ini'),first=a.first,last=a.last,
        night=a.night or a.variant=='night',atmosphere=a.atmosphere,maplight=a.maplight,freecam=a.freecam,
        passive=a.passive,
        expected_samples=a.last-a.first+1,width=a.width,height=a.height,platform=platform.platform(),
        cpu=sorted(set(re.findall(r'^model name\s*:\s*(.+)$',Path('/proc/cpuinfo').read_text(),re.M))),
        weather_clock='Existing deterministic W3D capture clock: one logic frame = 1/60s; primary window covers 30s after10s warmup',
        timing_basis='Native end-to-end offscreen present intervals, not GPU timestamps/FPS; '+('real null-backend audio mixer enabled' if a.audio else 'audio disabled: render-only cost'))
    write(a.out/'manifest.json',manifest)
    if not a.run:return
    execution=os.environ.copy()
    for key in ('ZH_GPU_DEBUG','VK_INSTANCE_LAYERS','ZH_GPU_CREATION_LOG'):execution.pop(key,None)
    execution.update(env);start=time.monotonic()
    with (a.out/'run.log').open('w') as stream:run=subprocess.run(command,cwd=a.out,env=execution,stdout=stream,stderr=subprocess.STDOUT,timeout=600)
    manifest.update(returncode=run.returncode,wall_seconds=time.monotonic()-start)
    log=(a.out/'run.log').read_text(errors='replace')
    for engine in (user/'Logs/weather_DebugLogFile.txt', a.candidate.parent/'weather_DebugLogFile.txt'):
        if engine.exists():shutil.copy2(engine,a.out/'engine.log');log+='\n'+engine.read_text(errors='replace');break
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
        except Exception as error:manifest['timing_error']=str(error)
    else:
        from PIL import Image
        manifest['pictures']=[]
        for i,path in enumerate(sorted(user.glob('sshot*.bmp'))):
            png=a.out/f'picture-{i}.png';Image.open(path).save(png);manifest['pictures'].append(dict(path=str(png),sha256=digest(png)))
    manifest['accepted']=run.returncode==0 and bool(manifest['crc']) and not manifest['validation_errors'] and (bool(manifest.get('metrics')) if a.repeat>0 else bool(manifest.get('pictures')))
    write(a.out/'manifest.json',manifest)
    print(json.dumps({key:manifest.get(key) for key in ('accepted','returncode','crc','timing_error','pictures')}))
    raise SystemExit(0 if manifest['accepted'] else 1)

if __name__=='__main__':main()
