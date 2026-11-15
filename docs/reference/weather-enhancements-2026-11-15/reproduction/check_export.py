#!/usr/bin/env python3
"""Portable verification of archived weather evidence; no game, retail data or GPU needed."""
import csv, hashlib, json, math, statistics
from pathlib import Path

root = Path(__file__).resolve().parents[1]
evidence = root / 'evidence'
def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def load(p): return json.loads(p.read_text())

inventory = load(root / 'evidence-inventory.json')
for name, record in inventory.items():
    p = root / name
    assert p.stat().st_size == record['size'] and digest(p) == record['sha256'], name
admission = load(evidence / 'benchmark-admission.json')
summary = load(evidence / 'summary/phase-results.json')
assert digest(evidence / 'benchmark-admission.json') == summary['admission_sha256']
assert digest(root / 'reproduction/run_weather.py') == admission['runner_sha256']
assert digest(root / 'reproduction/generate_map.py') == admission['generator_sha256']
assert digest(root / 'reproduction/rain_series.cpp') == admission['phase_tool_source_sha256']
header = evidence / 'source-snapshot/W3DDevice/GameClient/WeatherParticleMath.h'
assert digest(header) == admission['phase_source_sha256']
phase_rows = list(csv.DictReader((evidence / 'summary/rain-phase-series.csv').open()))
phases = {int(row['logic_frame']): row['phase'] for row in phase_rows}
frames = {}; seen = set(); total = 0
for directory in sorted((evidence / 'primary-runs').iterdir()):
    m = load(directory / 'manifest.json'); case = admission['cases'][m['kind']]
    key = (m['kind'], m['label'], m['repeat']); assert key not in seen; seen.add(key)
    assert m['accepted'] and m['crc_admitted'] and m['returncode'] == 0
    assert m['admission_sha256'] == summary['admission_sha256']
    assert m['candidate_sha256'] == m['candidate_after_sha256'] == admission['artifacts'][m['label']]['sha256']
    assert digest(directory / 'timing.csv') == m['timing_csv_sha256']
    assert digest(directory / 'engine.log') == m['engine_log_provenance']['sha256']
    assert digest(directory / 'user/Options.ini') == m['profile_sha256']
    for k in ('map_sha256','scenario_sha256','profile_common_sha256','first','last','width','height','audio','idle_ai'):
        assert m[k] == case[k], (directory.name, k)
    rows = list(csv.DictReader((directory / 'timing.csv').open()))
    assert [int(row['logic_frame']) for row in rows] == list(range(m['first'], m['last'] + 1))
    frames[directory.name] = rows; total += len(rows)
expected = {(kind, label, repeat) for kind in admission['cases'] for label in admission['artifacts'] for repeat in (1,2,3)}
assert seen == expected and total == 253800
names = {'plateau':'wet','fade-in':'fade_in','fade-out':'fade_out','dry':'dry'}
for record in summary['runs']:
    rows = frames[record['run']]; phase = record['phase']
    assert record['artifact_label'] == record['label']
    if phase == 'active-30s': subset = [row for row in rows if 601 <= int(row['logic_frame']) <= 2400]
    elif phase == 'complete-cycle': subset = [row for row in rows if 210 <= ((int(row['logic_frame'])-1)*1000//60)/1000 < 420]
    else: subset = [row for row in rows if phases[int(row['logic_frame'])] == names[phase]]
    for metric in ('frame_ms', 'work_ms', 'draws'):
        values = sorted(float(row[metric]) for row in subset)
        assert all(math.isfinite(value) and value >= 0 for value in values)
        actual = dict(samples=len(values), median=statistics.median(values), p95=values[math.ceil(.95*len(values))-1],
                      mean=statistics.mean(values), minimum=values[0], maximum=values[-1])
        assert actual == record[metric], (record['run'], phase, metric)
print(f'PASS: {len(inventory)} archived file hashes, 27 paired runs, {total} rows, {len(summary["runs"])} exact phase distributions')
