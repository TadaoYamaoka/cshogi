import io
import re
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from threading import Event
from unittest.mock import Mock, patch

from cshogi import cli
if __package__:
    from .test_cli_ponder import Match as EngineFixture
else:
    from test_cli_ponder import Match as EngineFixture


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
        match = cli.Match(Mock(), 6, True, callback, sprt=sprt)
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


class TestSprtInterface(unittest.TestCase):
    def setUp(self):
        # A plays the first move as Black; its opponent always resigns.
        candidate = EngineFixture(['7g7f', 'resign'])
        opponent = EngineFixture(['resign'])
        def engine(name, **kwargs):
            return (candidate if name == 'A' else opponent).engine(name, **kwargs)

        engines = patch.object(cli, 'Engine', side_effect=engine)
        self.engines = engines.start()
        self.addCleanup(engines.stop)

    def test_game_limit_and_summary_output(self):
        for summary in (True, False):
            with self.subTest(summary=summary), redirect_stdout(io.StringIO()) as output:
                stats = cli.main('A', 'B', games=2, sprt=cli.SprtConfig(0, 5), print_summary=summary)
                self.assertEqual(stats['engine1_won'], 2)
                sprt = stats['sprt']
                self.assertEqual((sprt['engines'], sprt['pairs'], sprt['result']), ((1, 2), 1, None))
                self.assertAlmostEqual(sprt['llr'], 0.014316, places=6)
                self.assertEqual(output.getvalue().count('no decision after 1 pair (game limit reached)'),
                                 int(summary))
                if not summary:
                    self.assertNotIn('SPRT:', output.getvalue())

    def test_quiet_decision_and_callback_stop(self):
        config = cli.SprtConfig(0, 5, alpha=0.499, beta=0.499)
        with redirect_stdout(io.StringIO()) as output:
            result = cli.main('A', 'B', games=4, sprt=config, print_summary=False)
        self.assertEqual((result['total'], result['sprt']['result']), (2, 'H1'))
        self.assertNotIn('SPRT:', output.getvalue())
        with redirect_stdout(io.StringIO()) as output:
            result = cli.main('A', 'B', games=4, sprt=config, callback=lambda _: False)
        self.assertEqual((result['total'], result['sprt']['pairs'], result['sprt']['result']), (1, 0, None))
        self.assertNotIn('game limit reached', output.getvalue())

    def test_reversing_test_engines_for_symmetric_hypotheses(self):
        def run(engines):
            config = cli.SprtConfig(-5, 5, 0.499, 0.499, engines=engines)
            return cli.main('A', 'B', games=4, sprt=config)['sprt']

        with redirect_stdout(io.StringIO()):
            forward, reverse = run((1, 2)), run((2, 1))
        self.assertEqual((forward['result'], reverse['result']), ('H1', 'H0'))
        self.assertEqual(forward['pentanomial'], reverse['pentanomial'][::-1])
        self.assertAlmostEqual(forward['llr'], -reverse['llr'], places=12)

    def test_zero_games(self):
        with redirect_stdout(io.StringIO()) as output:
            stats = cli.main('A', 'B', games=0, sprt=cli.SprtConfig(0, 5))
        sprt = stats['sprt']
        self.assertEqual((stats['total'], sprt['pairs'], sprt['llr'], sprt['result']), (0, 0, 0.0, None))
        self.assertEqual(output.getvalue().count('no decision after 0 pairs (game limit reached)'), 1)
        self.engines.assert_not_called()

    def test_league_cli_decision_before_standings(self):
        args = 'A B C --games 4 --sprt 0 5 --sprt-engines 2 1 --sprt-alpha .499 --sprt-beta .499'
        with redirect_stdout(io.StringIO()) as output:
            cli._cli(args.split())
        self.assertEqual(output.getvalue().count('SPRT: B vs A H0 accepted after 1 pair'), 1)
        self.assertNotIn('games finished.', output.getvalue())
        self.assertNotIn('game limit reached', output.getvalue())

    def test_invalid_cli_before_start(self):
        for args, message in (
            ('A B --sprt 0 5', 'SPRT requires an even --games value'),
            ('A B --games 2 --sprt 0 5 --no-swap', '--no-swap'),
            ('A B C --games 2 --sprt 0 5', 'requires --sprt-engines'),
            ('A B --games 2 --sprt 0 5 --sprt-engines 0 2', 'distinct engine numbers'),
            ('A B --games 2 --sprt 0 5 --sprt-engines 1 3', 'distinct engine numbers'),
            ('A B --games 2 --sprt 0 5 --sprt-engines 1 1', 'distinct engine numbers'),
            ('A B --games 2 --sprt 0 nan', 'finite'),
            ('A B --games 2 --sprt 5 0', 'distinct increasing scores'),
            ('A B --games 2 --sprt 0 5 --sprt-alpha .6 --sprt-beta .4', 'alpha + beta'),
            ('A B --games 2 --sprt-alpha .1', 'require --sprt'),
        ):
            with self.subTest(args=args), redirect_stderr(io.StringIO()) as stderr:
                with self.assertRaises(SystemExit) as error:
                    cli._cli(args.split())
                self.assertEqual(error.exception.code, 2)
                self.assertIn(message, stderr.getvalue().split('error: ')[-1])
        self.engines.assert_not_called()

    def test_invalid_engine_selection_before_start(self):
        for engines in (3, (1,), (1, 2, 3)):
            with self.subTest(engines=engines), self.assertRaisesRegex(
                    cli._MatchConfigError, 'two distinct engine numbers between 1 and 2'):
                cli.main('A', 'B', games=2, sprt=cli.SprtConfig(0, 5, engines=engines))
        self.engines.assert_not_called()


class TestParallelSprt(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.directory = Path(directory.name)
        book = self.directory / 'openings.sfen'
        book.write_text('startpos\nstartpos moves 7g7f 3c3d\n')
        self.options = dict(games=6, opening=book, opening_seed=0)
        # On the first opening A wins both colors; on the second it loses both.
        candidate = EngineFixture(['7g7f', 'resign'])
        opponent = EngineFixture(['resign', 'resign', '2g2f', 'resign'])

        def engine(name, **kwargs):
            return (candidate if name == 'A' else opponent).engine(name, **kwargs)

        engines = patch.object(cli, 'Engine', side_effect=engine)
        engines.start()
        self.addCleanup(engines.stop)

    def hold_until_stop(self, waiting, in_flight):
        """Let the waiting game proceed once the other worker has started in_flight."""
        started = Event()
        play_game = cli.Worker.play_game

        def play(worker, spec, stack):
            if spec.index == waiting:
                self.assertTrue(started.wait(5), f'game {in_flight} did not start')
            elif spec.index == in_flight:
                started.set()
                self.assertTrue(worker.match.stopped.wait(5), 'match did not stop')
            return play_game(worker, spec, stack)

        return patch.object(cli.Worker, 'play_game', play)

    def test_later_losing_pair_finishes_before_winning_pair(self):
        config = cli.SprtConfig(0, 5, alpha=0.499, beta=0.499)
        pgn = self.directory / 'parallel.pgn'
        seen = []

        def callback(stats):
            seen.append(stats['sprt'])
            return True

        with redirect_stdout(io.StringIO()):
            serial = cli.main('A', 'B', sprt=config, **self.options)
            with self.hold_until_stop(waiting=0, in_flight=4):
                parallel = cli.main('A', 'B', sprt=config, concurrency=2, callback=callback,
                                    pgn=pgn, **self.options)
        records = pgn.read_text()
        self.assertEqual(re.findall(r'\[Round "(\d+)"\]', records), ['3', '4', '1', '2', '5'])
        self.assertEqual(re.findall(r'\[Result "([^"]+)"\]', records), ['0-1', '1-0', '1-0', '0-1', '1-0'])
        self.assertEqual(serial['total'], 2)
        self.assertEqual((parallel['total'], parallel['engine1_won'], parallel['engine2_won']), (5, 3, 2))
        self.assertEqual([s['result'] for s in seen], [None, None, None, 'H1'])
        self.assertEqual(seen[-1], parallel['sprt'])
        self.assertEqual(parallel['sprt'], serial['sprt'])
        self.assertEqual((serial['sprt']['pairs'], serial['sprt']['pentanomial']), (1, (0, 0, 0, 0, 1)))

    def test_callback_stop_precedes_winning_pair(self):
        config = cli.SprtConfig(0, 5, alpha=0.499, beta=0.499)
        seen = []

        def callback(stats):
            seen.append(stats['sprt'])
            return stats['total'] < 2

        with redirect_stdout(io.StringIO()), self.hold_until_stop(waiting=2, in_flight=1):
            stats = cli.main('A', 'B', sprt=config, concurrency=2, callback=callback, **self.options)
        self.assertEqual((stats['total'], stats['engine1_won']), (3, 2))
        self.assertEqual(len(seen), 2)
        self.assertEqual(stats['sprt'], seen[-1])
        self.assertIsNone(stats['sprt']['result'])
        self.assertEqual(stats['sprt']['pairs'], 0)

    def test_interrupt_during_recording_skips_callback(self):
        writing, stopped = Event(), Event()
        finish = cli.Records.finish
        pgn = self.directory / 'interrupted.pgn'
        callback = Mock()

        def write_record(records, result, csa_record):
            writing.set()
            self.assertTrue(stopped.wait(5))
            finish(records, result, csa_record)

        def interrupt(*args, **kwargs):
            self.assertTrue(writing.wait(5))
            raise KeyboardInterrupt

        # Both engines have quit before Records.finish; close signals that
        # the main thread has handled the interrupt and set the stop event.
        with redirect_stdout(io.StringIO()), patch.object(cli, 'wait', interrupt), \
                patch.object(cli.Records, 'finish', write_record), \
                patch.object(cli.ProcessGroup, 'close', side_effect=stopped.set):
            with self.assertRaises(KeyboardInterrupt):
                cli.main('A', 'B', concurrency=2, games=2, callback=callback, pgn=pgn)
        self.assertEqual(pgn.read_text().count('[Result '), 1)
        callback.assert_not_called()

    def test_league_stops_with_later_stage_in_flight(self):
        config = cli.SprtConfig(0, 5, alpha=0.499, beta=0.499, engines=(3, 2))
        with redirect_stdout(io.StringIO()):
            serial = cli.league('C', 'B', 'A', sprt=config, **self.options)
        self.assertEqual([r['total'] for r in serial], [2, 2, 2])
        decision = serial[2]['sprt']
        self.assertEqual((decision['result'], decision['pairs'], decision['pentanomial']),
                         ('H1', 1, (0, 0, 0, 0, 1)))

        # Hold the last game of stage 1 until stage 2 or stage 3 starts.
        for in_flight, stages in ((6, 1), (12, 2)):
            with self.subTest(in_flight=in_flight), redirect_stdout(io.StringIO()) as output:
                pgn = self.directory / f'league-{in_flight}.pgn'
                with self.hold_until_stop(waiting=5, in_flight=in_flight):
                    results = cli.league('C', 'B', 'A', concurrency=2, pgn=pgn, sprt=config, **self.options)
                self.assertEqual(results[2]['sprt'], decision)
                self.assertEqual([r['total'] for r in results], [2 * stages + 1, 2 * stages, 2 * stages])
                self.assertEqual(pgn.read_text().count('[Event '), in_flight + 1)
                self.assertEqual(output.getvalue().count('games finished.'), stages)
                self.assertEqual(output.getvalue().count('SPRT: A vs B H1 accepted after 1 pair'), 1)
