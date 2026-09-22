import unittest

import cshogi


class NyugyokuTest(unittest.TestCase):
    POSITION = "PPPPKPPPP/GG7/9/9/9/9/9/9/4k4 b {hand} 1"

    def board(self, hand):
        return cshogi.Board(sfen=self.POSITION.format(hand=hand))

    def assert_result_for_both_colors(self, hand, rule, expected):
        sfen = self.POSITION.format(hand=hand)
        for position in (sfen, cshogi.rotate_sfen(sfen)):
            with self.subTest(position=position, rule=rule):
                self.assertEqual(
                    cshogi.Board(sfen=position).nyugyoku_result(rule),
                    expected,
                )

    def test_result_constants(self):
        self.assertEqual(cshogi.NYUGYOKU_NONE, 0)
        self.assertEqual(cshogi.NYUGYOKU_WIN, 1)
        self.assertEqual(cshogi.NYUGYOKU_DRAW, 2)
        self.assertNotEqual(cshogi.LAW_24, cshogi.LAW_27)

    def test_24_point_rule_boundaries(self):
        # The ten pieces in the enemy camp are worth ten points.
        rule = cshogi.LAW_24
        self.assert_result_for_both_colors("2R2BP", rule, cshogi.NYUGYOKU_WIN)
        self.assert_result_for_both_colors("2R2B", rule, cshogi.NYUGYOKU_DRAW)
        self.assert_result_for_both_colors("2R4P", rule, cshogi.NYUGYOKU_DRAW)
        self.assert_result_for_both_colors("2R3P", rule, cshogi.NYUGYOKU_NONE)

    def test_27_point_rule_boundaries(self):
        black_28 = self.POSITION.format(hand="2RB3P")
        black_27 = self.POSITION.format(hand="2RB2P")
        black_26 = self.POSITION.format(hand="2RBP")
        rule = cshogi.LAW_27

        self.assertEqual(
            cshogi.Board(sfen=black_28).nyugyoku_result(rule),
            cshogi.NYUGYOKU_WIN,
        )
        self.assertEqual(
            cshogi.Board(sfen=black_27).nyugyoku_result(rule),
            cshogi.NYUGYOKU_NONE,
        )
        self.assertEqual(
            cshogi.Board(sfen=cshogi.rotate_sfen(black_27)).nyugyoku_result(rule),
            cshogi.NYUGYOKU_WIN,
        )
        self.assertEqual(
            cshogi.Board(sfen=cshogi.rotate_sfen(black_26)).nyugyoku_result(rule),
            cshogi.NYUGYOKU_NONE,
        )

    def test_boolean_api_returns_true_only_for_a_win(self):
        win_27 = self.board("2RB3P")
        draw_24 = self.board("2R2B")

        self.assertEqual(draw_24.nyugyoku_result(), cshogi.NYUGYOKU_DRAW)
        self.assertEqual(draw_24.nyugyoku_result(cshogi.LAW_27), cshogi.NYUGYOKU_WIN)
        self.assertTrue(win_27.is_nyugyoku())
        self.assertTrue(win_27.is_nyugyoku(cshogi.LAW_27))
        self.assertFalse(draw_24.is_nyugyoku(cshogi.LAW_24))

    def test_common_declaration_conditions(self):
        positions = (
            # King has not entered the enemy camp.
            "PPPP1PPPP/GG7/9/4K4/9/9/9/9/4k4 b 2R2BP 1",
            # Only nine non-king pieces are in the enemy camp.
            "PPPPKPPPP/G8/9/9/9/9/9/9/4k4 b 2R2BP 1",
            # The declaring king is in check from the rook on 5c.
            "PPPPKPPPP/GG7/4r4/9/9/9/9/9/4k4 b R2B2G4S 1",
        )

        for position in positions:
            with self.subTest(position=position):
                self.assertEqual(
                    cshogi.Board(sfen=position).nyugyoku_result(),
                    cshogi.NYUGYOKU_NONE,
                )

    def test_invalid_rule(self):
        board = self.board("2R2BP")
        for rule in (-1, 2, 24, 27):
            with self.subTest(rule=rule):
                with self.assertRaises(ValueError):
                    board.nyugyoku_result(rule)
                with self.assertRaises(ValueError):
                    board.is_nyugyoku(rule)

        with self.assertRaises(TypeError):
            board.nyugyoku_result(None)
        with self.assertRaises(TypeError):
            board.is_nyugyoku(None)


if __name__ == "__main__":
    unittest.main()
