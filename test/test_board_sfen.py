import subprocess
import sys
import unittest

import cshogi


class BoardSfenTests(unittest.TestCase):
    def test_invalid_sfen_raises_without_terminating_process(self):
        result = subprocess.run(
            [sys.executable, '-c',
             "import cshogi\n"
             "try:\n"
             "    cshogi.Board().set_sfen('invalid b - 1')\n"
             "except RuntimeError as error:\n"
             "    print(error)\n"
             "else:\n"
             "    raise AssertionError('invalid SFEN was accepted')\n"],
            capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('incorrect SFEN string', result.stdout)

    def test_valid_sfen_replaces_position_and_history(self):
        board = cshogi.Board()
        board.push_usi('7g7f')
        sfen = '4k4/9/9/9/9/9/9/9/4K4 w P 41'
        board.set_sfen(sfen)
        self.assertEqual(board.sfen(), sfen)
        self.assertEqual(board.turn, cshogi.WHITE)
        self.assertEqual(board.move_number, 41)
        self.assertEqual(len(board.history), 0)


if __name__ == '__main__':
    unittest.main()
