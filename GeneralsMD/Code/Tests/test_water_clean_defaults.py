#!/usr/bin/env python3
"""Bit-exact coefficients and baked/live shader constants for the approved profile."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

probe = Path(sys.argv[1]).resolve()
fixture = Path(__file__).with_name('water_clean_approved.ini')
base = {k: v for k, v in os.environ.items() if not k.startswith('ZH_WATER_')}
with tempfile.TemporaryDirectory(prefix='water-clean-defaults-') as folder:
    root = Path(folder)
    tuning = root / 'approved.ini'
    tuning.write_bytes(fixture.read_bytes())
    for angle in ('fallback', '90', '-60'):
        snapshots = []
        for live in (False, True):
            user = root / f'user-{angle}-{live}'
            user.mkdir()
            snapshot = user / 'snapshot.bin'
            env = base | {'ZH_WATER_CLEAN': '1', 'ZH_USER_DATA_DIR': str(user)}
            if live:
                env |= {'ZH_WATER_TUNING': '1', 'ZH_WATER_TUNING_FILE': str(tuning)}
            subprocess.run([str(probe), str(snapshot), angle], env=env, check=True)
            snapshots.append(snapshot.read_bytes())
        if snapshots[0] != snapshots[1]:
            first = next((i for i, (a, b) in enumerate(zip(*snapshots)) if a != b),
                         min(map(len, snapshots)))
            raise AssertionError(f'clean default/file mismatch at byte {first}, map {angle}')
        print(f'PASS map {angle}: exact 128/256 coefficients, shader constants and phase ({len(snapshots[0])} bytes)')
