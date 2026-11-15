#!/usr/bin/env python3
"""Phase-aligned native present intervals; production helper supplies all rain boundaries."""
import argparse,csv,hashlib,io,json,math,re,statistics,subprocess
from pathlib import Path

def stats(rows,key):
    values=sorted(float(row[key]) for row in rows)
    if not values:return None
    assert all(math.isfinite(v) and v>=0 for v in values)
    return dict(samples=len(values),median=statistics.median(values),p95=values[math.ceil(.95*len(values))-1],
        mean=statistics.mean(values),minimum=values[0],maximum=values[-1])

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('runs',type=Path);p.add_argument('--helper',type=Path,required=True)
    p.add_argument('--admission',type=Path,required=True)
    p.add_argument('--rain-seed',type=int,help='Optional cross-check only; the qualified map inventory owns the seed')
    p.add_argument('--clock-frame-offset',type=int,default=1)
    p.add_argument('--series-helper',type=Path,required=True,help='Hashed compiled rain_series.cpp using the exact production schedule header')
    p.add_argument('--out',type=Path,required=True);a=p.parse_args();helper=a.helper.resolve()
    digest=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
    admitted=json.loads(a.admission.read_text());admission_sha=digest(a.admission)
    assert admitted['qualified']
    seed=admitted['cases']['rain']['rain_seed']
    assert a.rain_seed is None or a.rain_seed==seed,'CLI seed differs from the production-probed qualified map'
    a.rain_seed=seed
    assert digest(a.series_helper)==admitted['phase_helper_sha256'],'Phase-series helper differs from admission'
    assert digest(helper)==admitted['phase_window_helper_sha256'],'Production window helper differs from admission'
    for key in ('phase_source','phase_tool_source','retail_inventory'):
        assert digest(admitted[key+'_path'])==admitted[key+'_sha256'],key+' differs from admission'
    windows={}
    assert a.clock_frame_offset==admitted['clock_frame_offset']==1
    output=subprocess.check_output([str(a.series_helper.resolve()),'1','25200',str(seed)],text=True)
    series={int(row['logic_frame']):row for row in csv.DictReader(io.StringIO(output))}
    a.out.mkdir(parents=True,exist_ok=True);(a.out/'rain-phase-series.csv').write_text(output)
    def seconds(frame): return ((frame-a.clock_frame_offset)*1000//60)/1000
    def window(seconds):
        slot=math.floor(seconds/210)
        if slot not in windows:
            windows[slot]=json.loads(subprocess.check_output([str(helper),'--rain-window',str(slot*210+.5),str(a.rain_seed)],text=True))
        return windows[slot]
    def phase(frame):
        return {'dry':'dry','fade_in':'fade-in','fade_out':'fade-out','wet':'plateau'}[series[frame]['phase']]
    window(.5);window(210.5)
    summaries=[];rejected=[];seen=set();pairs=[]
    for manifest_path in sorted(a.runs.glob('*/manifest.json')):
        m=json.loads(manifest_path.read_text());timing=manifest_path.parent/'timing.csv'
        if m.get('repeat',0)<=0 or not timing.exists():continue
        if not m.get('accepted'):
            rejected.append(dict(run=manifest_path.parent.name,manifest_sha256=digest(manifest_path),timing_error=m.get('timing_error'),crc=m.get('crc'),returncode=m.get('returncode')))
            continue
        case=admitted['cases'][m['kind']];artifact=admitted['artifacts'][m['label']]
        assert m['admission_sha256']==admission_sha
        for key in ('runner_sha256','generator_sha256','overlay_inventory_sha256','phase_helper_sha256','phase_source_sha256','phase_tool_source_sha256','retail_inventory_sha256'):
            assert m[key]==admitted[key],key+' mismatch in '+str(manifest_path)
        assert m['candidate_sha256']==m['candidate_after_sha256']==artifact['sha256'] and m['variant']==artifact['variant']
        assert m['overlay_after_inventory_sha256']==admitted['overlay_inventory_sha256']
        for key in ('map_sha256','scenario_sha256','profile_common_sha256','first','last','width','height','audio','idle_ai'):
            assert m[key]==case[key],key+' pairing mismatch'
        assert m['rain_seed']==case.get('rain_seed'),'Seed is not linked to the qualified map/helper'
        assert m['crc_admitted'] and m['expected_crc']==case['expected_crc'] and m['expected_crc_frame']==case['expected_crc_frame']
        crc=[re.search(r'(0x[0-9A-Fa-f]+) at frame (\d+)',line).groups() for line in m['crc']]
        assert crc and all(value.upper()==case['expected_crc'].upper() and int(frame)==case['expected_crc_frame'] for value,frame in crc)
        assert m['capture_clock_arm_inert'] and m['capture_clock_arm_frame']>m['last']+120
        assert m['environment'].get('VK_DRIVER_FILES')==admitted['vulkan_icd'] and m['environment']['ZH_GPU_TIMING_SYNC']=='0'
        assert digest(timing)==m['timing_csv_sha256'],'Raw timing file changed after collection'
        identity=(m['kind'],m['label'],m['repeat']);assert identity not in seen,'Duplicate admitted repeat';seen.add(identity)
        pairs.append(dict(kind=m['kind'],label=m['label'],repeat=m['repeat'],manifest_sha256=digest(manifest_path),timing_sha256=digest(timing),crc=case['expected_crc'],frame=case['expected_crc_frame']))
        with timing.open() as stream:rows=list(csv.DictReader(stream))
        assert [int(r['logic_frame']) for r in rows]==list(range(m['first'],m['last']+1)),f'Incomplete window: {manifest_path}'
        subsets={}
        if m['kind']=='rain':
            subsets['active-30s']=[r for r in rows if 601<=int(r['logic_frame'])<=2400]
            assert len(subsets['active-30s'])==1800
            assert all(phase(int(r['logic_frame']))=='plateau' for r in subsets['active-30s'])
            for label in ('plateau','fade-in','fade-out','dry'):
                subsets[label]=[r for r in rows if phase(int(r['logic_frame']))==label]
            cycle=[r for r in rows if 210<=seconds(int(r['logic_frame']))<420]
            assert len(cycle)==12600,'A complete second schedule slot is required after warmup'
            subsets['complete-cycle']=cycle
            occupancy={label:sum(phase(int(r['logic_frame']))==label for r in cycle) for label in ('plateau','fade-in','fade-out','dry')}
            if series:
                occupancy['mean_envelope']=statistics.mean(float(series[int(r['logic_frame'])]['envelope']) for r in cycle)
        else:subsets['active-30s']=rows;occupancy={'active':len(rows)}
        for label,subset in subsets.items():
            if not subset:continue
            record=dict(run=manifest_path.parent.name,kind=m['kind'],variant=m['variant'],label=m['label'],repeat=m['repeat'],phase=label,
                artifact_label=m['label'],candidate_sha256=m['candidate_sha256'],profile_sha256=m['profile_sha256'],map_sha256=m['map_sha256'],
                first=min(int(r['logic_frame']) for r in subset),last=max(int(r['logic_frame']) for r in subset),
                frame_ms=stats(subset,'frame_ms'),work_ms=stats(subset,'work_ms'),draws=stats(subset,'draws'),
                cycle_occupancy=occupancy)
            summaries.append(record)
    expected={(kind,label,repeat) for kind in admitted['cases'] for label in admitted['artifacts'] for repeat in range(1,4)}
    assert seen==expected,'Missing/extra accepted repeats: '+str(expected.symmetric_difference(seen))
    a.out.mkdir(parents=True,exist_ok=True)
    (a.out/'phase-results.json').write_text(json.dumps(dict(clock_frame_offset=a.clock_frame_offset,clock_formula='floor((logic_frame-offset)*1000/60)/1000',rain_seed=a.rain_seed,
        series_helper_sha256=hashlib.sha256(a.series_helper.read_bytes()).hexdigest() if a.series_helper else None,
        admission_sha256=admission_sha,pairing_admission=pairs,rejected_runs=rejected,
        production_windows=windows,timing_basis='Native end-to-end offscreen present intervals; not hardware GPU timestamps or FPS',
        phase_note='Baseline continuous rain and final OFF are labelled by the matching enhanced schedule to compare identical absolute scene times.',runs=summaries),indent=2)+'\n')
    columns=['run','kind','artifact_label','variant','repeat','phase','samples','median_ms','p95_ms','mean_ms','min_ms','max_ms','candidate_sha256']
    with (a.out/'phase-results.csv').open('w') as stream:
        writer=csv.DictWriter(stream,fieldnames=columns);writer.writeheader()
        for row in summaries:
            metric=row['frame_ms'];writer.writerow(dict(run=row['run'],kind=row['kind'],artifact_label=row['artifact_label'],variant=row['variant'],repeat=row['repeat'],
                phase=row['phase'],samples=metric['samples'],median_ms=metric['median'],p95_ms=metric['p95'],mean_ms=metric['mean'],
                min_ms=metric['minimum'],max_ms=metric['maximum'],candidate_sha256=row['candidate_sha256']))
    lines=['| Kind | Artifact label | Phase | Run | Samples | Mean ms | Median ms | p95 ms | Max ms |','|---|---|---|---|---:|---:|---:|---:|---:|']
    for row in summaries:
        metric=row['frame_ms'];lines.append(f"| {row['kind']} | {row['artifact_label']} | {row['phase']} | {row['run']} | {metric['samples']} | {metric['mean']:.3f} | {metric['median']:.3f} | {metric['p95']:.3f} | {metric['maximum']:.3f} |")
    (a.out/'all-repeats.md').write_text('\n'.join(lines)+'\n');print(f'{len(summaries)} phase distributions exported')

if __name__=='__main__':main()
