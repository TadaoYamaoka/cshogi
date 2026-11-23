import contextlib
import io
import re
import runpy
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from cshogi import cli
from test.test_cli_ponder import FakeEngine, Match


def run_cli(match, *args):
    output = io.StringIO()
    with patch('cshogi.usi.Engine', match.engine), \
            patch.object(sys, 'argv', ['cshogi.cli', *map(str, args)]), \
            contextlib.redirect_stdout(output):
        runpy.run_module('cshogi.cli', run_name='__main__')
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
