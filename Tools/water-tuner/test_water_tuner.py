#!/usr/bin/env python3
"""GPU-free integration checks: python3 Tools/water-tuner/test_water_tuner.py."""
import io
import json
import os
import re
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
from urllib.error import HTTPError
from urllib.request import Request, urlopen

import water_tuner


class TunerTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name) / 'water-tuning.ini'
        self.tuner = water_tuner.Tuner(self.path)
        self.url = 'http://127.0.0.1:8765'

    def tearDown(self):
        self.temp.cleanup()

    def raw_request(self, path='/', body=None, **headers):
        payload = b'' if body is None else json.dumps(body).encode()
        method = 'GET' if body is None else 'POST'
        headers = {'Host': '127.0.0.1:8765', 'Content-Type': 'application/json',
                   'Content-Length': str(len(payload)), **headers}
        request = f'{method} {path} HTTP/1.1\r\n' + ''.join(f'{k}: {v}\r\n' for k, v in headers.items()) + '\r\n'
        handler = water_tuner.Handler.__new__(water_tuner.Handler)
        handler.rfile = io.BytesIO(request.encode() + payload)
        handler.wfile = io.BytesIO()
        handler.connection = SimpleNamespace(settimeout=lambda _timeout: None)
        handler.client_address = ('127.0.0.1', 1)
        handler.server = SimpleNamespace(tuner=self.tuner, server_port=8765)
        handler.handle_one_request()
        head, content = handler.wfile.getvalue().split(b'\r\n\r\n', 1)
        code = int(head.split(b' ', 2)[1])
        if code >= 400:
            raise HTTPError(self.url + path, code, content.decode(), None, None)
        return content

    def request(self, path='/api/values', body=None, **headers):
        return json.loads(self.raw_request(path, body, **headers))

    def test_values_ranges_partial_files_and_atomic_writes(self):
        self.assertEqual(self.request()['values'], water_tuner.DEFAULTS)
        page = self.raw_request().decode()
        self.assertIn('Save preset', page)
        old = self.path.open()
        old_content = old.read()
        initial_inode = self.path.stat().st_ino
        snapshot = self.request(body={'values': {'wind_speed': 100, 'fetch': -1, 'swell': 1.2, 'split': 'spectrum'}})
        self.assertEqual(snapshot['values']['wind_speed'], 30)
        self.assertEqual(snapshot['values']['fetch'], 1000)
        self.assertEqual(snapshot['values']['swell'], 1.2)
        self.assertEqual(snapshot['values']['height_long'], 1.5)
        self.assertEqual(snapshot['values']['split'], 'spectrum')
        self.assertNotEqual(initial_inode, self.path.stat().st_ino)
        old.seek(0)
        self.assertEqual(old.read(), old_content)  # Open reader retains the previous complete file.
        old.close()
        self.assertTrue(self.path.read_text().endswith('\n'))
        self.assertEqual(len(self.path.read_text().splitlines()), len(water_tuner.SPECS))
        self.assertEqual(list(self.path.parent.glob('.*water-tuning.ini.*')), [])
        for key, row in water_tuner.SPECS.items():
            if key in water_tuner.ENUMS:
                continue
            self.assertEqual(self.request(body={'values': {key: -999999}})['values'][key], row[2])
            self.assertEqual(self.request(body={'values': {key: 999999}})['values'][key], row[3])
        self.path.write_text('wind_speed=11\nfetch=garbage\nswell=0.')
        values = self.request()['values']
        self.assertEqual(values['wind_speed'], 11)
        self.assertEqual(values['swell'], 2)  # Truncated line ignored, previous retained.
        self.assertEqual(values['fetch'], 200000)
        self.path.write_bytes(b'wind_speed=9\n\0fetch=3\n')
        self.assertEqual(self.request()['values']['wind_speed'], 11)

    def test_approved_defaults_and_resets(self):
        fixture = Path(__file__).resolve().parents[2] / 'GeneralsMD/Code/Tests/water_clean_approved.ini'
        expected = {}
        for line in fixture.read_text().splitlines():
            key, value = line.split('=', 1)
            expected[key] = value if key in water_tuner.ENUMS else float(value)
        self.assertEqual(water_tuner.DEFAULTS, expected)
        with patch.dict(os.environ, {}, clear=True):
            self.assertEqual(water_tuner.start_values(), expected)
        for split in ('fixed', 'spectrum'):
            with patch.dict(os.environ, {'ZH_WATER_CLEAN_SPLIT': split}, clear=True):
                self.assertEqual(water_tuner.start_values()['split'], split)
        snapshot = self.request()
        self.assertEqual(snapshot['defaults'], expected)
        self.request(body={'values': {'wind_speed': 20, 'shore_turn': 0, 'split': 'fixed'}})
        self.assertEqual(self.request(body={'values': snapshot['defaults']})['values'], expected)
        # Both UI reset paths consume this same defaults object.
        self.assertIn('Object.entries(state.defaults)', water_tuner.HTML)
        self.assertIn('edit(row[0],state.defaults[row[0]])', water_tuner.HTML)
        for group in {row[6] for row in water_tuner.CONTROLS}:
            rows = [row for row in water_tuner.CONTROLS if row[6] == group]
            changed = {row[0]: (water_tuner.ENUMS[row[0]][0] if row[0] in water_tuner.ENUMS else row[2]) for row in rows}
            self.request(body={'values': changed})
            reset = {row[0]: snapshot['defaults'][row[0]] for row in rows}
            self.assertEqual(self.request(body={'values': reset})['values'], expected)

    def test_reject_invalid_input_without_writing(self):
        before = self.path.read_bytes()
        for body in [{'values': {'swell': float('nan')}}, {'values': {'swell': float('inf')}},
                     {'values': {'swell': True}}, {'values': {'swell': '1'}},
                     {'values': {'split': 'other'}}, {'values': {'travel_mode': 'other'}}, {'values': {'shore_bands': 'other'}},
                     {'values': {'shore_bands': 1}}, {'values': {'mystery': 1}},
                     {'values': {'swell': 1.2, 'glint': None}}, [], {'values': []}]:
            with self.assertRaises(HTTPError) as raised:
                self.request(body=body)
            self.assertEqual(raised.exception.code, 400)
            raised.exception.close()
            self.assertEqual(self.path.read_bytes(), before)
        with self.assertRaises(HTTPError) as raised:
            self.request(body={'values': {'swell': 1}}, Origin='https://example.com')
        self.assertEqual(raised.exception.code, 403)
        raised.exception.close()
        self.assertEqual(self.path.read_bytes(), before)

    def test_presets_and_copyable_environment(self):
        self.request(body={'values': {'travel_angle': -120, 'depth_long': 32, 'chop_short': 2.2,
                                            'sea2_share': .6, 'sea2_angle': 170, 'sea2_wavelength': 750,
                                            'sea2_spread': 21, 'spread': .3, 'shore_turn': .7,
                                            'shore_reach': 1200, 'shore_tile': 1500, 'shore_sharp': 5,
                                            'shore_bands': 'all', 'shore_max_turn': 35, 'heading_follow': .4}})
        snapshot = self.request('/api/presets/save', {'name': 'Wide crests'})
        self.assertEqual(snapshot['presets'], ['Wide crests'])
        presets_file = self.path.with_suffix('.presets.json')
        self.assertEqual(json.loads(presets_file.read_text())['Wide crests']['depth_long'], 32)
        self.request(body={'values': {'depth_long': 12}})
        snapshot = self.request('/api/presets/load', {'name': 'Wide crests'})
        self.assertEqual(snapshot['values']['depth_long'], 32)
        env = dict(part.split('=', 1) for part in snapshot['env'].split())
        with patch.dict(os.environ, env, clear=True):
            self.assertEqual(water_tuner.start_values(), snapshot['values'])
        self.assertFalse('\n' in snapshot['env'])
        self.assertEqual(env['ZH_WATER_CLEAN_SHORE'], '0.7,1200,1500,5')
        self.assertEqual(env['ZH_WATER_CLEAN_SHORE_MAX_TURN'], '35')
        self.assertEqual(env['ZH_WATER_CLEAN_HEADING_FOLLOW'], '0.4')
        self.assertEqual(env['ZH_WATER_CLEAN_TRAVEL_ANGLE'], '-120')
        self.assertNotIn('ZH_WATER_CLEAN_WIND_ANGLE', env)
        self.assertEqual(env['ZH_WATER_CLEAN_SHORE_BANDS'], 'all')
        self.assertNotIn('shore_depth', self.path.read_text())

    def test_shore_band_choices_and_defaults(self):
        with patch.dict(os.environ, {}, clear=True):
            self.assertEqual(water_tuner.start_values()['shore_bands'], 'all')
        self.assertEqual(self.request()['values']['shore_bands'], 'all')
        self.assertIn("['medium','long_medium','all']", water_tuner.HTML)
        for band in ('medium', 'long_medium', 'all'):
            with self.subTest(band=band):
                snapshot = self.request(body={'values': {'shore_bands': band}})
                self.assertEqual(snapshot['values']['shore_bands'], band)
                self.assertIn(f'shore_bands={band}\n', self.path.read_text())
                self.assertIn(f'ZH_WATER_CLEAN_SHORE_BANDS={band}', snapshot['env'])
                with patch.dict(os.environ, {'ZH_WATER_CLEAN_SHORE_BANDS': band}, clear=True):
                    self.assertEqual(water_tuner.start_values()['shore_bands'], band)
                self.assertEqual(water_tuner.Tuner(self.path).read_values()['shore_bands'], band)

    def test_old_files_presets_and_groups(self):
        self.request(body={'values': {'heading_follow': 0}})
        self.path.write_text('wind_speed=11\nsplit=spectrum\nshore_depth=20\nshore_depth=garbage\n')
        with patch('sys.stderr', new_callable=io.StringIO) as log:
            values = self.tuner.read_values()
            self.tuner.read_values()
            self.assertEqual(log.getvalue().count('shore_depth ignored'), 1)
        self.assertEqual(values['shore_reach'], 1500)
        self.assertEqual(values['heading_follow'], 1)
        self.assertNotIn('shore_depth', values)
        for key in ('sea2_share', 'spread', 'shore_turn'):
            self.assertEqual(values[key], water_tuner.DEFAULTS[key])
        self.assertEqual(values['shore_bands'], 'all')
        self.tuner.presets_path.write_text(json.dumps({'Old': {'wind_speed': 14}}))
        values = self.tuner.post('/api/presets/load', {'name': 'Old'})['values']
        self.assertEqual(values['wind_speed'], 14)
        self.assertEqual(values['sea2_share'], .33)
        self.assertEqual(values['heading_follow'], 1)
        self.assertEqual(water_tuner.SPECS['heading_follow'][1], 'Heading')
        self.assertIn('extra', water_tuner.SPECS['shore_turn'][7])
        for group in ('Mixed seas', 'Shore'):
            self.assertIn(group, water_tuner.HTML)
            self.assertTrue(any(row[6] == group for row in water_tuner.CONTROLS))

    def test_travel_compatibility_defaults_and_api(self):
        snapshot = self.request()
        self.assertEqual(snapshot['values']['travel_angle'], -135)  # same as 225°
        self.assertEqual(snapshot['values']['shore_max_turn'], 60)
        self.assertEqual(snapshot['values']['shore_reach'], 1500)
        self.assertEqual(snapshot['controls'][0][0:2], ['travel_angle', 'Sea direction'])
        self.assertNotIn('wind_angle', snapshot['values'])
        for wind in (-180, -135, 0, 45, 135, 180):
            self.path.write_text(f'wind_angle={wind}\n')
            expected = (wind + 360) % 360 - 180
            self.assertEqual(self.request()['values']['travel_angle'], expected)
            with patch.dict(os.environ, {'ZH_WATER_CLEAN_WIND_ANGLE': str(wind)}, clear=True):
                self.assertEqual(water_tuner.start_values()['travel_angle'], expected)
        self.path.write_text('wind_angle=45\ntravel_angle=90\nshore_max_turn=25\n')
        self.assertEqual(self.request()['values']['travel_angle'], 90)
        self.path.write_text('travel_angle=90\nwind_angle=45\n')
        self.assertEqual(self.request()['values']['travel_angle'], -135)
        values = self.request(body={'values': {'travel_angle': 70, 'shore_max_turn': 35}})['values']
        self.assertEqual(values['travel_angle'], 70)
        self.assertEqual(values['shore_max_turn'], 35)
        self.assertIn('travel_angle=70\n', self.path.read_text())
        self.assertNotIn('wind_angle=', self.path.read_text())
        self.tuner.presets_path.write_text(json.dumps({'Legacy': {'wind_angle': 45}}))
        self.assertEqual(self.tuner.post('/api/presets/load', {'name': 'Legacy'})['values']['travel_angle'], -135)
        with patch.dict(os.environ, {'ZH_WATER_CLEAN_WIND_ANGLE': '45',
                                     'ZH_WATER_CLEAN_TRAVEL_ANGLE': '90',
                                     'ZH_WATER_CLEAN_SHORE_MAX_TURN': '35'}, clear=True):
            seeded = water_tuner.start_values()
            self.assertEqual(seeded['travel_angle'], 90)
            self.assertEqual(seeded['shore_max_turn'], 35)

    def test_auto_mode_display_metadata_and_compatibility(self):
        snapshot = self.request()
        self.assertEqual(snapshot['values']['travel_mode'], 'auto')
        self.assertIsNone(snapshot['auto_direction'])
        self.assertIn('ZH_WATER_CLEAN_TRAVEL_MODE=auto', snapshot['env'])
        metadata = Path(str(self.path) + '.auto')
        expected = dict(travel_angle=90, strength=.8, dominant=True)
        metadata.write_text(json.dumps(expected))
        snapshot = self.request()
        self.assertEqual(snapshot['auto_direction'], expected)
        self.assertEqual(snapshot['values']['travel_angle'], -135)  # saved manual angle retained
        self.request(body={'values': {'travel_mode': 'manual', 'travel_angle': 35}})
        self.assertEqual(self.request()['auto_direction'], expected)  # read-only metadata never overwritten
        self.path.write_text('travel_angle=50\n')
        self.assertEqual(self.request()['values']['travel_mode'], 'auto')  # legacy file
        self.request(body={'values': {'travel_mode': 'manual'}})
        self.tuner.presets_path.write_text(json.dumps({'Old': {'travel_angle': 40}}))
        self.assertEqual(self.tuner.post('/api/presets/load', {'name': 'Old'})['values']['travel_mode'], 'auto')
        for content in ('{', '[]', '{"travel_angle":NaN,"strength":0,"dominant":true}',
                        '{"travel_angle":90,"strength":2,"dominant":true}', 'x' * 4097):
            metadata.write_text(content)
            self.assertIsNone(self.request()['auto_direction'])
        with patch.dict(os.environ, {'ZH_WATER_CLEAN_TRAVEL_MODE': 'manual'}, clear=True):
            self.assertEqual(water_tuner.start_values()['travel_mode'], 'manual')

    def test_compass_live_directions(self):
        page = self.raw_request().decode()
        self.assertIn('<svg id="sea-compass"', page)
        self.assertIn('Map +x right · +y up', page)
        self.assertIn('updateCompass();message', page)  # updates immediately on edits
        self.assertIn('data.file;updateCompass()', page)  # also updates on API refresh
        node = shutil.which('node')
        if not node:
            self.skipTest('Node unavailable; compass markup and API checks passed')
        functions = '\n'.join(re.findall(r'^function (?:compassPoint|updateCompass)\(.*$', page, re.M))
        script = r'''
const assert=require('node:assert/strict');
const elements=Object.fromEntries(['sea-compass','main-sea-arrow','second-sea-arrow','second-sea-legend','main-sea-legend'].map(id=>[id,{style:{},setAttribute(k,v){this[k]=v}}]));
const document={querySelector(selector){return elements[selector.slice(1)]}};
let state={values:{travel_mode:'manual',travel_angle:0,sea2_angle:90,sea2_share:0}},pending={};
const fields={travel_angle:[{type:'range'},{type:'number'}]};
FUNCTIONS
function near(actual,expected){assert.ok(Math.abs(actual-expected)<1e-9)}
for(const [angle,x,y]of [[0,82,50],[90,50,18],[-90,50,82],[180,18,50]]){const p=compassPoint(angle);near(p[0],x);near(p[1],y)}
updateCompass();near(elements['main-sea-arrow'].x2,82);near(elements['main-sea-arrow'].y2,50);
assert.equal(elements['second-sea-arrow'].style.display,'none');assert.equal(elements['second-sea-legend'].hidden,true);
pending={travel_angle:90,sea2_share:.3,sea2_angle:-90};updateCompass();
near(elements['main-sea-arrow'].y2,18);near(elements['second-sea-arrow'].y2,82);
assert.equal(elements['second-sea-arrow'].style.display,'');assert.equal(elements['second-sea-legend'].hidden,false);
assert.ok(elements['sea-compass']['aria-label'].includes('second sea -90 degrees'));
pending={};state.values.travel_angle=-135;updateCompass();
assert.ok(elements['main-sea-arrow'].x2<50 && elements['main-sea-arrow'].y2>50); // default 225° points down-left
state.auto_direction={travel_angle:90,strength:.8,dominant:true};
pending={travel_mode:'auto'};updateCompass();
near(elements['main-sea-arrow'].y2,18);assert.equal(fields.travel_angle[0].disabled,true);
assert.equal(fields.travel_angle[1].readOnly,true);assert.equal(fields.travel_angle[1].value,90);
assert.equal(elements['main-sea-legend'].textContent,'Main sea travel · auto');
state.auto_direction.dominant=false;updateCompass();assert.equal(fields.travel_angle[1].value,-135);
pending={travel_mode:'manual'};updateCompass();assert.equal(fields.travel_angle[0].disabled,false);
assert.equal(fields.travel_angle[1].readOnly,false);assert.equal(fields.travel_angle[1].value,-135);
'''.replace('FUNCTIONS', functions)
        subprocess.run([node, '-e', script], check=True, capture_output=True, text=True)

    def test_atomic_failure_keeps_original(self):
        before = self.path.read_bytes()
        with patch('water_tuner.os.replace', side_effect=OSError('injected rename failure')):
            with self.assertRaises(OSError):
                water_tuner.atomic_write(self.path, 'bad content')
        self.assertEqual(self.path.read_bytes(), before)
        self.assertEqual(list(self.path.parent.glob('.*water-tuning.ini.*')), [])


class LiveServerTest(unittest.TestCase):
    def test_start_get_post(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'water-tuning.ini'
            process = subprocess.Popen([sys.executable, str(Path(water_tuner.__file__)),
                                        '--file', str(path), '--port', '0'],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                line = process.stdout.readline().strip()
                if not line:
                    error = process.stderr.read()
                    if 'Operation not permitted' in error or 'Permission denied' in error:
                        self.skipTest('Sandbox disallows loopback sockets; handler/file checks run above')
                    self.fail(error)
                url = line.split(' ', 2)[-1]
                self.assertTrue(url.startswith('http://127.0.0.1:'))
                with urlopen(url + '/api/values', timeout=5) as response:
                    snapshot = json.load(response)
                self.assertEqual(len(snapshot['values']), len(water_tuner.SPECS))
                before = path.stat().st_ino
                request = Request(url + '/api/values', data=b'{"values":{"wind_speed":22}}',
                                  headers={'Content-Type': 'application/json'})
                with urlopen(request, timeout=5) as response:
                    self.assertEqual(json.load(response)['values']['wind_speed'], 22)
                self.assertNotEqual(before, path.stat().st_ino)
                self.assertIn('wind_speed=22\n', path.read_text())
            finally:
                process.terminate()
                process.communicate(timeout=5)


if __name__ == '__main__':
    unittest.main()
