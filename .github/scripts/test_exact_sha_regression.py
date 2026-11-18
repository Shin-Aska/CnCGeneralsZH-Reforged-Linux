"""Negative controls for the reviewed CTest regression gate (standard library only)."""
import importlib.util
import json
import os
import subprocess
import tempfile
from pathlib import Path
import unittest
import xml.etree.ElementTree as ET

spec = importlib.util.spec_from_file_location('regression', Path(__file__).with_name('exact-sha-regression.py'))
regression = importlib.util.module_from_spec(spec)
spec.loader.exec_module(regression)

NAME = 'sdl_gpu_frame_selfcheck'
OUTPUT = ('FAIL /home/runner/work/project/candidate/GeneralsMD/Code/Tests/test_sdl_gpu_frame.cpp:12: value == expected\n'
          'FAIL /home/runner/work/project/candidate/GeneralsMD/Code/Tests/test_sdl_gpu_frame.cpp:12: value == expected\n'
          'sdl_gpu_frame_selfcheck: 2 FAILED (gamma: 0 pixels wrong; control differed in 3072)\n')


def case(name, output=None, message='Failed', skipped=False):
    result = ET.Element('testcase', name=name, status='notrun' if skipped else 'fail' if output is not None else 'run')
    if skipped:
        ET.SubElement(result, 'skipped')
    elif output is not None:
        ET.SubElement(result, 'failure', message=message)
        ET.SubElement(result, 'system-out').text = output
    return result


class RegressionGate(unittest.TestCase):
    def setUp(self):
        self.baseline = dict(reference={'source_sha':'a'*40,'run_id':'42'}, required_tests=[NAME,'required'],
                             baseline_skipped=['optional'], must_pass=['test_gameengine','test_water_grid'],
                             uncharacterized_failures={'test_water_grid':'no failure diagnostic'},
                             known_failures={NAME:dict(failure_message='Failed',signature=regression.signature(OUTPUT,'sdl_assertions'))})
        self.cases = [case(NAME, OUTPUT),case('required')]

    def check(self, accepted, cases=None, exits=None, inventory=None, full=None):
        cases = self.cases if cases is None else cases
        result = regression.compare(cases, [c.get('name') for c in cases] if inventory is None else inventory,
                                    {'ctest':8,'tee':0} if exits is None else exits, self.baseline, full)
        self.assertEqual(result['regression_gate']['accepted'],accepted,result)
        return result

    def test_exact_known_assertion_count_is_accepted_and_remains_red(self):
        result=self.check(True)
        self.assertEqual(result['failed'],1)
        self.assertEqual(result['inherited_failures'],[NAME])

    def test_line_and_runner_prefix_changes_are_incidental(self):
        output=OUTPUT.replace('/home/runner/work/project/','/tmp/runner42/').replace('.cpp:12:','.cpp:900:')
        self.check(True,[case(NAME,output),case('required')])

    def test_changed_expression_rejected(self):
        self.check(False,[case(NAME,OUTPUT.replace('value == expected','value != expected')),case('required')])

    def test_new_assertion_rejected(self):
        output=OUTPUT.replace('value == expected\n','value == other\n',1)
        self.check(False,[case(NAME,output),case('required')])

    def test_changed_assertion_count_rejected(self):
        self.check(False,[case(NAME,OUTPUT.splitlines(True)[0]+OUTPUT.replace('2 FAILED','3 FAILED')),case('required')])

    def test_missing_assertions_even_with_original_footer_rejected(self):
        self.check(False,[case(NAME,''.join(OUTPUT.splitlines(True)[1:])),case('required')])

    def test_missing_completion_summary_rejected(self):
        self.check(False,[case(NAME,''.join(OUTPUT.splitlines(True)[:-1])),case('required')])

    def test_new_failure_rejected(self):
        self.check(False,[case(NAME,OUTPUT),case('required','unexpected diagnostic')])

    def test_timeout_or_exception_failure_mode_rejected(self):
        for message in ['Timeout','SEGFAULT','Subprocess aborted']:
            self.check(False,[case(NAME,OUTPUT,message),case('required')])
        self.check(False,[case(NAME,OUTPUT+'terminate called after throwing an exception\n'),case('required')])

    def test_abrupt_ctest_or_tee_exit_rejected(self):
        for exits in [{'ctest':124,'tee':0},{'ctest':137,'tee':0},{'ctest':8,'tee':1},{}]:
            self.check(False,exits=exits)

    def test_newly_skipped_required_case_rejected(self):
        self.check(False,[case(NAME,OUTPUT),case('required',skipped=True)])

    def test_newly_added_skipped_test_rejected(self):
        self.check(False,self.cases+[case('new-feature-test',skipped=True)])

    def test_baseline_skip_remains_explicitly_unvalidated(self):
        result=self.check(True,self.cases+[case('optional',skipped=True)])
        self.assertEqual(result['skipped_tests'],['optional'])

    def test_missing_result_duplicate_and_empty_result_rejected(self):
        self.check(False,self.cases[:1],inventory=[NAME,'required'])
        self.check(False,self.cases+[case('required')])
        self.check(False,[])

    def test_missing_original_test_even_if_inventory_drops_it_rejected(self):
        self.check(False,self.cases[:1])

    def test_known_failure_fixed_to_pass_is_accepted(self):
        self.check(True,[case(NAME),case('required')],exits={'ctest':0,'tee':0})

    def test_raw_exit_must_agree_with_results(self):
        self.check(False,exits={'ctest':0,'tee':0})
        self.check(False,[case(NAME),case('required')])

    def test_test_gameengine_and_uncharacterized_water_grid_must_pass(self):
        for name in ['test_gameengine','test_water_grid']:
            self.check(False,self.cases+[case(name,'normal renderer teardown only')])

    def test_truncated_junit_needs_complete_last_test_output(self):
        truncated='[This part of the test output was removed since it exceeds the threshold of 307200 bytes.]'
        cases=[case(NAME,truncated),case('required')]
        self.check(False,cases)
        self.check(True,cases,full={NAME:{'output':OUTPUT,'failed':True}})

    def test_junit_cannot_hide_failure_recorded_in_complete_log(self):
        self.check(False,[case(NAME),case('required')],exits={'ctest':0,'tee':0},
                   full={NAME:{'output':OUTPUT,'failed':True}})

    def test_exact_diagnostics_and_widechar_counts_are_not_blanket_ignored(self):
        a=regression.signature('lobbyroom: Map.wnd must be regenerated\n','exact_diagnostics')
        b=regression.signature('lobbyroom: Other.wnd must be regenerated\n','exact_diagnostics')
        self.assertNotEqual(a,b)
        output=("widechar_check: FAIL - 1 uses of wchar_t text in the POSIX view of 20 files (description):\n"
                "  Tests/a.cpp: view line 12: wide character literal: L'x'\n"
                "widechar_check: FAIL - 1 literals hold raw non-ASCII bytes, description:\n"
                "  Tests/a.cpp:20: 'raw'\n")
        reference=regression.signature(output,'widechar_diagnostics')
        self.assertEqual(reference,regression.signature(output.replace('20 files','25 files'),'widechar_diagnostics'))
        with self.assertRaises(ValueError):
            regression.signature(output.replace('1 uses','2 uses'),'widechar_diagnostics')
        self.assertNotEqual(reference,regression.signature(output.replace("L'x'","L'y'"),'widechar_diagnostics'))


class HandoffLabels(unittest.TestCase):
    def fixture(self, folder, accepted):
        root=Path(folder)
        (root/'candidate').mkdir(); (root/'candidate/LICENSE.md').write_text('fixture license')
        (root/'build/overlay/Data').mkdir(parents=True)
        (root/'build/overlay/Data/Patch.str').write_text('fixture overlay')
        for name in ('generals','test_water_grid'):
            (root/'build'/name).write_text('exact fixture binary '+name)
            (root/'build'/name).chmod(0o755)
        reports=root/'reports';reports.mkdir()
        for name in ('provenance.json','vendor-hashes.json','ctest-exit.json','regression-baseline.json'):
            (reports/name).write_text('{}')
        (reports/'vendor-code-only.patch').write_text('fixture patch')
        (reports/'test-summary.json').write_text(json.dumps({'regression_gate':{'accepted':accepted},'failed_tests':['test_water_grid']}))
        return os.environ | dict(GITHUB_WORKSPACE=folder,RUNNER_TEMP=folder,CANDIDATE_SHA='f'*40)

    def run_stage(self, env, mode):
        return subprocess.run(['python3',str(Path(__file__).with_name('exact-sha-validation.py')),mode],env=env,capture_output=True,text=True)

    def test_rejected_gate_cannot_export_validated_binary(self):
        with tempfile.TemporaryDirectory() as folder:
            env=self.fixture(folder,False)
            self.assertNotEqual(self.run_stage(env,'stage').returncode,0)
            self.assertFalse((Path(folder)/'handoff').exists())

    def test_accepted_handoff_contains_overlay_and_provenance(self):
        with tempfile.TemporaryDirectory() as folder:
            env=self.fixture(folder,True);result=self.run_stage(env,'stage')
            self.assertEqual(result.returncode,0,result.stderr)
            root=next(p for p in (Path(folder)/'handoff').glob('linux-validation-*') if p.is_dir())
            self.assertEqual((root/'bin/generals').read_bytes(),(Path(folder)/'build/generals').read_bytes())
            self.assertTrue((root/'overlay/Data/Patch.str').is_file())
            self.assertFalse((root/'tests').exists())
            self.assertEqual(json.loads((root/'provenance.json').read_text())['artifact_verification'],'regression-gate-accepted')

    def test_rejected_handoff_is_explicitly_unverified_and_keeps_exact_test_binary(self):
        with tempfile.TemporaryDirectory() as folder:
            env=self.fixture(folder,False);result=self.run_stage(env,'diagnostic-stage')
            self.assertEqual(result.returncode,0,result.stderr)
            root=next(p for p in (Path(folder)/'unverified-handoff').glob('linux-UNVERIFIED-diagnostic-*') if p.is_dir())
            self.assertTrue((root/'UNVERIFIED.txt').is_file())
            self.assertEqual((root/'tests/test_water_grid').read_bytes(),(Path(folder)/'build/test_water_grid').read_bytes())
            self.assertEqual(json.loads((root/'provenance.json').read_text())['artifact_verification'],'UNVERIFIED-regression-gate-rejected')
            self.assertFalse((Path(folder)/'handoff').exists())


if __name__ == '__main__':
    unittest.main()
