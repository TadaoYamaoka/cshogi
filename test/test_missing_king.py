import unittest

import cshogi


class MissingKingMoveGenerationTest(unittest.TestCase):
    def test_missing_side_has_only_real_pawn_drops(self):
        for sfen in (
            "4k4/9/9/9/9/9/9/9/9 b P 1",
            "9/9/9/9/9/9/9/9/4K4 w p 1",
        ):
            with self.subTest(sfen=sfen):
                board = cshogi.Board(sfen)
                moves = list(board.legal_moves)
                ranks = "bcdefghi" if board.turn == cshogi.BLACK else "abcdefgh"
                expected = {f"P*{file}{rank}" for file in range(1, 10) for rank in ranks}
                self.assertEqual(len(moves), len(expected))
                self.assertEqual({cshogi.move_to_usi(move) for move in moves}, expected)

    def test_missing_side_without_pieces_has_no_moves(self):
        for sfen in (
            "4k4/9/9/9/9/9/9/9/9 b - 1",
            "9/9/9/9/9/9/9/9/4K4 w - 1",
        ):
            with self.subTest(sfen=sfen):
                self.assertEqual(list(cshogi.Board(sfen).legal_moves), [])


if __name__ == "__main__":
    unittest.main()
