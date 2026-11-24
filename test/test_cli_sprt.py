import io
import unittest
from contextlib import redirect_stdout
from unittest.mock import Mock

from cshogi import cli


class TestMatchSprt(unittest.TestCase):
    def setUp(self):
        self.output = io.StringIO()
        capture = redirect_stdout(self.output)
        capture.__enter__()
        self.addCleanup(capture.__exit__, None, None, None)

    def report(self, match, spec, winner_id):
        colors = (spec.black_id, spec.white_id)
        winner = None if winner_id is None else colors.index(winner_id)
        names = tuple('ABC'[engine] for engine in colors)
        result = cli.GameResult(spec, names, winner, '', f'game {spec.index}', ())
        match.report(result, None, '')

    def test_out_of_order_pairs_and_callback_snapshots(self):
        snapshots = []

        def callback(stats):
            snapshots.append(stats)
            return True

        sprt = cli.MatchSprt(0, 5, 0.499, 0.499, (1, 2))
        match = cli.Match(Mock(), 6, False, callback, sprt=sprt)
        first, second, third = cli.schedule(6, [('startpos', ())], False)
        for spec in second:
            self.report(match, spec, 1)
        for spec in first:
            self.report(match, spec, 0)
        self.assertFalse(match.try_start())
        self.report(match, third[0], None)  # Already started at decision time.
        self.assertEqual([s['sprt']['pairs'] for s in snapshots], [0, 0, 0, 1])
        self.assertEqual(snapshots[-1]['sprt']['result'], 'H1')
        snapshots[-1]['sprt']['result'] = None
        final = match.snapshot((0, 1))
        self.assertEqual(final['total'], 5)
        self.assertEqual(final['sprt']['result'], 'H1')
        self.assertEqual(final['sprt']['pentanomial'], (0, 0, 0, 0, 1))
        self.assertEqual(match.records.finish.call_count, 5)
        self.assertEqual(self.output.getvalue().count('accepted after 1 pair'), 1)

    def test_callback_stop_freezes_incomplete_pair(self):
        callback = Mock(side_effect=[True, False])
        sprt = cli.MatchSprt(0, 5, 0.499, 0.499, (1, 2))
        match = cli.Match(Mock(), 4, False, callback, sprt=sprt)
        first, second = cli.schedule(4, [('startpos', ())], False)
        self.report(match, first[0], 0)
        self.report(match, second[0], 0)
        self.assertFalse(match.try_start())
        self.report(match, first[1], 0)  # Would accept H1 if added.
        self.assertEqual(callback.call_count, 2)
        self.assertEqual(match.snapshot((0, 1))['total'], 3)
        self.assertIsNone(sprt.snapshot()['result'])
        self.assertEqual(sprt.snapshot()['pairs'], 0)

    def test_league_returns_games_from_an_incomplete_stage(self):
        sprt = cli.MatchSprt(0, 5, 0.499, 0.499, (3, 2))
        match = cli.Match(Mock(), 4, False, None, cli.LeagueTable(), sprt)
        ab, ac, bc, next_ab, *_ = cli.schedule(4, [('startpos', ())], False, league=True)
        for spec in (*ab, *ac, *bc):
            self.report(match, spec, 2 if spec in bc else None)
        self.assertFalse(match.try_start())
        self.report(match, next_ab[0], None)
        match.publish_standings()
        results = [match.snapshot(pair) for pair in cli.LEAGUE_PAIRS]
        self.assertEqual([r['total'] for r in results], [3, 2, 2])
        self.assertNotIn('sprt', results[0])
        self.assertNotIn('sprt', results[1])
        self.assertEqual(results[2]['engine2_won'], 2)
        self.assertEqual(results[2]['sprt']['engines'], (3, 2))
        self.assertEqual(results[2]['sprt']['pentanomial'], (0, 0, 0, 0, 1))
        output = self.output.getvalue()
        self.assertEqual(output.count('games finished.'), 1)
        self.assertIn('game 5\nSPRT: C vs B H1 accepted after 1 pair', output)
        self.assertIn('pentanomial 0-0-0-0-1', output)
