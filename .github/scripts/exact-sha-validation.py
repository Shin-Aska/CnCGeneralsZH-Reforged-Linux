#!/usr/bin/env python3
"""Fork-only exact-source validation helpers; never modifies tracked candidate files."""
import difflib
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import xml.etree.ElementTree as ET

workspace = Path(os.environ['GITHUB_WORKSPACE'])
reports = Path(os.environ['RUNNER_TEMP']) / 'reports'
candidate = workspace / 'candidate'
sha = os.environ['CANDIDATE_SHA']


def metadata():
    actual = subprocess.check_output(['git', '-C', str(candidate), 'rev-parse', 'HEAD'], text=True).strip()
    if not re.fullmatch('[0-9a-f]{40}', sha) or actual != sha:
        raise SystemExit('Source SHA mismatch')
    data = dict(candidate_sha=sha, actual_source_sha=actual,
                workflow_sha=os.environ['GITHUB_SHA'], workflow_ref=os.environ['GITHUB_WORKFLOW_REF'],
                label=os.environ.get('CANDIDATE_LABEL', ''), repository=os.environ['GITHUB_REPOSITORY'],
                run_id=os.environ['GITHUB_RUN_ID'], run_attempt=os.environ['GITHUB_RUN_ATTEMPT'],
                runner='ubuntu-24.04', configuration='Release', compiler='gcc-14/g++-14',
                game_data=False, optional_art=False)
    baseline = json.loads((Path(__file__).resolve().parents[1]/'validation/4.1.0-baseline.json').read_text())
    data['baseline_reference'] = baseline['reference']
    (reports / 'provenance.json').write_text(json.dumps(data, indent=2) + '\n')


def vendor():
    source = candidate / 'GeneralsMD/Code/Tools/vendor.sh'
    original = source.read_text()
    # Fail closed if a branch changes this audited terminal call structure.
    terminal = "install_art\nstep 'everything the build needs is in place'"
    if original.count(terminal) != 1 or len(re.findall(r'^install_art$', original, re.M)) != 1:
        raise SystemExit('Unrecognized vendor.sh terminal install_art call; manual audit required')
    modified = original.replace(terminal, "# CI: optional install_art invocation suppressed\nstep 'everything the build needs is in place'")
    destination = source.with_name('vendor.ci-code-only.sh')
    if destination.exists() or subprocess.check_output(
            ['git', '-C', str(candidate), 'ls-files', '--', 'GeneralsMD/Code/Tools/vendor.ci-code-only.sh']):
        raise SystemExit('Temporary vendor helper path already exists')
    destination.write_text(modified)
    (reports / 'vendor-code-only.patch').write_text(''.join(difflib.unified_diff(
        original.splitlines(True), modified.splitlines(True), fromfile='vendor.sh', tofile=destination.name)))
    (reports / 'vendor-hashes.json').write_text(json.dumps({
        'original_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
        'temporary_sha256': hashlib.sha256(destination.read_bytes()).hexdigest()}, indent=2) + '\n')


def summary():
    spec = importlib.util.spec_from_file_location('exact_sha_regression', Path(__file__).with_name('exact-sha-regression.py'))
    regression = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(regression)
    try:
        data = regression.evaluate(reports, Path(os.environ['RUNNER_TEMP'])/'build')
    except (OSError, ValueError, KeyError, ET.ParseError) as exc:
        data = dict(regression_gate=dict(accepted=False, errors=[f'Incomplete or invalid CTest evidence: {exc}']))
    (reports/'test-summary.json').write_text(json.dumps(data,indent=2)+'\n')
    baseline = json.loads(regression.BASELINE_PATH.read_text())
    shutil.copy2(regression.BASELINE_PATH,reports/'regression-baseline.json')
    provenance_path = reports/'provenance.json'
    provenance = json.loads(provenance_path.read_text())
    provenance['baseline_reference'] = baseline['reference']
    provenance['regression_gate'] = data['regression_gate']
    provenance['inherited_failures'] = data.get('inherited_failures', [])
    provenance['raw_ctest_counts'] = {k:data.get(k) for k in ('total','passed','failed','skipped')}
    provenance['raw_ctest_exit'] = data.get('raw_exit')
    provenance_path.write_text(json.dumps(provenance,indent=2)+'\n')
    with open(os.environ['GITHUB_STEP_SUMMARY'],'a') as out:
        out.write(f"### Exact source `{sha}`\n\nWorkflow: `{os.environ['GITHUB_SHA']}`\n\n")
        ref=baseline['reference']
        out.write(f"Compared with source `{ref['source_sha']}`, [baseline run {ref['run_id']}]({ref['run_url']}).\n\n")
        out.write(f"Regression gate: **{'accepted' if data['regression_gate']['accepted'] else 'rejected'}**. Full CTest; no test-name exclusions.\n\n")
        if 'total' in data:
            out.write(f"Raw CTest: {data['total']} total, {data['passed']} passed, {data['failed']} failed, {data['skipped']} skipped; exit `{data['raw_exit']}`.\n\n")
            out.write('Inherited failures with unchanged reviewed signatures (still red):\n'+''.join(f'- `{name}`\n' for name in data['inherited_failures'])+'\n')
            out.write('Skipped tests (not validated):\n'+''.join(f'- `{name}`\n' for name in data['skipped_tests'])+'\n')
        for error in data['regression_gate']['errors']:
            out.write(f'- Rejected: {error}\n')
    print(json.dumps(data,indent=2))
    if not data['regression_gate']['accepted']:
        raise SystemExit('CTest regression gate rejected the candidate')


def stage(verified=True):
    summary_path = reports/'test-summary.json'
    result = json.loads(summary_path.read_text()) if summary_path.is_file() else {}
    if verified and result.get('regression_gate',{}).get('accepted') is not True:
        raise SystemExit('No accepted regression gate; cannot export the binary')
    build = Path(os.environ['RUNNER_TEMP']) / 'build'
    binary = build / 'generals'
    if not binary.is_file() or not (build / 'overlay').is_dir():
        raise SystemExit('Expected tested generals binary and overlay were not produced')
    handoff = Path(os.environ['RUNNER_TEMP']) / ('handoff' if verified else 'unverified-handoff')
    root = handoff / f"{'linux-validation' if verified else 'linux-UNVERIFIED-diagnostic'}-{sha}"
    (root / 'bin').mkdir(parents=True)
    shutil.copy2(binary, root / 'bin/generals')
    # A fresh checkout and art suppression produce no external assets. Refuse links
    # and .big archives rather than accidentally dereference an art/game-data link.
    for path in (build / 'overlay').rglob('*'):
        if path.is_symlink() or path.suffix.lower() == '.big':
            raise SystemExit(f'Unexpected external/archive overlay asset: {path}')
    shutil.copytree(build / 'overlay', root / 'overlay')
    for name in ('provenance.json', 'test-summary.json', 'vendor-code-only.patch', 'vendor-hashes.json', 'ctest-exit.json', 'regression-baseline.json'):
        if (reports/name).is_file():
            shutil.copy2(reports/name,root/name)
        elif verified:
            raise SystemExit(f'Missing validation metadata: {name}')
    provenance = json.loads((root/'provenance.json').read_text())
    provenance['artifact_verification'] = 'regression-gate-accepted' if verified else 'UNVERIFIED-regression-gate-rejected'
    (root/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
    if not verified:
        (root/'UNVERIFIED.txt').write_text('REGRESSION GATE REJECTED. Diagnostic binaries only; tests did not establish an accepted candidate.\n')
        for name in sorted(set(result.get('failed_tests',[])) | {'test_water_grid','sdl_gpu_frame_selfcheck','test_gameengine'}):
            test_binary=build/name
            if re.fullmatch(r'[A-Za-z0-9_.-]+',name) and test_binary.is_file() and not test_binary.is_symlink():
                (root/'tests').mkdir(exist_ok=True)
                shutil.copy2(test_binary,root/'tests'/name)
    shutil.copy2(candidate / 'LICENSE.md', root / 'LICENSE.md')
    (root / 'README.txt').write_text(
        ('REGRESSION GATE ACCEPTED: inspect raw counts and inherited failures.\n' if verified else 'UNVERIFIED: regression gate rejected; diagnostic use only.\n') +
        'Validation handoff for Ubuntu 24.04 x86_64, not a portable release package.\n'
        'Contains the exact built Release binary and repository overlay.\n'
        'The regression gate permits only unchanged reviewed inherited failures; inspect test-summary.json.\n'
        'Requires original game data and system runtime libraries; skipped tests are not validated.\n'
        'Run bin/generals -overlay /absolute/path/to/overlay with your own game installation.\n')
    files = sorted(p for p in root.rglob('*') if p.is_file())
    (root / 'SHA256SUMS').write_text(''.join(
        f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(root)}\n' for p in files))
    with tarfile.open(handoff / f'{root.name}.tar.gz', 'w:gz') as archive:
        archive.add(root, arcname=root.name)


if __name__ == '__main__':
    {'metadata': metadata, 'vendor': vendor, 'summary': summary, 'stage': stage, 'diagnostic-stage': lambda: stage(False)}[sys.argv[1]]()
