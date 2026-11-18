#!/usr/bin/env python3
"""Fail-closed CTest comparison against a reviewed immutable runner baseline."""
from collections import Counter
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

BASELINE_PATH = Path(__file__).resolve().parents[1] / 'validation/4.1.0-baseline.json'


def normalize(line):
    # Preserve assertion expressions, counts, numeric values and file names.
    # Only runner prefixes and source/view line numbers are incidental.
    line = re.sub(r'/[^\s:]*?/candidate/GeneralsMD/Code/', '', line)
    line = re.sub(r'(?<=\.cpp):\d+(?=[:\s])', ':<line>', line)
    line = re.sub(r'(?<=\.h):\d+(?=[:\s])', ':<line>', line)
    line = re.sub(r'view line \d+:', 'view line <line>:', line)
    return line.strip()


def signature(output, kind):
    if '[This part of the test output was removed' in output:
        raise ValueError('truncated failure output cannot establish an exact signature')
    if re.search(r'(?im)(segmentation fault|addresssanitizer|undefinedbehaviorsanitizer|'
                 r'terminate called|uncaught exception|assertion .* failed|\btimeout\b|\btimed out\b)', output):
        raise ValueError('unexpected exception, sanitizer diagnostic or timeout')
    lines = [line.strip() for line in output.splitlines() if line.strip()]
    if kind == 'sdl_assertions':
        assertions = [normalize(line) for line in lines if line.startswith('FAIL ')]
        # Every failure line must identify an actual source assertion.
        if not assertions or any(not re.match(r'^FAIL .+\.(cpp|h):<line>:', line) for line in assertions):
            raise ValueError('unrecognized SDL assertion output')
        endings = re.findall(r'^sdl_gpu_frame_selfcheck: (\d+) FAILED \(gamma: (\d+) pixels wrong; control differed in (\d+)\)$', output, re.M)
        if len(endings) != 1 or int(endings[0][0]) != len(assertions):
            raise ValueError('missing, changed or incomplete SDL failure completion/count')
        return dict(kind=kind, assertions=dict(sorted(Counter(assertions).items())),
                    failed_assertions=len(assertions), gamma_wrong=int(endings[0][1]), control_differed=int(endings[0][2]))
    if kind == 'widechar_diagnostics':
        wchar = re.findall(r'^widechar_check: FAIL - (\d+) uses of wchar_t text in the POSIX view of \d+ files .*$', output, re.M)
        raw = re.findall(r'^widechar_check: FAIL - (\d+) literals hold raw non-ASCII bytes, .*$', output, re.M)
        offenders = [normalize(line) for line in lines if re.match(r'^(GameEngine|Tests)/', line)]
        # Scan breadth is informational; preserve every offending expression and both failure counts.
        if len(wchar) != 1 or len(raw) != 1 or len(offenders) != int(wchar[0])+int(raw[0]):
            raise ValueError('incomplete widechar diagnostics/counts')
        if len(lines) != len(offenders)+2:
            raise ValueError('unexpected widechar output')
        return dict(kind=kind, wchar_count=int(wchar[0]), raw_literal_count=int(raw[0]),
                    diagnostics=dict(sorted(Counter(offenders).items())))
    if kind == 'exact_diagnostics':
        if not lines:
            raise ValueError('missing stable failure diagnostic')
        return dict(kind=kind, diagnostics=dict(sorted(Counter(normalize(line) for line in lines).items())))
    raise ValueError('unreviewed failure signature kind')


def last_test_outputs(path):
    """CTest's complete per-test output, unaffected by JUnit's output size threshold."""
    if not path.is_file():
        return {}
    text = path.read_text()
    pattern = (r'^\d+/\d+ Testing: ([^\n]+)\n.*?^Output:\n-+\n'
               r'(.*?)^<end of output>\nTest time = .*?\n-+\nTest (Passed|Failed)\.\n')
    result = {}
    for match in re.finditer(pattern, text, re.M | re.S):
        name, output, outcome = match.groups()
        if name in result:
            raise ValueError(f'duplicate LastTest.log entry: {name}')
        result[name] = dict(output=output, failed=outcome == 'Failed')
    return result


def compare(cases, inventory_names, exits, baseline, full_outputs=None):
    full_outputs = full_outputs or {}
    errors, inherited = [], []
    names = [case.get('name', '') for case in cases]
    if not names or len(names) != len(set(names)) or '' in names:
        errors.append('empty, duplicate or unnamed CTest results')
    if len(inventory_names) != len(set(inventory_names)) or set(names) != set(inventory_names):
        errors.append('CTest results do not exactly cover the candidate test inventory')
    missing = sorted(set(baseline['required_tests']) - set(names))
    if missing:
        errors.append('missing baseline tests: '+', '.join(missing))
    if exits.get('ctest') not in (0, 8) or exits.get('tee') != 0:
        errors.append(f'abrupt or invalid raw CTest/tee exit: {exits}')
    skipped, failed, passed = [], [], []
    for case in cases:
        name = case.get('name', '')
        failure, error = case.find('failure'), case.find('error')
        skip = case.find('skipped') is not None or case.get('status') in ('notrun', 'disabled')
        if skip:
            skipped.append(name)
            if name not in baseline['baseline_skipped']:
                errors.append(f'newly skipped required test: {name}')
            if failure is not None or error is not None:
                errors.append(f'inconsistent skipped/failed result: {name}')
            continue
        full = full_outputs.get(name)
        if full is not None and full['failed'] != (failure is not None or error is not None):
            errors.append(f'JUnit/LastTest.log disagree: {name}')
        if failure is None and error is None:
            if case.get('status') not in ('run', 'passed'):
                errors.append(f'unknown or incomplete test result: {name}')
            passed.append(name)
            continue
        failed.append(name)
        expected = baseline['known_failures'].get(name)
        if name in baseline['must_pass'] or expected is None:
            errors.append(f'unapproved failure (must pass): {name}')
            continue
        if error is not None or failure.get('message') != expected['failure_message'] or case.get('status') != 'fail':
            errors.append(f'changed failure mode or exception: {name}')
            continue
        full = full_outputs.get(name)
        if full is not None and not full['failed']:
            errors.append(f'JUnit/LastTest.log disagree: {name}')
            continue
        output = full['output'] if full is not None else case.findtext('system-out', '')
        try:
            actual = signature(output, expected['signature']['kind'])
            if actual != expected['signature']:
                errors.append(f'changed assertion/diagnostic signature or count: {name}')
            else:
                inherited.append(name)
        except ValueError as exc:
            errors.append(f'{name}: {exc}')
    if exits.get('ctest') == 0 and failed:
        errors.append('raw CTest exit 0 contradicts failed test results')
    if exits.get('ctest') == 8 and not failed:
        errors.append('raw CTest exit 8 has no explained failed test')
    return dict(total=len(cases), passed=len(passed), failed=len(failed), skipped=len(skipped),
                failed_tests=failed, skipped_tests=skipped, excluded_tests=[],
                inherited_failures=inherited, regression_gate=dict(accepted=not errors, errors=errors),
                raw_exit=exits, baseline_reference=baseline['reference'],
                baseline_uncharacterized=baseline['uncharacterized_failures'])


def evaluate(reports, build):
    baseline = json.loads(BASELINE_PATH.read_text())
    cases = list(ET.parse(reports/'ctest.xml').getroot().iter('testcase'))
    inventory = json.loads((reports/'test-inventory.json').read_text())
    exits = json.loads((reports/'ctest-exit.json').read_text())
    return compare(cases, [test['name'] for test in inventory['tests']], exits, baseline,
                   last_test_outputs(build/'Testing/Temporary/LastTest.log'))
