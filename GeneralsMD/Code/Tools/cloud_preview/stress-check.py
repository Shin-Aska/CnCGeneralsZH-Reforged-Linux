#!/usr/bin/env python3
"""Run the isolated shared-cloud prototype with private profiles and retained evidence.

Uses an existing executable and game-data root; installs nothing. GPU runs are
sequential. Counts and lifecycle checks are automatic; screenshots need visual
review. Timing mode measures the whole engine, not isolated GPU cloud passes.
"""
import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess
import time

REPORT = re.compile(r'^cloud-stress: requested=(.+)$', re.M)
ERROR = re.compile(r'Validation Error|VUID-|cloud-stress:.*FAIL|shared cloud shader.*(?:ERROR|failed)', re.I)
PALETTES = ['mixed', 'toxin', 'anthrax', 'gamma', 'nuclear']


def digest(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def records(log):
    rows = []
    for match in REPORT.finditer(log):
        row = dict(re.findall(r'([\w-]+)=([^ ]+)', 'requested=' + match[1]))
        for key in ('requested', 'retained', 'active', 'submitted', 'peak-active', 'peak-submitted',
                    'logic-frame', 'render-frames', 'memory-bytes', 'volume-bytes', 'bakes', 'maximum-bin-sources',
                    'baked-sources', 'pending-slices', 'max-slices-per-frame'):
            if key in row:
                row[key] = int(row[key])
        for key in ('seconds', 'wall-seconds', 'fps', 'publication-age'):
            if key in row:
                row[key] = float(row[key])
        rows.append(row)
    return rows


def read_timing(path):
    """Reject corrupt timing evidence before computing any frame statistics."""
    with path.open(newline='') as f:
        reader = csv.DictReader(f, strict=True)
        fields = reader.fieldnames or []
        if not {'logic_frame', 'frame_ms'}.issubset(fields) or len(fields) != len(set(fields)):
            raise ValueError('timing CSV requires unique logic_frame and frame_ms columns')
        ids, values = [], []
        for line, row in enumerate(reader, start=2):
            if None in row or any(value is None for value in row.values()):
                raise ValueError(f'timing CSV line {line} has the wrong number of columns')
            try:
                frame = int(row['logic_frame'])
                value = float(row['frame_ms'])
            except (ValueError, TypeError) as error:
                raise ValueError(f'timing CSV line {line} has invalid timing data') from error
            if not math.isfinite(value) or value <= 0:
                raise ValueError(f'timing CSV line {line} requires finite positive frame_ms')
            ids.append(frame)
            values.append(value)
    return ids, sorted(values)


def timing_statistics(values):
    # Nearest-rank percentiles retain the existing p95 definition for 600 samples.
    return dict(samples=len(values), mean_ms=statistics.mean(values),
        median_ms=statistics.median(values), p95_ms=values[math.ceil(len(values) * .95) - 1],
        p99_ms=values[math.ceil(len(values) * .99) - 1], max_ms=values[-1],
        slowframes_over16_67=sum(value > 16.67 for value in values),
        slowframes_over33_33=sum(value > 33.33 for value in values))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('preview', 'root', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--palette', action='append', choices=PALETTES)
    parser.add_argument('--fields', type=int, action='append')
    parser.add_argument('--layout', choices=['grid', 'overlap'], default='grid')
    parser.add_argument('--volume-side', type=int, choices=[256, 512], default=512)
    parser.add_argument('--radius', type=float, default=30)
    parser.add_argument('--fringe', type=float, default=14)
    parser.add_argument('--spacing', type=float, default=64)
    parser.add_argument('--columns', type=int, default=16)
    parser.add_argument('--zoom', type=float)
    parser.add_argument('--stagger', type=float, default=0)
    parser.add_argument('--lifetime', type=float, default=24)
    parser.add_argument('--frames', type=int, default=600)
    parser.add_argument('--shot', type=int, default=420)
    parser.add_argument('--reference', action='store_true')
    parser.add_argument('--sync-bake', action='store_true')
    parser.add_argument('--bake-slices', type=int)
    parser.add_argument('--freeze-time', type=float)
    parser.add_argument('--replay-frame', type=int)
    parser.add_argument('--palette-at', nargs=2, metavar=('FRAME', 'PALETTE'))
    parser.add_argument('--timing', action='store_true')
    parser.add_argument('--timing-start', type=int, default=240,
                        help='first logic frame of the 600-frame timing window (default: 240)')
    parser.add_argument('--repeat', type=int, default=1)
    parser.add_argument('--gpu-debug', action='store_true')
    parser.add_argument('--timeout', type=int, default=180)
    args = parser.parse_args()
    for key in ('preview', 'root', 'out'):
        setattr(args, key, getattr(args, key).resolve())
    counts = args.fields or [128]
    if not args.preview.is_file() or not args.root.is_dir():
        parser.error('the built preview and prepared data root must already exist')
    if any(n < 1 or n > 4096 for n in counts) or args.frames < 2 or args.repeat < 1:
        parser.error('invalid field, frame or repeat count')
    if args.reference and max(counts) > 2:
        parser.error('the production reference supports at most two fields')
    if args.bake_slices is not None and not 1 <= args.bake_slices <= 96:
        parser.error('--bake-slices must be 1..96')
    if args.freeze_time is not None and (not math.isfinite(args.freeze_time) or not 0 <= args.freeze_time <= 10000):
        parser.error('--freeze-time must be finite and 0..10000')
    if args.replay_frame is not None and not 0 < args.replay_frame < args.frames:
        parser.error('--replay-frame must be positive and precede the final frame')
    if args.palette_at is not None:
        try:
            args.palette_at[0] = int(args.palette_at[0])
        except ValueError:
            parser.error('--palette-at requires an integer frame')
        if not 0 < args.palette_at[0] < args.frames or args.palette_at[1] not in PALETTES:
            parser.error('--palette-at requires a positive frame before the final frame and a supported palette')
    timing_end = args.timing_start + 599
    if args.timing_start < 0:
        parser.error('--timing-start must be nonnegative')
    if args.timing and args.frames <= timing_end + 1:
        parser.error(f'timing requires --frames at least {timing_end + 2} '
                     f'(window is {args.timing_start}..{timing_end})')
    if not args.timing and not 0 < args.shot < args.frames:
        parser.error('the screenshot frame must precede the final frame')
    args.out.mkdir(parents=True, exist_ok=False)
    exe_hash = digest(args.preview)
    results = []
    for repeat in range(args.repeat):
        for palette in args.palette or PALETTES:
            for count in counts:
                name = f'{palette}-{count}-{args.layout}-{repeat}'
                run = args.out / name
                run.mkdir()
                command = [str(args.preview), '--game-data', str(args.root), '--user-dir', str(run / 'user'),
                    '--fields', str(count), '--palette', palette, '--layout', args.layout,
                    '--volume-side', str(args.volume_side), '--radius', str(args.radius),
                    '--fringe', str(args.fringe), '--spacing', str(args.spacing), '--columns', str(args.columns),
                    '--stagger', str(args.stagger), '--lifetime', str(args.lifetime),
                    '--frames', str(args.frames), '--offscreen', '--uncapped']
                if args.zoom:
                    command += ['--zoom', str(args.zoom)]
                if args.reference:
                    command += ['--reference']
                if args.sync_bake:
                    command += ['--sync-bake']
                for option in ('bake_slices', 'freeze_time', 'replay_frame'):
                    value = getattr(args, option)
                    if value is not None:
                        command += ['--' + option.replace('_', '-'), str(value)]
                if args.palette_at is not None:
                    command += ['--palette-at', str(args.palette_at[0]), args.palette_at[1]]
                if not args.timing:
                    command += ['--screenshot-frame', str(args.shot)]
                env = dict(os.environ)
                for key in ('ZH_GPU_TIMING', 'ZH_GPU_TIMING_LOGIC', 'ZH_GPU_TIMING_SYNC', 'ZH_GPU_TIMING_COLD',
                            'ZH_GPU_TIMING_CSV', 'ZH_GPU_DEBUG', 'ZH_CLOUD_REPORT'):
                    env.pop(key, None)
                overrides = {'SDL_GPU_DRIVER': 'vulkan', 'ZH_UNATTENDED': '1', 'ZH_AUDIO_BACKEND': 'null',
                    'ZH_SUN_SHADOW_CAPTURE_CLOCK': 'logic', 'ZH_WATER_CAPTURE_CLOCK': 'picture'}
                if args.gpu_debug:
                    overrides['ZH_GPU_DEBUG'] = '1'
                if args.timing:
                    overrides.update(ZH_GPU_TIMING_LOGIC=f'{args.timing_start},{timing_end}',
                                     ZH_GPU_TIMING_CSV=str(run / 'frames.csv'))
                env.update(overrides)
                manifest = dict(name=name, command=command, environment=overrides, exe_sha256=exe_hash,
                    fixture=dict(sync_bake=args.sync_bake, bake_slices=args.bake_slices,
                        freeze_time=args.freeze_time, replay_frame=args.replay_frame,
                        palette_at=args.palette_at))
                (run / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
                start = time.monotonic()
                with (run / 'run.log').open('w') as log:
                    try:
                        process = subprocess.run(command, cwd=run, env=env, stdout=log, stderr=subprocess.STDOUT,
                                                 timeout=args.timeout)
                        manifest['returncode'] = process.returncode
                    except subprocess.TimeoutExpired:
                        manifest['returncode'] = -1
                        manifest['timeout'] = True
                manifest['elapsed_seconds'] = time.monotonic() - start
                log = (run / 'run.log').read_text(errors='replace')
                rows = records(log)
                manifest['reports'] = rows
                manifest['lifecycle_events'] = re.findall(
                    r'^cloud-stress: (?:replay|palette(?:-switch)?)[^\n]*$', log, re.M)
                manifest['screenshots'] = [dict(path=str(p), sha256=digest(p)) for p in sorted(run.rglob('sshot*.bmp'))]
                checks = manifest['checks'] = []
                def check(name, passed):
                    checks.append(dict(name=name, passed=bool(passed)))
                check('successful engine exit', manifest['returncode'] == 0)
                check('no cloud/GPU validation errors', not ERROR.search(log))
                check('all source records retained', rows and all(r['retained'] == count for r in rows))
                final_seconds = max((r['seconds'] for r in rows), default=0)
                scripted_reset = args.replay_frame is not None or args.palette_at is not None
                together_expected = (not scripted_reset and final_seconds > (count - 1) * args.stagger + .6
                                     and (count - 1) * args.stagger + .6 < args.lifetime
                                     and (args.freeze_time is None or args.freeze_time < args.lifetime))
                if together_expected:
                    check('all requested fields became active together', rows and max(r['peak-active'] for r in rows) == count)
                    check('all requested fields submitted volumetrically', rows and max(r['peak-submitted'] for r in rows) == count)
                check('no Classic substitution', 'hazard-cloud-fallback:' not in log)
                check('no gameplay hazard objects or damage', rows and all(r['damage-simulation'] == 'absent' for r in rows))
                if not scripted_reset and final_seconds >= args.lifetime + (count - 1) * args.stagger:
                    check('all sources expired', rows[-1]['active'] == 0 and rows[-1]['submitted'] == 0)
                if args.layout == 'overlap' and rows and not args.reference and together_expected:
                    check('spatial bin retains every overlapping source', max(r['maximum-bin-sources'] for r in rows) == count)
                if not args.reference:
                    check('shared atlas memory stable during run', rows and len({r['volume-bytes'] for r in rows}) == 1)
                    bake_rows = [r for r in rows if 'max-slices-per-frame' in r]
                    if bake_rows and not args.sync_bake:
                        budget = args.bake_slices if args.bake_slices is not None else 8
                        check('incremental bake slice budget respected',
                              all(r['max-slices-per-frame'] <= budget for r in bake_rows))
                    if bake_rows and not scripted_reset:
                        rise = min(6 if args.radius >= 100 else 3, args.lifetime * .25) \
                            if palette in ('mixed', 'nuclear') else min(.6, args.lifetime * .25)
                        mature_at = (count - 1) * args.stagger + rise + 1
                        mature_rows = [r for r in bake_rows if mature_at <= r['seconds'] <= args.lifetime - 1
                                       and r['render-frames'] >= 60]
                        if mature_rows:
                            check('completed atlas includes every mature source',
                                  any(r.get('baked-sources') == count for r in mature_rows))
                if args.timing:
                    check('complete 600-frame timing window', 'samples=600 expected=600 rejected=0 status=complete' in log)
                    check('timing CSV exists', (run / 'frames.csv').exists())
                    if (run / 'frames.csv').exists():
                        try:
                            ids, values = read_timing(run / 'frames.csv')
                        except (OSError, ValueError, csv.Error) as error:
                            manifest['timing_error'] = str(error)
                            check('timing CSV contains valid finite positive samples', False)
                            check('timing IDs match exactly', False)
                        else:
                            check('timing CSV contains valid finite positive samples', bool(values))
                            check('timing IDs match exactly', ids == list(range(args.timing_start, timing_end + 1)))
                            if values:
                                manifest['timing'] = timing_statistics(values)
                else:
                    check('screenshot captured', len(manifest['screenshots']) == 1)
                manifest['passed'] = all(c['passed'] for c in checks)
                (run / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
                results.append(manifest)
                print(name, 'PASS' if manifest['passed'] else 'FAIL', manifest.get('timing', {}), flush=True)
    cross_checks = []
    if not args.reference and len(set(counts)) > 1:
        sizes = {row['volume-bytes'] for run in results for row in run['reports']}
        cross_checks.append(dict(name='shared atlas memory independent of source count', passed=len(sizes) == 1))
    summary = dict(passed=all(r['passed'] for r in results) and all(c['passed'] for c in cross_checks),
                   checks=cross_checks, results=results)
    (args.out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    return 0 if summary['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
