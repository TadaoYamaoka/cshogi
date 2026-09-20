import unittest

import cshogi


class CheckMovesTest(unittest.TestCase):
    CASES = (
        ("4k4/9/9/9/9/9/9/9/4K4 b P 1", {"P*5b"}),
        ("9/4k4/9/4P4/9/9/9/9/4K4 b - 1", {"5d5c", "5d5c+"}),
        ("4rk3/9/9/9/8B/9/9/9/4K4 b - 1", {"1e5a+"}),
        ("4r1k2/9/6S2/9/9/9/9/9/4K4 b G 1", set()),
        ("4r4/9/9/9/5k3/9/9/9/4K4 b G 1", {"G*5e", "G*5f"}),
        ("4r4/9/9/5k3/8b/9/9/9/4K4 b G 1", set()),
    )

    def test_check_moves_match_legal_move_oracle(self):
        for sfen, expected in self.CASES:
            for position in (sfen, cshogi.rotate_sfen(sfen)):
                with self.subTest(position=position):
                    board = cshogi.Board(sfen=position)
                    oracle = set()
                    for move in board.legal_moves:
                        board.push(move)
                        if board.is_check():
                            oracle.add(move)
                        board.pop()

                    checking_moves = board.check_moves
                    actual = list(checking_moves)
                    self.assertIsInstance(checking_moves, cshogi.CheckMoveList)
                    self.assertEqual(len(checking_moves), len(actual))
                    self.assertEqual(len(actual), len(set(actual)))
                    self.assertEqual(set(actual), oracle)
                    self.assertEqual(
                        {cshogi.move_to_usi(move) for move in actual},
                        expected if position == sfen else {
                            cshogi.move_to_usi(move) for move in oracle
                        },
                    )
                    self.assertEqual(list(checking_moves), [])

    def test_iterator_keeps_generated_moves_after_board_changes(self):
        board = cshogi.Board(sfen=self.CASES[1][0])
        checking_moves = board.check_moves
        board.reset()
        self.assertEqual(
            {cshogi.move_to_usi(move) for move in checking_moves},
            self.CASES[1][1],
        )


if __name__ == "__main__":
    unittest.main()
