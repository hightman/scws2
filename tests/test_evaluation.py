"""Check evaluation metrics independently of external corpora and dictionaries."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('evaluation', ROOT / 'tools/evaluate.py')
evaluation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(evaluation)


class EvaluationTests(unittest.TestCase):
    def test_exact_word_spans_not_matching_strings_or_boundaries(self):
        report = evaluation.score([['我', '爱', '我']], [['我爱', '我']])
        self.assertEqual(report['correct_words'], 1)
        self.assertEqual(report['precision'], 0.5)
        self.assertAlmostEqual(report['recall'], 1 / 3)
        self.assertEqual(report['f1'], 0.4)
        self.assertEqual(report['exact_sentences'], 0)

    def test_metrics_aggregate_counts_instead_of_sentence_averages(self):
        report = evaluation.score([['研究', '生命', '起源'], ['中国']], [['研究生', '命', '起源'], ['中国']])
        self.assertEqual(report['correct_words'], 2)
        self.assertEqual(report['precision'], 0.5)
        self.assertEqual(report['recall'], 0.5)
        self.assertEqual(report['f1'], 0.5)
        self.assertEqual(report['exact_sentences'], 1)
        self.assertEqual(evaluation.score([['甲乙']], [['甲', '乙']])['f1'], 0)

    def test_overlapping_native_tokens_use_original_offsets(self):
        words, spans = evaluation.decode_spans(['AM21B'], b'0:4 2:3\n')
        self.assertEqual(words, [['AM21', '21B']])
        report = evaluation.score_spans([['AM', '21B']], spans)
        self.assertEqual(report['correct_words'], 1)
        self.assertEqual(report['f1'], 0.5)
        self.assertEqual(report['overlapping_sentences'], 1)
        self.assertEqual(report['uncovered_characters'], 0)
        with self.assertRaises(ValueError):
            evaluation.decode_spans(['中文'], b'1:2\n')

    def test_alignment_and_empty_inputs_fail(self):
        for gold, predicted in [([], []), ([[]], [[]]), ([['中国']], []), ([['中国']], [['中华']])]:
            with self.subTest(gold=gold, predicted=predicted):
                with self.assertRaises(ValueError):
                    evaluation.score(gold, predicted)


if __name__ == '__main__':
    unittest.main()
