"""Ponder lifecycle tests using real boards and bounded fake engine readers."""
import contextlib
import io
import subprocess
import sys
from concurrent.futures import Future, ThreadPoolExecutor
from threading import Event
import unittest
from unittest.mock import patch

from cshogi import cli
from cshogi.usi import Engine


class FakeEngine:
    def __init__(self, name, match):
        self.name = name
        self.match = match
        self.proc = None
        self.active = False
        self.release = Event()
        self.moves = []

    def record(self, command):
        self.match.events.append((self.name, command))

    def connect(self, listener=None):
        self.proc = object()

    def setoption(self, *args, **kwargs):
        pass

    def isready(self, listener=None):
        assert not self.active, 'isready raced with a ponder reader'
        self.record('isready')

    def usinewgame(self, listener=None):
        assert not self.active

    def position(self, moves, listener=None):
        assert not self.active, 'position raced with a ponder reader'
        self.moves = list(moves)

    def response(self, listener):
        ply = len(self.moves)
        if ply == self.match.fail_at:
            raise self.match.failure
        move = self.match.moves[ply] if ply < len(self.match.moves) else 'resign'
        predicted = self.match.predictions.get(ply)
        if predicted is None and ply + 1 < len(self.match.moves):
            predicted = self.match.moves[ply + 1]
        if listener:
            listener('info score {} pv {}'.format(
                self.match.scores.get(ply, 'cp 0'), move))
            listener('bestmove ' + move)
        return move, predicted

    def go(self, listener=None, **kwargs):
        assert not self.active
        self.record('go')
        return self.response(listener)

    def _send_go(self, listener=None, **kwargs):
        assert not self.active
        self.active = True
        self.release.clear()
        self.record('go ponder')

    def _read_bestmove(self, listener=None):
        # Deliberately delay reading until stop/ponderhit, even if submitted first.
        if not self.release.wait(3):
            self.active = False
            raise AssertionError('ponder was never stopped or hit')
        try:
            if self.match.reader_error:
                raise EOFError('ponder EOF')
            return self.response(listener)
        finally:
            self.record('collected')
            self.active = False

    def stop(self, listener=None):
        self.record('stop')
        self.release.set()

    def ponderhit(self, listener=None):
        self.record('ponderhit')
        self.release.set()

    def gameover(self, listener=None):
        assert not self.active, 'gameover raced with a ponder reader'
        self.record('gameover')

    def quit(self, listener=None):
        assert not self.active, 'quit raced with a ponder reader'
        self.record('quit')
        self.proc = None


class Match:
    def __init__(self, moves=None):
        self.moves = moves or ['7g7f', '3c3d', '2g2f', '8c8d', 'resign']
        self.events = []
        self.engines = []
        self.pools = []
        self.predictions = {}
        self.scores = {}
        self.fail_at = None
        self.failure = RuntimeError('normal search failed')
        self.reader_error = False

    def engine(self, name, **kwargs):
        engine = FakeEngine(name, self)
        self.engines.append(engine)
        return engine

    def pool(self, **kwargs):
        pool = ThreadPoolExecutor(**kwargs)
        self.pools.append(pool)
        return pool

    def run(self, **kwargs):
        options = dict(ponder=True, print_summary=False)
        options.update(kwargs)
        with patch.object(cli, 'Engine', self.engine), \
                patch.object(cli, 'ThreadPoolExecutor', self.pool), \
                contextlib.redirect_stdout(io.StringIO()):
            return cli.main('black', 'white', **options)


class CliInfoTests(unittest.TestCase):
    def test_mate_without_distance(self):
        for value, expected in [('+', 100000), ('-', -100000)]:
            for pv in ('', ' pv 7g7f'):
                with self.subTest(value=value, pv=pv):
                    self.assertEqual(cli.usi_info_to_score(
                        'info score mate ' + value + pv), expected)

    def test_string_contents_are_not_scores_or_pv(self):
        for line in (
                'info string score mate 3',
                'info depth 1 string score mate 3',
                'info depth 1 string score cp -500 pv 7g7f'):
            with self.subTest(line=line):
                self.assertIsNone(cli.usi_info_to_score(line))
                self.assertIsNone(cli.usi_info_to_csa_comment(cli.Board(), line))

        for suffix, expected in (
                (' string score mate 3 pv invalid', '** 123'),
                (' pv 7g7f 3c3d', '** 123 -3334FU')):
            line = 'info depth 1 score cp 123 nodes 100' + suffix
            with self.subTest(line=line):
                self.assertEqual(cli.usi_info_to_score(line), 123)
                self.assertEqual(cli.usi_info_to_csa_comment(cli.Board(), line), expected)

    def test_terminal_decisions_ignore_diagnostic_text(self):
        original_response = FakeEngine.response

        def response(engine, listener):
            def forward(line):
                if line.startswith('bestmove '):
                    listener('info depth 1 string score mate 3')
                    listener('info depth 1 string score cp -500')
                listener(line)
            return original_response(engine, forward)

        for debug in (False, True):
            for score, options, expected in (
                    ('mate +', {'mate_win': True}, 'engine1_won'),
                    ('mate -', {'resign': 100}, 'engine2_won'),
                    ('cp 0', {'mate_win': True, 'resign': 100}, 'draw')):
                with self.subTest(debug=debug, score=score):
                    match = Match(['7g7f'])
                    match.scores[0] = score
                    with patch.object(FakeEngine, 'response', response):
                        result = match.run(ponder=False, draw=1, debug=debug, **options)
                    self.assertEqual(result[expected], 1)


class CliPonderTests(unittest.TestCase):
    def assert_clean(self, match):
        self.assertTrue(all(not engine.active for engine in match.engines))
        self.assertTrue(all(not thread.is_alive()
                            for pool in match.pools for thread in pool._threads))
        for engine in match.engines:
            events = [command for name, command in match.events if name == engine.name]
            self.assertEqual(events.count('go ponder'), events.count('collected'))

    def test_hit_and_miss_collect_the_correct_engine(self):
        for hit in (True, False):
            with self.subTest(hit=hit):
                match = Match()
                if not hit:
                    match.predictions[0] = '8c8d'
                match.run()
                black = [cmd for name, cmd in match.events if name == 'black']
                self.assertIn('ponderhit' if hit else 'stop', black)
                self.assertEqual(black.count('go'), 1 if hit else 2)
                self.assert_clean(match)

    def test_ponder_starts_before_display_and_skips_draw_position(self):
        match = Match()

        def record_display(value, *args, **kwargs):
            if isinstance(value, cli.Board):
                match.events.append(('display', value.move_number))

        with patch.object(cli, 'print', side_effect=record_display, create=True), \
                patch.object(cli, 'is_jupyter', False):
            result = match.run(is_display=True, draw=2)

        self.assertEqual(result['draw'], 1)
        self.assertLess(match.events.index(('black', 'go ponder')),
                        match.events.index(('display', 2)))
        self.assertIn(('display', 3), match.events)
        self.assertNotIn(('white', 'go ponder'), match.events)
        self.assert_clean(match)

    def test_terminal_paths_collect_before_gameover_and_quit(self):
        scenarios = [
            ('resign', ['7g7f', 'resign'], {}, {}),
            ('threshold', ['7g7f', '3c3d'], {1: 'cp -500'}, {'resign': 100}),
            ('mate', ['7g7f', '3c3d'], {1: 'mate 1'}, {'mate_win': True}),
            ('time', ['7g7f', '3c3d'], {}, {'time': 1000}),
            ('draw', ['7g7f', '3c3d'], {}, {'draw': 2}),
            ('board', ['7g7f', '3c3d'], {}, {}),
            ('illegal', ['7g7f', '7g7f'], {}, {}),
            ('win', ['7g7f', 'win'], {}, {}),
        ]
        for reason, moves, scores, options in scenarios:
            with self.subTest(reason=reason), contextlib.ExitStack() as stack:
                match = Match(moves)
                match.scores = scores
                # Supply a prediction even on the final response: no new ponder
                # should start once the terminal condition is known.
                match.predictions[len(moves) - 1] = '2g2f'
                if reason == 'time':
                    stack.enter_context(patch.object(cli, 'perf_counter', side_effect=[0, 0, 0, 2]))
                if reason == 'board':
                    original = cli.Board

                    def board_factory():
                        board = original()

                        class TerminalBoard:
                            def __getattr__(self, name):
                                return getattr(board, name)

                            def is_game_over(self):
                                return board.move_number >= 3

                        return TerminalBoard()

                    stack.enter_context(patch.object(cli, 'Board', board_factory))
                result = match.run(**options)
                self.assertEqual(result['total'], 1)
                self.assertEqual(sum(cmd == 'go ponder' for _, cmd in match.events), 1)
                black = [cmd for name, cmd in match.events if name == 'black']
                self.assertLess(black.index('go ponder'), black.index('stop'))
                self.assertLess(black.index('stop'), black.index('collected'))
                self.assertLess(black.index('collected'), black.index('gameover'))
                self.assertLess(black.index('gameover'), black.index('quit'))
                self.assert_clean(match)

    def test_repeated_games_with_and_without_process_reuse(self):
        for reuse in (True, False):
            with self.subTest(keep_process=reuse):
                match = Match()
                result = match.run(games=4, keep_process=reuse)
                self.assertEqual(result['total'], 4)
                self.assertEqual(result['engine1_won'], 2)
                self.assertEqual(result['engine2_won'], 2)
                self.assert_clean(match)

    def test_search_exception_preserved_when_cleanup_also_fails(self):
        for reader_error in (True, False):
            with self.subTest(reader_error=reader_error):
                match = Match()
                match.fail_at = 1
                match.reader_error = reader_error
                with self.assertRaises(RuntimeError) as caught:
                    match.run()
                self.assertIs(caught.exception, match.failure)
                self.assert_clean(match)

    def test_ponder_eof_is_reported_and_executor_shuts_down(self):
        match = Match(['7g7f', 'resign'])
        match.reader_error = True
        with self.assertRaisesRegex(EOFError, 'ponder EOF'):
            match.run()
        self.assert_clean(match)

    def test_ponder_eof_is_reported_inside_callers_exception_handler(self):
        match = Match(['7g7f', 'resign'])
        match.reader_error = True
        try:
            raise ValueError('unrelated error handled by caller')
        except ValueError:
            with self.assertRaisesRegex(EOFError, 'ponder EOF'):
                match.run()
        self.assert_clean(match)

    def test_without_ponder(self):
        match = Match()
        result = match.run(ponder=False)
        self.assertEqual(result['engine2_won'], 1)
        self.assertFalse(match.pools)
        self.assertNotIn('go ponder', [cmd for _, cmd in match.events])

    def test_failed_stop_kills_process_and_releases_blocked_reader(self):
        engine = Engine('unused', connect=False)
        process = subprocess.Popen(
            [sys.executable, '-u', '-c',
             "import sys; print('ready', flush=True); sys.stdin.read()"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        engine.proc = process
        failure = BrokenPipeError('debug output closed')

        def listener(line):
            raise failure

        with ThreadPoolExecutor(max_workers=1) as pool:
            try:
                self.assertEqual(process.stdout.readline().strip(), b'ready')
                future = pool.submit(engine._read_bestmove)
                read_result = future.result
                pending = {engine: (future, '3c3d')}
                # Bound a regression so an unsent stop cannot hang the suite.
                with patch.object(future, 'result',
                                  side_effect=lambda: read_result(timeout=3)):
                    with self.assertRaises(BrokenPipeError) as caught:
                        cli._stop_ponder(pending, {engine: listener})
                self.assertIs(caught.exception, failure)
                self.assertIsNotNone(process.poll())
                self.assertIsNone(engine.proc)
                self.assertFalse(pending)
                self.assertTrue(future.done())
                self.assertIsInstance(future.exception(), EOFError)
            finally:
                if process.poll() is None:
                    process.kill()
                process.wait()
                for stream in (process.stdin, process.stdout, process.stderr):
                    stream.close()

    def test_cleanup_stops_every_engine_before_collecting_and_keeps_first_error(self):
        events = []
        failure = EOFError('first reader failed')

        class PendingEngine:
            def __init__(self, name, future):
                self.name, self.future = name, future

            def stop(self, listener=None):
                events.append(self.name)
                if self.name == 'first':
                    self.future.set_exception(failure)
                    raise OSError('stop failed')
                self.future.set_result(None)

        class CheckedFuture(Future):
            def result(self, *args, **kwargs):
                if events != ['first', 'second']:
                    raise AssertionError('readers joined before all stops')
                return super().result(*args, **kwargs)

        first, second = CheckedFuture(), CheckedFuture()
        engines = [PendingEngine('first', first), PendingEngine('second', second)]
        pending = {engines[0]: (first, 'x'), engines[1]: (second, 'y')}
        with self.assertRaisesRegex(OSError, 'stop failed'):
            cli._stop_ponder(pending, dict.fromkeys(engines))
        self.assertEqual(events, ['first', 'second'])
        self.assertFalse(pending)


if __name__ == '__main__':
    unittest.main()
