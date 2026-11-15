#!/usr/bin/env python3
"""Run a prepared private weather fixture with an explicit existing -seed value.

Prepare it using the measured-v4 run_weather.py without --run. This follow-up
does not change that frozen collector or admit new benchmark measurements.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('fixture', type=Path)
    parser.add_argument('--seed', type=int, required=True)
    args = parser.parse_args()
    folder = args.fixture.resolve()
    prepared = folder / 'manifest.json'
    result_path = folder / 'startup-result.json'
    if result_path.exists():
        raise SystemExit('Refusing to overwrite an executed fixture')
    manifest = json.loads(prepared.read_text())
    assert manifest['repeat'] == 0, 'Qualification only; no primary timing'
    command = manifest['command'][:]
    command[command.index('-seed') + 1] = str(args.seed)
    candidate = Path(command[0])
    assert digest(candidate) == manifest['candidate_sha256']
    environment = os.environ.copy()
    for key in ('ZH_GPU_DEBUG', 'VK_INSTANCE_LAYERS', 'ZH_GPU_CREATION_LOG'):
        environment.pop(key, None)
    environment.update(manifest['environment'])
    result = dict(prepared_manifest_sha256=digest(prepared), command=command,
                  environment=manifest['environment'], match_seed=args.seed,
                  candidate_sha256=digest(candidate), runner_sha256=digest(Path(__file__)))
    result_path.write_text(json.dumps(result, indent=2) + '\n')
    prefix = manifest['log_prefix']
    sources = (folder / 'user/Logs' / (prefix + 'DebugLogFile.txt'),
               candidate.parent / (prefix + 'DebugLogFile.txt'))
    assert not any(path.exists() for path in sources), 'Log prefix collision'
    start = time.monotonic()
    with (folder / 'run.log').open('w') as stream:
        run = subprocess.run(command, cwd=folder, env=environment,
                             stdout=stream, stderr=subprocess.STDOUT, timeout=180)
    result.update(returncode=run.returncode, wall_seconds=time.monotonic() - start,
                  candidate_after_sha256=digest(candidate))
    log = (folder / 'run.log').read_text(errors='replace')
    for source in sources:
        if source.exists():
            shutil.copy2(source, folder / 'engine.log')
            log += '\n' + source.read_text(errors='replace')
            result['engine_log_sha256'] = digest(folder / 'engine.log')
            break
    result['crc'] = sorted(set(re.findall(r'HEADLESS CRC AT LIMIT:.*', log)))
    result['validation_errors'] = [line for line in log.splitlines()
                                   if 'VUID-' in line or 'Validation Error' in line]
    from PIL import Image
    result['pictures'] = []
    for index, path in enumerate(sorted((folder / 'user').glob('sshot*.bmp'))):
        target = folder / f'picture-{index}.png'
        with Image.open(path) as source:
            source.save(target)
        result['pictures'].append(dict(path=target.name, sha256=digest(target)))
    result['accepted'] = (run.returncode == 0 and bool(result['crc'])
                          and bool(result['pictures']) and not result['validation_errors']
                          and result['candidate_after_sha256'] == result['candidate_sha256'])
    result_path.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['accepted'] else 1)


if __name__ == '__main__':
    main()
