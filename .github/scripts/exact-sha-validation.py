#!/usr/bin/env python3
"""Fork-only exact-source validation helpers; never modifies tracked candidate files."""
import difflib
import hashlib
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
    path = reports / 'ctest.xml'
    if not path.exists():
        raise SystemExit('CTest did not produce JUnit; inspect test logs (no successful test report)')
    cases = list(ET.parse(path).getroot().iter('testcase'))
    skipped = [c.attrib['name'] for c in cases if c.find('skipped') is not None or c.get('status') in ('notrun', 'disabled')]
    failed = [c.attrib['name'] for c in cases if c.find('failure') is not None or c.find('error') is not None]
    data = dict(total=len(cases), passed=len(cases)-len(skipped)-len(failed), failed=len(failed),
                skipped=len(skipped), skipped_tests=skipped, failed_tests=failed, excluded_tests=[])
    (reports / 'test-summary.json').write_text(json.dumps(data, indent=2) + '\n')
    with open(os.environ['GITHUB_STEP_SUMMARY'], 'a') as out:
        out.write(f"### Exact source `{sha}`\n\nWorkflow: `{os.environ['GITHUB_SHA']}`\n\n")
        out.write(f"CTest: {data['total']} total, {data['passed']} passed, {len(failed)} failed, {len(skipped)} skipped. No test-name exclusions.\n\n")
        out.write('Skipped tests (not validated):\n' + ''.join(f'- `{name}`\n' for name in skipped))
    print(json.dumps(data, indent=2))
    if not cases or failed:
        raise SystemExit('CTest has failures or zero cases')


def stage():
    build = Path(os.environ['RUNNER_TEMP']) / 'build'
    binary = build / 'generals'
    if not binary.is_file() or not (build / 'overlay').is_dir():
        raise SystemExit('Expected tested generals binary and overlay were not produced')
    handoff = Path(os.environ['RUNNER_TEMP']) / 'handoff'
    root = handoff / f'linux-validation-{sha}'
    (root / 'bin').mkdir(parents=True)
    shutil.copy2(binary, root / 'bin/generals')
    # A fresh checkout and art suppression produce no external assets. Refuse links
    # and .big archives rather than accidentally dereference an art/game-data link.
    for path in (build / 'overlay').rglob('*'):
        if path.is_symlink() or path.suffix.lower() == '.big':
            raise SystemExit(f'Unexpected external/archive overlay asset: {path}')
    shutil.copytree(build / 'overlay', root / 'overlay')
    for name in ('provenance.json', 'test-summary.json', 'vendor-code-only.patch', 'vendor-hashes.json'):
        shutil.copy2(reports / name, root / name)
    shutil.copy2(candidate / 'LICENSE.md', root / 'LICENSE.md')
    (root / 'README.txt').write_text(
        'Validation handoff for Ubuntu 24.04 x86_64, not a portable release package.\n'
        'Contains the exact CTest-tested Release binary and repository overlay.\n'
        'Requires original game data and system runtime libraries; skipped tests are not validated.\n'
        'Run bin/generals -overlay /absolute/path/to/overlay with your own game installation.\n')
    files = sorted(p for p in root.rglob('*') if p.is_file())
    (root / 'SHA256SUMS').write_text(''.join(
        f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(root)}\n' for p in files))
    with tarfile.open(handoff / f'{root.name}.tar.gz', 'w:gz') as archive:
        archive.add(root, arcname=root.name)


if __name__ == '__main__':
    {'metadata': metadata, 'vendor': vendor, 'summary': summary, 'stage': stage}[sys.argv[1]]()
