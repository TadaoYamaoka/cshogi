import contextlib
import io
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from threading import Barrier, Event
from time import monotonic
from unittest.mock import patch

from cshogi import cli
if __package__:
    from .test_cli_ponder import FakeEngine, Match
else:
    from test_cli_ponder import FakeEngine, Match


def run_cli(match, *args):
    output = io.StringIO()
    with patch.object(cli, 'Engine', match.engine), \
            contextlib.redirect_stdout(output):
        cli._cli(list(map(str, args)))
    return output.getvalue()


class SequentialMatchTests(unittest.TestCase):
    def test_individual_csa_is_closed_once_before_callback(self):
        class Record(io.StringIO):
            closes = 0

            def close(self):
                self.closes += 1
                super().close()

        record = Record()

        def callback(stats):
            self.assertTrue(record.closed)

        with patch.object(cli, 'open', return_value=record):
            Match(['resign']).run(csa='records', callback=callback)
        self.assertEqual(record.closes, 1)

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

    def test_csa_endings_and_pgn_results(self):
        cases = [
            ('startpos', ['resign'], {}, '%TORYO', '0-1'),
            ('startpos', ['win'], {}, '%ILLEGAL_MOVE', '0-1'),
            ('startpos', ['7g7f'], {'draw': 1}, '%JISHOGI', '1/2-1/2'),
            ('startpos', ['7g7f'], {'time': 0}, '%TIME_UP', '0-1'),
            ('startpos', '5i5h 5a5b 5h5i 5b5a'.split() * 3, {}, '%SENNICHITE', '1/2-1/2'),
            ('sfen PPPPKPPPP/GG7/9/9/9/9/9/9/4k4 b 2RB3P 1', ['win'], {}, '%KACHI', '1-0'),
            ('sfen 4k4/5R3/9/9/9/9/9/9/K8 b - 1', '4b5b 5a4a 5b4b 4a5a'.split() * 3,
             {}, '%+ILLEGAL_ACTION', '0-1'),
            ('sfen 4k4/4R4/9/9/9/9/9/9/K8 w - 1', '5a4a 5b4b 4a5a 4b5b'.split() * 3,
             {}, '%+ILLEGAL_ACTION', '0-1'),
        ]
        for opening, moves, options, ending, result in cases:
            with self.subTest(ending=ending, opening=opening), tempfile.TemporaryDirectory() as directory:
                path = Path(directory)
                book, pgn, csa = path / 'book', path / 'match.pgn', path / 'match.csa'
                book.write_text(opening)
                with patch.object(cli, 'perf_counter', side_effect=range(0, 100, 2)):
                    Match(moves).run(ponder=False, opening=book, pgn=pgn,
                                     csa=csa, multi_csa=True, **options)
                self.assertEqual(csa.read_text().splitlines()[-1], ending)
                self.assertIn(f'[Result "{result}"]', pgn.read_text())


class ParallelMatchTests(unittest.TestCase):
    def test_ten_games_keep_openings_and_gpu_settings_with_their_pairs(self):
        match = Match(['resign'])
        barrier = Barrier(3, timeout=4)
        seen = {}
        commands = {}
        play_game = cli.Worker.play_game

        def play(worker, spec, stack):
            if spec.index in (0, 2, 4):
                barrier.wait()
            seen[spec.index] = (worker.prefix, worker.players[1].engine)
            return play_game(worker, spec, stack)

        def option(engine, key, value, **kwargs):
            commands.setdefault(engine, []).append((key, value))

        with tempfile.TemporaryDirectory() as directory, patch.object(cli.Worker, 'play_game', play), \
                patch.object(FakeEngine, 'setoption', option):
            book = Path(directory) / 'opening'
            book.write_text('startpos\nstartpos moves 7g7f 3c3d\n')
            pgn = Path(directory) / 'games'
            result = match.run(games=10, concurrency=3, ponder=False, pgn=pgn,
                               opening=book, opening_seed=31, options2={'model': 'new', 'gpu': '0'},
                               worker_options=[None, [{'gpu': str(i)} for i in range(3)]])
            records = re.split(r'\[Event ', pgn.read_text())[1:]
        self.assertEqual((result['total'], result['engine1_won'], result['engine2_won']), (10, 5, 5))
        moves = {int(re.search(r'\[Round "(\d+)"\]', r)[1]): r.split('\n\n')[1] for r in records}
        self.assertEqual(sorted(moves), list(range(1, 11)))
        self.assertTrue(any(moves.values()))
        self.assertIn('', moves.values())
        for i in range(0, 10, 2):
            self.assertEqual(seen[i], seen[i + 1])
            self.assertEqual(bool(moves[i + 1]), bool(moves[i + 2]))
        for prefix, engine in seen.values():
            gpu = {'[w1] ': '0', '[w2] ': '1', '[w3] ': '2'}[prefix]
            self.assertEqual(commands[engine][::2], [('model', 'new')] * (len(commands[engine]) // 2))
            self.assertEqual(commands[engine][1::2], [('gpu', gpu)] * (len(commands[engine]) // 2))

    def test_league_publishes_standings_in_stage_order(self):
        args = ('A', 'B', 'C', '--games', 3, '--no-swap', '--keep-process')
        baseline = run_cli(Match(['resign']), *args)
        later_stage = Event()
        play_game = cli.Worker.play_game
        report = cli.Match.report

        def play(worker, spec, stack):
            if spec.index == 0:
                self.assertTrue(later_stage.wait(4))
            return play_game(worker, spec, stack)

        def reported(state, result, *args):
            report(state, result, *args)
            if result.spec.index == 11:
                later_stage.set()

        match = Match(['resign'])
        commands = {}

        def option(engine, key, value, listener):
            commands.setdefault((engine, listener.label.split()[0]), []).append((key, value))

        with patch.object(cli.Worker, 'play_game', play), patch.object(cli.Match, 'report', reported), \
                patch.object(FakeEngine, 'setoption', option):
            overrides = ('UCT_Threads:2', 'UCT_Threads:0,UCT_Threads2:2')
            output = run_cli(match, *args, '--concurrency', 2,
                             '--options2', 'DNN_Model:base,UCT_Threads:3',
                             '--options3', 'DNN_Model:new,UCT_Threads:3',
                             '--worker-options2', *overrides, '--worker-options3', *overrides)
        for (engine, worker), sent in commands.items():
            model = {'B': 'base', 'C': 'new'}[engine.name]
            expected = [('DNN_Model', model)]
            expected += [('UCT_Threads', '2')] if worker == '[w1]' else [('UCT_Threads', '0'), ('UCT_Threads2', '2')]
            self.assertEqual(sent, expected * 2)
        standings = [s for s in baseline.splitlines() if 'finished.' in s or ': ' in s]
        self.assertEqual([s for s in output.splitlines() if not s.startswith('[w')], standings)
        self.assertEqual(len(match.engines), 12)
        self.assertEqual(sum(cmd == 'quit' for _, cmd in match.events), 12)

    def test_out_of_order_summary_and_csa_append(self):
        match = Match(['resign'])
        first, third = Event(), Event()
        play_game = cli.Worker.play_game
        report = cli.Match.report

        def ordered(worker, spec, stack):
            if spec.index == 2:
                self.assertTrue(first.wait(3))
            if spec.index % 2:
                self.assertTrue(third.wait(3))
            return play_game(worker, spec, stack)

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
            with patch.object(cli.Worker, 'play_game', ordered), patch.object(cli.Match, 'report', reported):
                result = match.run(ponder=False, print_summary=True, concurrency=2, games=4,
                                   csa=csa, multi_csa=True, pgn=pgn)
            self.assertEqual(len(cli.CSA.Parser.parse_file(str(csa))), 5)
            rounds = re.findall(r'\[Round "(\d+)"\]', pgn.read_text())
        self.assertEqual(rounds[:2], ['1', '3'])
        self.assertEqual(sorted(rounds), list('1234'))
        self.assertEqual(result['total'], 4)

    def test_interrupted_league_keeps_five_games_without_publishing_six(self):
        recorded = {index: Event() for index in (1, 3, 4)}
        closed = Event()
        report, close = cli.Match.report, cli.ProcessGroup.close

        def reported(state, result, *args):
            report(state, result, *args)
            if result.spec.index in recorded:
                recorded[result.spec.index].set()
            if result.spec.index == 4:
                self.assertTrue(closed.wait(4))

        def interrupt(*args, **kwargs):
            self.assertTrue(all(event.wait(3) for event in recorded.values()))
            raise KeyboardInterrupt

        def shutdown(group):
            close(group)
            closed.set()

        output = io.StringIO()
        with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stdout(output), \
                patch.object(cli, 'Engine', Match(['resign']).engine), patch.object(cli, 'wait', interrupt), \
                patch.object(cli.Match, 'report', reported), patch.object(cli.ProcessGroup, 'close', shutdown):
            pgn = Path(directory) / 'league.pgn'
            with self.assertRaises(KeyboardInterrupt):
                cli.league('A', 'B', 'C', games=2, concurrency=2, pgn=pgn)
            self.assertEqual(pgn.read_text().count('[Result '), 5)
        self.assertNotIn('games finished.', output.getvalue())

    def test_parallel_options_snapshot_survives_callback_mutation(self):
        options = {'Threads': 1, 'Hash': 16}
        setting, changed = Event(), Event()
        sent = []

        def option(engine, key, value, listener):
            if listener.label.startswith('[w2]'):
                sent.append((key, value))
                setting.set()
                self.assertTrue(changed.wait(3))
            else:
                self.assertTrue(setting.wait(3))

        def callback(stats):
            options.update(Hash=32, Ponder=False)
            changed.set()
            return False

        with patch.object(FakeEngine, 'setoption', option):
            result = Match(['resign']).run(games=4, concurrency=2, ponder=False,
                                           options1=options, callback=callback)
        self.assertEqual(sent, [('Threads', 1), ('Hash', 16)])
        self.assertEqual(result['total'], 2)

    def test_debug_search_can_return_while_a_callback_holds_the_match_lock(self):
        searching, reporting, logged = Event(), Event(), Event()
        play_game, go = cli.Worker.play_game, FakeEngine.go

        def play(worker, spec, stack):
            for player in worker.players.values():
                player.engine.during_report = spec.index == 2
            if spec.index == 0:
                self.assertTrue(searching.wait(3))
            return play_game(worker, spec, stack)

        def search(engine, **kwargs):
            if engine.during_report:
                searching.set()
                self.assertTrue(reporting.wait(3))
            result = go(engine, **kwargs)
            if engine.during_report:
                logged.set()
            return result

        def callback(stats):
            reporting.set()
            self.assertTrue(logged.wait(3))
            return False

        with patch.object(cli.Worker, 'play_game', play), patch.object(FakeEngine, 'go', search):
            result = Match(['resign']).run(games=4, concurrency=2, debug=True, ponder=False, callback=callback)
        self.assertEqual(result['total'], 2)

    def test_callback_stops_new_games_and_drains_started_games(self):
        match = Match(['resign'])
        started, stopped = Event(), Event()
        play_game = cli.Worker.play_game
        calls = []

        def ordered(worker, spec, stack):
            if spec.index == 0:
                self.assertTrue(started.wait(3))
            else:
                started.set()
                self.assertTrue(stopped.wait(3))
            return play_game(worker, spec, stack)

        def stop(stats):
            calls.append(stats['total'])
            stopped.set()

        with tempfile.TemporaryDirectory() as directory, patch.object(cli.Worker, 'play_game', ordered):
            pgn = Path(directory) / 'games'
            result = match.run(concurrency=2, games=10, callback=stop, pgn=pgn)
            self.assertEqual(pgn.read_text().count('[Result '), 2)
        self.assertEqual(calls, [1])
        self.assertEqual(result['total'], 2)

    def test_worker_failure_kills_ponder_and_drains_other_worker(self):
        other_started, killed = Event(), Event()
        failure = RuntimeError('search failed')
        play_game = cli.Worker.play_game
        match = Match()
        go = FakeEngine.go

        def play(worker, spec, stack):
            for player in worker.players.values():
                player.engine.failing = spec.index == 0
            if spec.index == 0:
                self.assertTrue(other_started.wait(3))
            else:
                other_started.set()
                self.assertTrue(killed.wait(3))
            return play_game(worker, spec, stack)

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

        with tempfile.TemporaryDirectory() as directory, patch.object(cli.Worker, 'play_game', play), \
                patch.object(FakeEngine, 'go', search), patch.object(FakeEngine, 'kill', kill), \
                patch.object(FakeEngine, 'stop', stop):
            pgn = Path(directory) / 'games'
            with self.assertRaises(RuntimeError) as caught:
                match.run(concurrency=2, games=8, pgn=pgn)
            self.assertEqual(caught.exception, failure)
            self.assertEqual(pgn.read_text().count('[Result '), 1)
        self.assertTrue(all(e.proc is None and not e.active for e in match.engines))

    def test_invalid_configuration_is_rejected_before_start(self):
        for kwargs in (dict(concurrency=0), dict(concurrency=2, is_display=True),
                       dict(concurrency=2, worker_options=[None, [{}]])):
            match = Match()
            with self.assertRaises(ValueError):
                match.run(**kwargs)
            self.assertEqual(match.engines, [])
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
            run_cli(match, 'A', 'B', 'C', '--concurrency', 2, '--worker-options3', 'GPU:0')
        self.assertEqual(caught.exception.code, 2)
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

    def run_shutdown(self, mode, concurrency=2, league=False):
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
                if concurrency == 1:
                    with patch.object(engine.proc.stdout, 'readline', side_effect=KeyboardInterrupt):
                        return handshake(engine, listener)
            handshake(engine, listener)

        def read(engine, listener):
            if mode == 'ponder' and engine.pondering:
                waiting.set()
            elif mode == 'ponder' and concurrency == 1 and engine.cmd == 'white':
                interrupt()
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
            if mode != 'callback' and concurrency > 1:
                stack.enter_context(patch.object(cli, 'wait', interrupt))
            start = monotonic()
            with self.assertRaises(RuntimeError if mode == 'callback' else KeyboardInterrupt) as caught:
                options = dict(concurrency=concurrency, games=4, keep_process=True, ponder=mode == 'ponder')
                if league:
                    cli.league('black', 'white', 'third', **options)
                else:
                    cli.main('black', 'white', callback=callback, **options)
            if mode == 'callback':
                self.assertIs(caught.exception, failure)
            self.assertLess(monotonic() - start, 7)
        self.assertTrue(processes)
        self.assertTrue(all(p.poll() is not None for p in processes))
        self.assertTrue(all(stream.closed for p in processes for stream in (p.stdin, p.stdout, p.stderr)))

    def test_interrupt_and_callback_cleanup(self):
        for mode in ('handshake', 'ponder'):
            for concurrency in (1, 2):
                for league in (False, True):
                    with self.subTest(mode=mode, concurrency=concurrency, league=league):
                        self.run_shutdown(mode, concurrency, league)
        self.run_shutdown('callback')
