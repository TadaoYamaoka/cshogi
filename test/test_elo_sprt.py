import math
import unittest

from cshogi.elo import SPRT


class TestSPRT(unittest.TestCase):
    def test_no_pairs(self):
        sprt = SPRT(0, 5)
        self.assertEqual(sprt.counts, (0, 0, 0, 0, 0))
        self.assertEqual(sprt.llr(), 0.0)
        self.assertIsNone(sprt.status())
        lower, upper = sprt.bounds()
        self.assertAlmostEqual(lower, -math.log(19))
        self.assertAlmostEqual(upper, math.log(19))

    def test_fishtest_logistic_llr(self):
        # Reference values from fishtest's LLRcalc.LLR_logistic(0, 5, counts).
        for counts, expected in (
            ((0, 0, 0, 1, 9), 0.142889),
            ((5, 20, 40, 25, 10), 0.374955),
            ((1, 0, 0, 0, 1), -0.000208),
        ):
            with self.subTest(counts=counts):
                sprt = SPRT(0, 5)
                for i, n in enumerate(counts):
                    for _ in range(n):
                        sprt.add_pair(i / 4)
                self.assertEqual(sprt.counts, counts)
                self.assertAlmostEqual(sprt.llr(), expected, places=6)
                self.assertIsNone(sprt.status())

    def test_first_decision_freezes_counts_and_llr(self):
        for score, decision, llr in ((1, 'H1', 0.014316), (0, 'H0', -0.014524)):
            with self.subTest(decision=decision):
                sprt = SPRT(0, 5, alpha=0.499, beta=0.499)
                sprt.add_pair(score)
                self.assertEqual(sprt.status(), decision)
                self.assertAlmostEqual(sprt.llr(), llr, places=6)
                counts = sprt.counts
                self.assertEqual(sum(counts), 1)
                self.assertEqual(counts[4 * score], 1)
                decision_llr = sprt.llr()
                sprt.add_pair(1 - score)
                self.assertEqual(sprt.counts, counts)
                self.assertEqual(sprt.llr(), decision_llr)
                self.assertEqual(sprt.status(), decision)

    def test_reversed_scores_for_symmetric_hypotheses(self):
        forward, reverse = SPRT(-5, 5), SPRT(-5, 5)
        for score in (1, 0.5, 0, 0.75, 1):
            forward.add_pair(score)
            reverse.add_pair(1 - score)
        self.assertEqual(forward.counts, reverse.counts[::-1])
        self.assertAlmostEqual(forward.llr(), -reverse.llr(), places=12)

    def test_only_wins_until_decision(self):
        sprt = SPRT(0, 5)
        for pairs in range(1, 301):
            sprt.add_pair(1)
            self.assertTrue(math.isfinite(sprt.llr()))
            if sprt.status() is not None:
                break
        self.assertEqual(sprt.status(), 'H1')
        self.assertEqual(sprt.counts, (0, 0, 0, 0, pairs))

    def test_extreme_finite_parameters(self):
        sprt = SPRT(-120000, 5, alpha=1e-310, beta=0.05)
        lower, upper = sprt.bounds()
        self.assertAlmostEqual(lower, math.log(0.05))
        self.assertAlmostEqual(upper, 713.7500855, places=7)
        sprt.add_pair(1)
        self.assertTrue(math.isfinite(sprt.llr()))

    def test_invalid_hypotheses(self):
        for hypotheses in ((0, 0), (5, 0), (math.nan, 5), (0, math.inf),
                           (0, 30000), (0, 1e-14), (-128000, 0)):
            with self.subTest(hypotheses=hypotheses), self.assertRaises(ValueError):
                SPRT(*hypotheses)

    def test_invalid_error_probabilities(self):
        for alpha, beta in ((0, 0.05), (0.05, -1), (1, 0.05), (0.05, 1),
                            (0.5, 0.5), (0.7, 0.4), (math.nan, 0.05), (0.05, math.inf)):
            with self.subTest(alpha=alpha, beta=beta), self.assertRaises(ValueError):
                SPRT(0, 5, alpha=alpha, beta=beta)

    def test_invalid_pair_scores_leave_state_unchanged(self):
        sprt = SPRT(0, 5)
        for score in (-0.25, 0.1, 1.25, math.nan, math.inf):
            with self.subTest(score=score), self.assertRaises(ValueError):
                sprt.add_pair(score)
        self.assertEqual(sprt.counts, (0, 0, 0, 0, 0))
        self.assertEqual(sprt.llr(), 0.0)


if __name__ == '__main__':
    unittest.main()
