"""Synthetic inputs are unit-test fixtures, never board measurements."""
import sys
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from collect_log import parse_row
from summarize_results import summarize, percentile95

class ToolTests(unittest.TestCase):
    def row(self, event, tick, generation='1'):
        return dict(run_id='SYNTHETIC', version='V2', test_id='2', task_id='1', event_id='99',
                    event=event, tick=str(tick), generation=generation, reason='0', value='0', cycles='0')

    def test_parser_rejects_malformed(self):
        self.assertIsNone(parse_row('bad,line'))
        self.assertIsNotNone(parse_row('demo,M5_ONLY,2,12,99,1,1,INJECT,0,0,0'))
        self.assertIsNone(parse_row('demo,M5_ONLY,2,-12,99,1,1,INJECT,0,0,0'))

    def test_correlation_and_new_generation(self):
        rows = [self.row('INJECT', 100), self.row('FAULT_DETECTED', 150),
                self.row('FIRST_PROGRESS', 180, '2'), self.row('RECOVERY_OK', 185, '2')]
        result = summarize(rows)[0]
        self.assertEqual(result['detection_ticks'], 50)
        self.assertEqual(result['recovery_ticks'], 30)

    def test_missing_evidence_stays_empty(self):
        result = summarize([self.row('RECOVERY_OK', 100, '2')])[0]
        self.assertIsNone(result['detection_ticks'])
        self.assertIsNone(result['recovery_ticks'])

    def test_stale_progress_cannot_verify(self):
        result = summarize([self.row('FAULT_DETECTED', 100), self.row('FIRST_PROGRESS', 120),
                            self.row('RECOVERY_OK', 130, '2')])[0]
        self.assertIsNone(result['recovery_ticks'])

    def test_duplicate_evidence_not_guessed(self):
        result = summarize([self.row('INJECT', 100), self.row('FAULT_DETECTED', 110), self.row('FAULT_DETECTED', 120)])[0]
        self.assertIsNone(result['detection_ticks'])

    def test_wrap_and_p95(self):
        result = summarize([self.row('INJECT', 0xFFFFFFF0), self.row('FAULT_DETECTED', 10)])[0]
        self.assertEqual(result['detection_ticks'], 26)
        self.assertEqual(percentile95(list(range(1, 31))), 29)

if __name__ == '__main__':
    unittest.main()
