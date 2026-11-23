import contextlib
import io
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from threading import Event
from time import monotonic
from unittest.mock import patch

from cshogi import cli
from test.test_cli_ponder import FakeEngine, Match


def run_cli(match, *args):
    output = io.StringIO()
    with patch.object(cli, 'Engine', match.engine), \
            contextlib.redirect_stdout(output):
        cli._cli(list(map(str, args)))
    return output.getvalue()


class SequentialMatchTests(unittest.TestCase):
    def test_counts_colors_and_callback_snapshot(self):
        calls = []

        def callback(stats):
            calls.append(stats)
            if len(calls) < 3:
                return True

        match = Match(['resign'])
        result = match.run(games=6, ponder=False, callback=callback)
        self.assertEqual([s['total'] for s in calls], [1, 2, 3])
        self.assertEqual(result, dict(engine1_name='black', engine2_name='white',
                                      engine1_won=1, engine2_won=2, black_won=0,
                                      white_won=3, draw=0, total=3))
        self.assertEqual(calls[-1], result)

    def test_color_swap_preserves_options_and_clocks_with_identical_names(self):
        options, searches = [], []
        match = Match(['resign'])
        with patch.object(FakeEngine, 'setoption',
                          lambda e, k, v, **kw: options.append((match.engines.index(e), k, v))), \
                patch.object(FakeEngine, 'go',
                             lambda e, listener, **kw: (searches.append(kw) or ('resign', None))):
            match.run(games=2, ponder=False, names=['same', 'same'],
                      options1={'gpu': 'x'}, options2={'gpu': 'y'},
                      time=[10, 20], inc=[1, 2], byoyomi=[11, 22])
        self.assertEqual(options, [(0, 'gpu', 'x'), (1, 'gpu', 'y'),
                                   (1, 'gpu', 'y'), (0, 'gpu', 'x')])
        self.assertEqual(searches, [dict(btime=10, wtime=20, binc=1, winc=2, byoyomi=11),
                                    dict(btime=20, wtime=10, binc=2, winc=1, byoyomi=22)])

    def test_white_sfen_opening_and_timed_search_in_csa(self):
        board = cli.Board()
        board.push_usi('7g7f')
        match = Match(['3c3d', '2g2f', 'resign'])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'book').write_text('sfen ' + board.sfen() + ' moves 3c3d')
            with patch.object(cli, 'perf_counter', return_value=1):
                match.run(opening=path / 'book', csa=directory, ponder=False)
            record = next(path.glob('*.csa')).read_text()
        self.assertIn('\n-\n-3334FU\n+2726FU\nT0\n', record)
        self.assertTrue(record.endswith("'** 0\n%TORYO\n"))

    def test_league_odd_count_rounds_swap_and_process_lifetime(self):
        match = Match(['resign'])
        with tempfile.TemporaryDirectory() as directory:
            pgn = Path(directory) / 'league.pgn'
            output = run_cli(match, 'A', 'B', 'C', '--games', 1, '--no-swap',
                             '--keep-process', '--pgn', pgn)
            tags = pgn.read_text()
        self.assertEqual(re.findall(r'\[Round "(\d+)"\]', tags), ['1', '2'] * 3)
        self.assertEqual(re.findall(r'\[White "(.*?)"\]', tags), list('ABACBC'))
        self.assertEqual(output.count('games finished.'), 1)
        self.assertIn('2 of 1 games finished.', output)
        self.assertEqual(len(match.engines), 6)
        self.assertEqual(sum(cmd == 'quit' for _, cmd in match.events), 6)


class ParallelMatchTests(unittest.TestCase):
    def test_out_of_order_summary_and_csa_append(self):
        match = Match(['resign'])
        first, third = Event(), Event()
        play_game = cli.play_game
        report = cli.Match.report

        def ordered(spec, *args):
            if spec.index == 2:
                self.assertTrue(first.wait(3))
            if spec.index % 2:
                self.assertTrue(third.wait(3))
            return play_game(spec, *args)

        def reported(state, result, *args):
            report(state, result, *args)
            if result.spec.index == 0:
                first.set()
            if result.spec.index == 2:
                third.set()

        with tempfile.TemporaryDirectory() as directory:
            csa = Path(directory) / 'record.csa'
            pgn = Path(directory) / 'record.pgn'
            match.run(ponder=False, csa=csa, multi_csa=True)
            with patch.object(cli, 'play_game', ordered), patch.object(cli.Match, 'report', reported):
                result = match.run(ponder=False, print_summary=True, concurrency=2, games=4,
                                   csa=csa, multi_csa=True, pgn=pgn)
            self.assertEqual(len(cli.CSA.Parser.parse_file(str(csa))), 5)
            rounds = re.findall(r'\[Round "(\d+)"\]', pgn.read_text())
        self.assertEqual(rounds[:2], ['1', '3'])
        self.assertEqual(sorted(rounds), list('1234'))
        self.assertEqual(result['total'], 4)

    def test_callback_stops_new_games_and_drains_started_games(self):
        match = Match(['resign'])
        started, stopped = Event(), Event()
        play_game = cli.play_game
        calls = []

        def ordered(spec, *args):
            if spec.index == 0:
                self.assertTrue(started.wait(3))
            else:
                started.set()
                self.assertTrue(stopped.wait(3))
            return play_game(spec, *args)

        def stop(stats):
            calls.append(stats['total'])
            stopped.set()

        with tempfile.TemporaryDirectory() as directory, patch.object(cli, 'play_game', ordered):
            pgn = Path(directory) / 'games'
            result = match.run(concurrency=2, games=10, callback=stop, pgn=pgn)
            self.assertEqual(pgn.read_text().count('[Result '), 2)
        self.assertEqual(calls, [1])
        self.assertEqual(result['total'], 2)

    def test_worker_failure_kills_ponder_and_drains_other_worker(self):
        other_started, killed = Event(), Event()
        failure = RuntimeError('search failed')
        play_game = cli.play_game
        match = Match()
        go = FakeEngine.go

        def play(spec, players, *args):
            for player in players.values():
                player.engine.failing = spec.index == 0
            if spec.index == 0:
                self.assertTrue(other_started.wait(3))
            else:
                other_started.set()
                self.assertTrue(killed.wait(3))
            return play_game(spec, players, *args)

        def search(engine, **kwargs):
            if engine.failing and engine.name == 'white':
                raise failure
            return go(engine, **kwargs)

        def kill(engine):
            killed.set()
            engine.release.set()

        def stop(engine, **kwargs):
            if not engine.failing:
                engine.release.set()

        with tempfile.TemporaryDirectory() as directory, patch.object(cli, 'play_game', play), \
                patch.object(FakeEngine, 'go', search), patch.object(FakeEngine, 'kill', kill), \
                patch.object(FakeEngine, 'stop', stop):
            pgn = Path(directory) / 'games'
            with self.assertRaises(RuntimeError) as caught:
                match.run(concurrency=2, games=8, pgn=pgn)
            self.assertEqual(caught.exception, failure)
            self.assertEqual(pgn.read_text().count('[Result '), 1)
        self.assertTrue(all(e.proc is None and not e.active for e in match.engines))

    def test_worker_options_and_input_validation(self):
        match = Match(['resign'])
        events = []
        with patch.object(FakeEngine, 'setoption',
                          lambda e, k, v, **kw: events.append((e, k, v))):
            run_cli(match, 'A', 'B', '--games', 10, '--concurrency', 3,
                    '--options2', 'model:new,threads:3', '--worker-options2',
                    'threads:2', 'threads:0,threads2:2', 'threads:1')
        for engine in match.engines:
            sent = [(k, v) for e, k, v in events if e is engine]
            if engine.name == 'B':
                self.assertEqual(sent[0], ('model', 'new'))
                self.assertIn(sent[1], [('threads', '2'), ('threads', '0'), ('threads', '1')])
                if sent[1][1] == '0':
                    self.assertEqual(sent[2], ('threads2', '2'))
            else:
                self.assertEqual(sent, [])
        for kwargs in (dict(concurrency=0), dict(concurrency=2, is_display=True),
                       dict(concurrency=2, worker_options=[None, [{}]])):
            match = Match()
            with self.assertRaises(ValueError):
                match.run(**kwargs)
            self.assertEqual(match.engines, [])


class ProcessShutdownTests(unittest.TestCase):
    # The deadline in the child also bounds a regression in the parent's shutdown order.
    script = """
import os, sys, threading
threading.Timer(8, lambda: os._exit(9)).start()
mode, name = sys.argv[1:]
for command in sys.stdin:
    command = command.strip()
    if command == 'usi' and mode != 'handshake':
        print('id name ' + name)
        print('usiok', flush=True)
    elif command == 'isready':
        print('readyok', flush=True)
    elif command == 'quit':
        os._exit(0)
    elif command.startswith('go') and mode == 'callback':
        print('bestmove resign', flush=True)
    elif command.startswith('go') and 'ponder' not in command and name == 'black':
        print('bestmove 7g7f ponder 3c3d', flush=True)
"""

    def run_shutdown(self, mode):
        processes = []
        waiting = Event()
        handshake = cli.Engine._handshake
        reader = cli.Engine._read_bestmove

        def spawn(engine):
            engine.proc = subprocess.Popen(
                [sys.executable, '-u', '-c', self.script, mode, engine.cmd],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            processes.append(engine.proc)

        def handshake_wait(engine, listener):
            if mode == 'handshake':
                waiting.set()
            handshake(engine, listener)

        def read(engine, listener):
            if mode == 'ponder' and engine.pondering:
                waiting.set()
            return reader(engine, listener)

        send = cli.Engine._send_go

        def send_go(engine, ponder=False, *args, **kwargs):
            engine.pondering = ponder
            return send(engine, ponder, *args, **kwargs)

        def interrupt(*args, **kwargs):
            self.assertTrue(waiting.wait(4))
            raise KeyboardInterrupt

        failure = RuntimeError('callback failed')

        def callback(stats):
            raise failure

        with contextlib.ExitStack() as stack:
            for name, replacement in [('_spawn', spawn), ('_handshake', handshake_wait),
                                      ('_read_bestmove', read), ('_send_go', send_go)]:
                stack.enter_context(patch.object(cli.Engine, name, replacement))
            stack.enter_context(contextlib.redirect_stdout(io.StringIO()))
            if mode != 'callback':
                stack.enter_context(patch.object(cli, 'wait', interrupt))
            start = monotonic()
            with self.assertRaises(RuntimeError if mode == 'callback' else KeyboardInterrupt) as caught:
                cli.main('black', 'white', concurrency=2, games=4, keep_process=True,
                         ponder=mode == 'ponder', callback=callback)
            if mode == 'callback':
                self.assertIs(caught.exception, failure)
            self.assertLess(monotonic() - start, 7)
        self.assertTrue(processes)
        self.assertTrue(all(p.poll() is not None for p in processes))

    def test_interrupt_and_callback_cleanup(self):
        for mode in ('handshake', 'ponder', 'callback'):
            with self.subTest(mode=mode):
                self.run_shutdown(mode)
