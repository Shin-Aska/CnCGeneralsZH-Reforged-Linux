#!/usr/bin/env python3
"""Loopback-only, standard-library clean FFT water tuner."""
import argparse
import json
import math
import os
import sys
from pathlib import Path
import tempfile
from http.server import BaseHTTPRequestHandler, HTTPServer

# key, label, minimum, maximum, default, step, group, plain-language hint
CONTROLS = [
    ('travel_angle', 'Sea direction', -180, 180, -135, 1, 'Waves', 'Direction waves travel on the map: 0° right (+x), 90° up (+y). Manual / fallback direction. Auto shows the map direction read-only; 0° right (+x), 90° up (+y).'),
    ('travel_mode', 'Direction mode', None, None, 'auto', None, 'Waves', "Auto uses the map's prevailing wave paths, including the original fallback direction. Manual uses your chosen angle and recalculates those paths."),
    ('wind_speed', 'Wind speed', 1, 30, 8.7, .1, 'Waves', 'Wind at sea in metres per second; shapes the spectrum.'),
    ('fetch', 'Fetch', 1000, 200000, 20000, 100, 'Waves', 'Wind travel distance in metres; more fetch makes larger waves.'),
    ('swell', 'Swell', 0, 2, .7, .01, 'Waves', 'Higher values align waves into longer crests.'),
    ('heading_follow', 'Heading', 0, 1, 1, .01, 'Waves', "How strongly waves follow the map's wave paths around islands and into bays."),
    ('wave_speed', 'Speed', .1, 1.5, .4, .01, 'Waves', 'Slows or speeds up the FFT clock without changing wave size.'),
    ('sea2_share', 'Second sea', 0, .9, .33, .01, 'Mixed seas', 'Fraction of long and medium wave energy assigned to a second sea; zero disables it.'),
    ('sea2_angle', 'Travel angle', -180, 180, 135, 1, 'Mixed seas', 'Absolute travel direction in degrees in the world XY plane.'),
    ('sea2_wavelength', 'Wavelength', 100, 1500, 600, 10, 'Mixed seas', 'Dominant wavelength of the second sea in game units.'),
    ('sea2_spread', 'Narrowness', 1, 40, 12, .1, 'Mixed seas', 'Higher values align the second sea into narrower traveling crests.'),
    ('spread', 'Main spread', 0, 1, 0, .01, 'Mixed seas', 'Blends the main sea toward equal energy in every direction; one is isotropic.'),
    ('height_long', 'Long height', 0, 3, 1.5, .01, 'Strength', 'RMS height of broad waves in game units with fixed split.'),
    ('height_medium', 'Medium height', 0, 3, .75, .01, 'Strength', 'RMS height of middle waves in game units with fixed split.'),
    ('height_fine', 'Fine height', 0, 3, .18, .01, 'Strength', 'RMS height of small ripples in game units with fixed split.'),
    ('split', 'Band split', None, None, 'spectrum', None, 'Strength', 'Fixed sets each band height; spectrum shares the total height budget.'),
    ('chop_long', 'Long chop', 0, 2.5, 1.83, .01, 'Strength', 'Horizontal push of broad waves; higher values sharpen crests.'),
    ('chop_short', 'Short chop', 0, 2.5, 1.92, .01, 'Strength', 'Horizontal push of middle waves and fine ripples.'),
    ('depth_long', 'Long reach', .5, 40, 24, .1, 'Shallow water', 'Depth where broad waves reach full strength; higher fades sooner.'),
    ('depth_medium', 'Medium reach', .5, 40, 12, .1, 'Shallow water', 'Depth where middle waves reach full strength.'),
    ('depth_fine', 'Fine reach', .5, 40, 4, .1, 'Shallow water', 'Depth where fine ripples reach full strength.'),
    ('shore_turn', 'Shore turn', 0, 1, 1, .01, 'Shore', 'Extra pull toward the local shoreline after following the wave paths; zero disables this extra pull.'),
    ('shore_max_turn', 'Maximum turn', 0, 180, 60, 1, 'Shore', 'Maximum extra shore pull in degrees; the map wave paths are not limited by this control.'),
    ('shore_reach', 'Turn reach', 100, 3000, 1500, 10, 'Shore', 'Distance from land in game units where the extra shore pull fades away.'),
    ('shore_tile', 'Patch size', 100, 3000, 300, 10, 'Shore', 'Hexagonal patch size in game units for following wave paths and extra shore pull.'),
    ('shore_sharp', 'Blend sharpness', 1, 8, 3, .1, 'Shore', 'Higher values narrow the blend between nearby wave directions.'),
    ('shore_bands', 'Bands', None, None, 'all', None, 'Shore', 'Apply wave paths and extra shore pull to medium, long + medium, or all waves including fine ripples and glints (default).'),
    ('crest_color', 'Crest colour', 0, 1, .11, .01, 'Look', 'Green light through wave tops; zero removes the extra tint.'),
    ('foam_threshold', 'Foam', 0, 1, .73, .01, 'Look', 'Higher values make whitecaps easier to form; zero turns them off.'),
    ('glint', 'Glint', 0, 4, 1, .01, 'Look', 'Spreads the sun highlight using tiny waves too small to see.'),
]
SPECS = {row[0]: row for row in CONTROLS}
DEFAULTS = {key: row[4] for key, row in SPECS.items()}
ENUMS = {'travel_mode': ('auto', 'manual'), 'split': ('fixed', 'spectrum'), 'shore_bands': ('medium', 'long_medium', 'all')}


def validate(values):
    if not isinstance(values, dict):
        raise ValueError('values must be an object')
    result = {}
    for key, value in values.items():
        legacy_wind = key == 'wind_angle'
        if legacy_wind:
            key = 'travel_angle'
        if key not in SPECS:
            raise ValueError(f'Unknown parameter: {key}')
        if key in ENUMS:
            if value not in ENUMS[key]:
                raise ValueError(f'{key} must be one of {ENUMS[key]}')
        else:
            if isinstance(value, bool) or not isinstance(value, (int, float)):
                raise ValueError(f'{key} must be a finite number')
            try:
                value = float(value)
            except (ValueError, OverflowError):
                raise ValueError(f'{key} must be a finite number') from None
            if not math.isfinite(value):
                raise ValueError(f'{key} must be a finite number')
            value = max(SPECS[key][2], min(SPECS[key][3], value))
            if legacy_wind:
                value = (value + 360) % 360 - 180  # normalize(wind + 180)
        result[key] = value
    return result


def start_values():
    values = dict(DEFAULTS)
    tuples = [
        ('ZH_WATER_CLEAN_SPECTRUM', ('wind_speed', 'fetch', 'swell'), ((1, 50), (100, 1000000), (0, 2))),
        ('ZH_WATER_CLEAN_LOOK', ('crest_color', 'foam_threshold', 'glint'), ((0, 1), (0, 1), (0, 4))),
        ('ZH_WATER_CLEAN_HEIGHTS', ('height_long', 'height_medium', 'height_fine'), None),
        ('ZH_WATER_CLEAN_CHOP', ('chop_long', 'chop_short'), None),
        ('ZH_WATER_CLEAN_DEPTHS', ('depth_long', 'depth_medium', 'depth_fine'), None),
        ('ZH_WATER_CLEAN_SEA2', ('sea2_share', 'sea2_angle', 'sea2_wavelength', 'sea2_spread'), None),
        ('ZH_WATER_CLEAN_SHORE', ('shore_turn', 'shore_reach', 'shore_tile', 'shore_sharp'), None),
    ]
    for env, keys, limits in tuples:
        try:
            numbers = [float(v) for v in os.environ[env].split(',')]
            if len(numbers) != len(keys) or not all(math.isfinite(v) for v in numbers):
                continue
            if limits and any(not low <= v <= high for v, (low, high) in zip(numbers, limits)):
                continue
            values.update(validate(dict(zip(keys, numbers))))
        except (KeyError, ValueError):
            pass
    if os.environ.get('ZH_WATER_CLEAN_TRAVEL_MODE') == 'manual':
        values['travel_mode'] = 'manual'
    if os.environ.get('ZH_WATER_CLEAN_SPLIT') in ENUMS['split']:
        values['split'] = os.environ['ZH_WATER_CLEAN_SPLIT']
    if os.environ.get('ZH_WATER_CLEAN_SHORE_BANDS') in ENUMS['shore_bands']:
        values['shore_bands'] = os.environ['ZH_WATER_CLEAN_SHORE_BANDS']
    for env, key in [('ZH_WATER_FFT_TIMESCALE', 'wave_speed'), ('ZH_WATER_CLEAN_WIND_ANGLE', 'wind_angle'),
                     ('ZH_WATER_CLEAN_TRAVEL_ANGLE', 'travel_angle'), ('ZH_WATER_CLEAN_SPREAD', 'spread'),
                     ('ZH_WATER_CLEAN_SHORE_MAX_TURN', 'shore_max_turn'),
                     ('ZH_WATER_CLEAN_HEADING_FOLLOW', 'heading_follow')]:
        try:
            value = float(os.environ[env])
            if env == 'ZH_WATER_FFT_TIMESCALE' and not 0 < value <= 4:
                continue
            values.update(validate({key: value}))
        except (KeyError, ValueError):
            pass
    return values


def default_path():
    if os.environ.get('ZH_WATER_TUNING_FILE'):
        return Path(os.environ['ZH_WATER_TUNING_FILE']).expanduser()
    data = os.environ.get('ZH_USER_DATA_DIR')
    if not data:
        base = Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share'))
        data = base / 'Command and Conquer Generals Zero Hour Data'
    return Path(data) / 'water-tuning.ini'


def atomic_write(path, content):
    """Publish a complete file, with a temporary file on the same filesystem."""
    path.parent.mkdir(parents=True, exist_ok=True)
    name = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=path.parent,
                                         prefix=f'.{path.name}.', delete=False) as stream:
            name = stream.name
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(name, path)
    finally:
        if name and os.path.exists(name):
            os.unlink(name)


def env_line(values):
    def joined(*keys):
        return ','.join(format(values[k], '.12g') for k in keys)
    return ' '.join([
        'ZH_WATER_CLEAN=1 ZH_WATER_TUNING=1',
        'ZH_WATER_CLEAN_SPECTRUM=' + joined('wind_speed', 'fetch', 'swell'),
        'ZH_WATER_CLEAN_LOOK=' + joined('crest_color', 'foam_threshold', 'glint'),
        'ZH_WATER_CLEAN_SPLIT=' + values['split'],
        'ZH_WATER_FFT_TIMESCALE=' + joined('wave_speed'),
        'ZH_WATER_CLEAN_TRAVEL_ANGLE=' + joined('travel_angle'),
        'ZH_WATER_CLEAN_TRAVEL_MODE=' + values['travel_mode'],
        'ZH_WATER_CLEAN_HEIGHTS=' + joined('height_long', 'height_medium', 'height_fine'),
        'ZH_WATER_CLEAN_CHOP=' + joined('chop_long', 'chop_short'),
        'ZH_WATER_CLEAN_DEPTHS=' + joined('depth_long', 'depth_medium', 'depth_fine'),
        'ZH_WATER_CLEAN_SEA2=' + joined('sea2_share', 'sea2_angle', 'sea2_wavelength', 'sea2_spread'),
        'ZH_WATER_CLEAN_SPREAD=' + joined('spread'),
        'ZH_WATER_CLEAN_HEADING_FOLLOW=' + joined('heading_follow'),
        'ZH_WATER_CLEAN_SHORE=' + joined('shore_turn', 'shore_reach', 'shore_tile', 'shore_sharp'),
        'ZH_WATER_CLEAN_SHORE_MAX_TURN=' + joined('shore_max_turn'),
        'ZH_WATER_CLEAN_SHORE_BANDS=' + values['shore_bands'],
    ])


class Tuner:
    def __init__(self, path):
        self.path = Path(path).resolve()
        self.presets_path = self.path.with_suffix('.presets.json')
        self.values = start_values()
        self.reported_shore_depth = False
        if not self.path.exists():
            self.write_values(self.values)

    def read_values(self):
        with self.path.open('rb') as stream:
            raw = stream.read(65537)
        if len(raw) > 65536 or b'\0' in raw:
            return dict(self.values)
        try:
            text = raw.decode('utf-8')
        except UnicodeDecodeError:
            return dict(self.values)
        saw_travel_mode = False
        saw_heading_follow = False
        # Same patch semantics as the game: complete, valid lines update previous values.
        for line in text.splitlines(keepends=True):
            if not line.endswith('\n'):
                continue
            line = line.strip()
            if not line or line.startswith(('#', ';')) or '=' not in line:
                continue
            key, value = (part.strip() for part in line.split('=', 1))
            if key == 'travel_mode':
                saw_travel_mode = True
            if key == 'heading_follow':
                saw_heading_follow = True
            if key == 'shore_depth':
                if not self.reported_shore_depth:
                    print('water tuning: shore_depth ignored; use shore_reach in game units', file=sys.stderr)
                    self.reported_shore_depth = True
                continue
            try:
                self.values.update(validate({key: value if key in ENUMS else float(value)}))
            except ValueError:
                pass
        if not saw_travel_mode:
            self.values['travel_mode'] = 'auto'  # old files select auto
        if not saw_heading_follow:
            self.values['heading_follow'] = 1  # legacy files follow the field
        return dict(self.values)

    def write_values(self, values):
        values = validate(values)
        text = ''.join(f'{key}={values[key] if key in ENUMS else format(values[key], ".12g")}\n'
                       for key in SPECS)
        atomic_write(self.path, text)
        self.values = dict(values)

    def presets(self):
        if not self.presets_path.exists():
            return {}
        with self.presets_path.open(encoding='utf-8') as stream:
            presets = json.load(stream)
        if not isinstance(presets, dict):
            raise ValueError('Preset file must contain an object')
        return presets

    def auto_direction(self):
        try:
            with Path(str(self.path) + '.auto').open('rb') as stream:
                raw = stream.read(4097)
            if len(raw) > 4096:
                return None
            result = json.loads(raw)
            angle, strength = result['travel_angle'], result['strength']
            if (isinstance(angle, bool) or not isinstance(angle, (int, float))
                    or isinstance(strength, bool) or not isinstance(strength, (int, float))
                    or not math.isfinite(angle) or not -180 <= angle <= 180
                    or not math.isfinite(strength) or not 0 <= strength <= 1
                    or not isinstance(result['dominant'], bool)):
                return None
            return result
        except (OSError, ValueError, KeyError, TypeError):
            return None

    def snapshot(self):
        values = self.read_values()
        return dict(values=values, auto_direction=self.auto_direction(), defaults=DEFAULTS, controls=CONTROLS,
                    presets=list(self.presets()), env=env_line(values), file=str(self.path))

    def post(self, path, body):
        if not isinstance(body, dict):
            raise ValueError('Request must be an object')
        if path == '/api/values':
            values = self.read_values()
            values.update(validate(body.get('values')))
            self.write_values(values)
        elif path in ('/api/presets/save', '/api/presets/load'):
            name = body.get('name')
            if not isinstance(name, str) or not 1 <= len(name.strip()) <= 64 or not name.isprintable():
                raise ValueError('Use a preset name of 1–64 printable characters')
            name = name.strip()
            presets = self.presets()
            if path.endswith('/save'):
                presets[name] = self.read_values()
                atomic_write(self.presets_path, json.dumps(presets, indent=2, allow_nan=False) + '\n')
            else:
                if name not in presets:
                    raise ValueError('Preset does not exist')
                values = self.read_values()
                values['travel_mode'] = 'auto'
                values['heading_follow'] = 1
                values.update(validate(presets[name]))
                self.write_values(values)
        else:
            raise ValueError('Unknown API endpoint')
        return self.snapshot()


HTML = r'''<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Clean water tuner</title>
<style>
.compass-wrap{display:flex;align-items:center;gap:12px}.compass{width:116px;height:116px;flex:none}.compass text{fill:#a2b7c0;font-size:10px}.compass-legend{font-size:11px;line-height:1.7}.main-sea{color:#65d6c5}.second-sea{color:#f6b86b}
:root{color-scheme:dark;font:13px system-ui,sans-serif;background:#111a20;color:#dfebee;accent-color:#65d6c5}
*{box-sizing:border-box}body{margin:0;padding:14px;max-width:540px;margin-inline:auto}h1{font-size:20px;margin:0 0 4px}p{color:#a2b7c0;margin:5px 0 12px;line-height:1.4}header{border-bottom:1px solid #33464f;padding-bottom:12px}button,input,select,textarea{font:inherit;border:1px solid #425b65;border-radius:5px;background:#1c2c34;color:inherit;padding:6px}button{cursor:pointer}button:hover{background:#304950}button:disabled{opacity:.5;cursor:wait}button:focus-visible,input:focus-visible,select:focus-visible,textarea:focus-visible{outline:2px solid #65d6c5;outline-offset:2px}section{border-bottom:1px solid #33464f;padding:12px 0 9px}.group-title{display:flex;align-items:center;justify-content:space-between;margin-bottom:8px}h2{font-size:14px;margin:0;color:#8ee0d5}.reset{font-size:11px;padding:4px 7px}.control{margin:8px 0 12px}.row{display:grid;grid-template-columns:100px minmax(60px,1fr) 72px;gap:7px;align-items:center}.row label{font-weight:550;min-width:0;overflow-wrap:anywhere}input[type=range]{width:100%;padding:0;min-width:0}input[type=number]{width:72px;padding:4px}select{min-width:0}.hint{font-size:11px;color:#a2b7c0;line-height:1.35;margin-top:4px}.presets{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:7px;margin-top:9px}.presets input,.presets select{width:100%}textarea{width:100%;resize:vertical;min-height:95px;font:11px ui-monospace,monospace;line-height:1.5;margin-top:8px}#status{font-size:12px;color:#8ee0d5;min-height:32px;white-space:pre-wrap;overflow-wrap:anywhere;margin:8px 0}#status.error{color:#ffb2a7}footer{margin-top:12px;font-size:11px;color:#a2b7c0;overflow-wrap:anywhere}.wide{width:100%}
</style>
<header><h1>Clean water tuner</h1><p>Live FFT water · edits reach the game in about a second.</p><button id="reset-all" class="wide">Reset all to defaults</button></header>
<div id="status" role="status" aria-live="polite">Connecting…</div><main id="groups"></main>
<section><h2>Presets</h2><div class="presets"><input id="preset-name" maxlength="64" placeholder="Preset name" aria-label="Preset name"><button id="save">Save preset</button><select id="preset-list" aria-label="Saved presets"></select><button id="load">Load preset</button></div></section>
<section><h2>Copy these settings</h2><p class="hint">Send this line back for feedback. New files can also start from these environment values.</p><textarea id="env" readonly aria-label="Current settings as an environment line" spellcheck="false"></textarea></section>
<footer id="file"></footer>
<script>
'use strict';
let state, pending={}, busy=false, actionBusy=false, lastSend=0;
const fields={}, status=document.querySelector('#status');
function compassPoint(degrees){const angle=degrees*Math.PI/180;return [50+32*Math.cos(angle),50-32*Math.sin(angle)]}
function updateCompass(){const compass=document.querySelector('#sea-compass');if(!compass)return;const values={...state.values,...pending},auto=values.travel_mode==='auto',map=state.auto_direction,angle=auto&&map?.dominant?map.travel_angle:values.travel_angle;for(const [id,degrees]of [['main-sea-arrow',angle],['second-sea-arrow',values.sea2_angle]]){const [x,y]=compassPoint(degrees),arrow=document.querySelector('#'+id);arrow.setAttribute('x2',x);arrow.setAttribute('y2',y)}for(const input of fields.travel_angle||[]){input.value=auto?Number(angle.toFixed(2)):values.travel_angle;input.disabled=auto&&input.type==='range';input.readOnly=auto}document.querySelector('#main-sea-legend').textContent='Main sea travel · '+(auto?'auto':'manual');const mixed=values.sea2_share>0;document.querySelector('#second-sea-arrow').style.display=mixed?'':'none';document.querySelector('#second-sea-legend').hidden=!mixed;compass.setAttribute('aria-label','Map wave travel: main sea '+angle+' degrees ('+(auto?'auto':'manual')+')'+(mixed?', second sea '+values.sea2_angle+' degrees':'')+'. Map +x right, +y up.')}
function message(text,error=false){status.textContent=text;status.classList.toggle('error',error)}
async function api(path,body){const response=await fetch(path,body===undefined?{cache:'no-store'}:{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});const data=await response.json();if(!response.ok)throw Error(data.error||'Request failed');return data}
function render(data){state=data;for(const [key,pair]of Object.entries(fields)){if(key in pending)continue;for(const input of pair)input.value=data.values[key]}document.querySelector('#env').value=data.env;const list=document.querySelector('#preset-list'),selected=list.value;list.replaceChildren();for(const name of data.presets){const option=document.createElement('option');option.textContent=name;option.value=name;list.append(option)}if(data.presets.includes(selected))list.value=selected;document.querySelector('#load').disabled=!data.presets.length;document.querySelector('#file').textContent=data.file;updateCompass()}
function edit(key,value){if(actionBusy)return;state.values[key]=value;pending[key]=value;for(const field of fields[key])field.value=value;updateCompass();message('Sending changes…')}
function build(data){const root=document.querySelector('#groups');for(const group of ['Waves','Mixed seas','Strength','Shallow water','Shore','Look']){const section=document.createElement('section'),heading=document.createElement('div'),title=document.createElement('h2'),reset=document.createElement('button');heading.className='group-title';title.textContent=group;reset.textContent='Reset group';reset.className='reset';reset.setAttribute('aria-label','Reset '+group);reset.onclick=()=>{for(const row of data.controls.filter(r=>r[6]===group))edit(row[0],state.defaults[row[0]])};heading.append(title,reset);section.append(heading);for(const [key,label,min,max,initial,step,g,hint]of data.controls.filter(r=>r[6]===group)){const control=document.createElement('div'),row=document.createElement('div'),name=document.createElement('label'),help=document.createElement('div');control.className='control';row.className='row';name.textContent=label;name.htmlFor=key;help.className='hint';help.id=key+'-hint';help.textContent=hint;row.append(name);if(key==='split'||key==='shore_bands'||key==='travel_mode'){const input=document.createElement('select');input.id=key;input.style.gridColumn='span 2';input.setAttribute('aria-describedby',help.id);for(const value of (key==='split'?['fixed','spectrum']:key==='travel_mode'?['auto','manual']:['medium','long_medium','all'])){const option=document.createElement('option');option.value=value;option.textContent=(value[0].toUpperCase()+value.slice(1)).replace('_',' + ');input.append(option)}input.onchange=()=>edit(key,input.value);fields[key]=[input];row.append(input)}else{fields[key]=[];for(const type of ['range','number']){const input=document.createElement('input');input.type=type;input.min=min;input.max=max;input.step=step;input.id=key+(type==='number'?'-number':'');input.setAttribute('aria-label',label+(type==='number'?' value':''));input.setAttribute('aria-describedby',help.id);input.oninput=()=>{const value=input.valueAsNumber;if(Number.isFinite(value))edit(key,Math.max(min,Math.min(max,value)))};input.onchange=()=>{if(!Number.isFinite(input.valueAsNumber))input.value=state.values[key]};fields[key].push(input);row.append(input)}}control.append(row,help);if(key==='travel_angle'){const view=document.createElement('div');view.className='compass-wrap';view.innerHTML=`<svg id="sea-compass" class="compass" viewBox="0 0 100 100" role="img" aria-label="Map wave travel"><defs><marker id="main-arrow-tip" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="5" markerHeight="5" orient="auto"><path d="M0 0 L10 5 L0 10 Z" fill="#65d6c5"/></marker><marker id="second-arrow-tip" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="5" markerHeight="5" orient="auto"><path d="M0 0 L10 5 L0 10 Z" fill="#f6b86b"/></marker></defs><circle cx="50" cy="50" r="34" fill="none" stroke="#425b65"/><path d="M16 50 H84 M50 16 V84" stroke="#33464f"/><text x="50" y="10" text-anchor="middle">+y</text><text x="50" y="98" text-anchor="middle">−y</text><text x="2" y="47">−x</text><text x="85" y="47">+x</text><line id="main-sea-arrow" x1="50" y1="50" x2="50" y2="50" stroke="#65d6c5" stroke-width="2.5" marker-end="url(#main-arrow-tip)"/><line id="second-sea-arrow" x1="50" y1="50" x2="50" y2="50" stroke="#f6b86b" stroke-width="2" marker-end="url(#second-arrow-tip)" style="display:none"/></svg><div class="compass-legend"><div id="main-sea-legend" class="main-sea">Main sea travel · auto</div><div id="second-sea-legend" class="second-sea" hidden>Second sea travel</div><div>Map +x right · +y up</div></div>`;control.append(view)}section.append(control)}root.append(section)}}
async function flush(){if(busy||!Object.keys(pending).length)return;busy=true;lastSend=Date.now();const changes=pending;pending={};try{const data=await api('/api/values',{values:changes});render(data);message(Object.keys(pending).length?'Sending changes…':'Saved · game checks twice per second')}catch(error){pending={...changes,...pending};message(error.message+' · retrying',true)}finally{busy=false}}
async function action(path,body){if(actionBusy)return;actionBusy=true;document.querySelectorAll('main input,main select,button').forEach(el=>el.disabled=true);try{while(busy)await new Promise(r=>setTimeout(r,25));if(Object.keys(pending).length){await flush();if(Object.keys(pending).length)throw Error('Pending edits could not be saved')}render(await api(path,body));message('Preset '+(path.endsWith('save')?'saved':'loaded'))}catch(error){message(error.message,true)}finally{actionBusy=false;document.querySelectorAll('main input,main select,button').forEach(el=>el.disabled=false);document.querySelector('#load').disabled=!state.presets.length;updateCompass()}}
document.querySelector('#reset-all').onclick=()=>{for(const [key,value]of Object.entries(state.defaults))edit(key,value)};
document.querySelector('#save').onclick=()=>action('/api/presets/save',{name:document.querySelector('#preset-name').value});
document.querySelector('#load').onclick=()=>action('/api/presets/load',{name:document.querySelector('#preset-list').value});
document.querySelector('#env').onclick=event=>event.target.select();
setInterval(()=>{if(!actionBusy&&Date.now()-lastSend>=200)flush()},50);
// Refresh external edits without overwriting a drag or a queued request.
setInterval(async()=>{if(!state||busy||actionBusy||Object.keys(pending).length||document.activeElement?.matches('main input,main select'))return;try{const data=await api('/api/values');if(!busy&&!actionBusy&&!Object.keys(pending).length)render(data)}catch(error){message(error.message,true)}},1500);
api('/api/values').then(data=>{build(data);render(data);message('Ready · drag a slider to tune')}).catch(error=>message(error.message,true));
</script></html>'''


class Server(HTTPServer):
    def __init__(self, address, tuner):
        self.tuner = tuner
        super().__init__(address, Handler)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def reply(self, status, value, html=False):
        content = value.encode('utf-8') if html else json.dumps(value, allow_nan=False).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', 'text/html; charset=utf-8' if html else 'application/json')
        self.send_header('Content-Length', str(len(content)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.end_headers()
        self.wfile.write(content)

    def allowed(self):
        # Block cross-origin writes and DNS rebinding while remaining usable from local scripts.
        port = self.server.server_port
        origins = {f'http://127.0.0.1:{port}', f'http://localhost:{port}'}
        return (self.headers.get('Host') in {f'127.0.0.1:{port}', f'localhost:{port}'}
                and self.headers.get('Origin', f'http://127.0.0.1:{port}') in origins)

    def do_GET(self):
        if not self.allowed():
            return self.reply(403, {'error': 'Use the local tuner address'})
        if self.path == '/':
            return self.reply(200, HTML, html=True)
        if self.path != '/api/values':
            return self.reply(404, {'error': 'Not found'})
        try:
            self.reply(200, self.server.tuner.snapshot())
        except (OSError, ValueError) as error:
            self.reply(400, {'error': str(error)})

    def do_POST(self):
        if not self.allowed():
            return self.reply(403, {'error': 'Use the local tuner address'})
        try:
            size = int(self.headers.get('Content-Length', '0'))
            if not 0 < size <= 65536:
                raise ValueError('Request must be between 1 and 65536 bytes')
            if self.headers.get_content_type() != 'application/json':
                raise ValueError('Use application/json')
            self.connection.settimeout(5)
            body = json.loads(self.rfile.read(size))
            self.reply(200, self.server.tuner.post(self.path, body))
        except (OSError, ValueError, TypeError) as error:
            self.reply(400, {'error': str(error)})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--file', type=Path, default=default_path())
    parser.add_argument('--port', type=int, default=8765)
    args = parser.parse_args()
    try:
        with Server(('127.0.0.1', args.port), Tuner(args.file)) as server:
            print(f'Water tuner: http://127.0.0.1:{server.server_port}\nFile: {server.tuner.path}', flush=True)
            server.serve_forever()
    except KeyboardInterrupt:
        pass
    except (OSError, ValueError) as error:
        parser.exit(1, f'water tuner: {error}\n')


if __name__ == '__main__':
    main()
