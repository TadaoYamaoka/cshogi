import unittest

import cshogi
import cshogi.osl


# 1手詰め: G*5b
MATE1_SFEN = "4k4/9/4P4/9/9/9/9/9/4K4 b G 1"
# 5手詰め
MATE5_SFEN = "4k4/9/9/9/9/9/9/9/4K4 b 2R2B4G4S4N4L18P 1"
# 9手以上の詰み(ノード数100では証明できない)
MATE_LONG_SFEN = "4k4/9/9/9/9/9/9/9/4K4 b RBGSNLP 1"
# 王手する手段がない(不詰め)
NOMATE_SFEN = "4k4/9/9/9/9/9/9/9/4K4 b - 1"
# 歩打ちの王手のみで詰まない(不詰め)
NOMATE2_SFEN = "4k4/9/9/9/9/9/9/9/4K4 b P 1"
# 王手を続けると連続王手の千日手になり、攻めが尽きる(不詰め)
# 千日手を使わずに不詰めを示せる変化はない
PERPETUAL_CHECK_SFEN = "8k/9/9/6g+RG/9/9/9/9/K8 b - 1"
# 87手目から9手詰め(95手目で詰む)。93手目までには詰みがない
MATE9_AT_95_SFEN = "+P3+P2nl/1+R+B3gb1/p3sk3/P1Npsppp1/2+r1pn2p/6P2/3PPPS1P/1S2GK2L/L4G1L1 b G3Pn 87"


class DfPnResultTest(unittest.TestCase):
    CLASSES = (cshogi.DfPn, cshogi.osl.DfPn)

    def search(self, cls, sfen, **kwargs):
        dfpn = cls(**kwargs)
        board = cshogi.Board(sfen)
        found = dfpn.search(board)
        return dfpn, found

    def test_constants(self):
        for cls in self.CLASSES:
            with self.subTest(cls=cls):
                self.assertEqual(cls.UNKNOWN, 0)
                self.assertEqual(cls.MATE, 1)
                self.assertEqual(cls.NO_MATE, 2)
                self.assertEqual(cls().result, cls.UNKNOWN)

    def test_mate(self):
        for cls in self.CLASSES:
            for sfen in (MATE1_SFEN, MATE5_SFEN, MATE_LONG_SFEN):
                with self.subTest(cls=cls, sfen=sfen):
                    dfpn, found = self.search(cls, sfen)
                    self.assertTrue(found)
                    self.assertEqual(dfpn.result, cls.MATE)

    def test_no_mate(self):
        for cls in self.CLASSES:
            for sfen in (NOMATE_SFEN, NOMATE2_SFEN):
                with self.subTest(cls=cls, sfen=sfen):
                    dfpn, found = self.search(cls, sfen)
                    self.assertFalse(found)
                    self.assertEqual(dfpn.result, cls.NO_MATE)

    def test_unknown_by_node_limit(self):
        for cls in self.CLASSES:
            with self.subTest(cls=cls):
                dfpn, found = self.search(cls, MATE_LONG_SFEN, nodes=100)
                self.assertFalse(found)
                self.assertEqual(dfpn.result, cls.UNKNOWN)
                self.assertGreater(dfpn.searched_node, 0)

    def test_result_is_reset(self):
        for cls in self.CLASSES:
            with self.subTest(cls=cls):
                dfpn = cls()
                dfpn.search(cshogi.Board(MATE5_SFEN))
                self.assertEqual(dfpn.result, cls.MATE)
                dfpn.search(cshogi.Board(NOMATE2_SFEN))
                self.assertEqual(dfpn.result, cls.NO_MATE)
                dfpn.set_max_search_node(100)
                dfpn.search(cshogi.Board(MATE_LONG_SFEN))
                self.assertEqual(dfpn.result, cls.UNKNOWN)

    def test_no_mate_by_perpetual_check(self):
        # 連続王手の千日手による反証は、探索深さの上限に依存しないので不詰め
        for cls in self.CLASSES:
            with self.subTest(cls=cls):
                dfpn, found = self.search(cls, PERPETUAL_CHECK_SFEN)
                self.assertFalse(found)
                self.assertEqual(dfpn.result, cls.NO_MATE)

    def test_unknown_by_depth_limit(self):
        # 探索深さの上限による打ち切りは不詰めの証明にならない
        for cls in self.CLASSES:
            with self.subTest(cls=cls):
                dfpn, found = self.search(cls, MATE5_SFEN, depth=1)
                self.assertFalse(found)
                self.assertEqual(dfpn.result, cls.UNKNOWN)

        # 3手詰めチェックの中での打ち切り
        # (OSLは深さ制限があると探索ノード数が大きくなるため対象外)
        dfpn, found = self.search(cshogi.DfPn, MATE5_SFEN, depth=3)
        self.assertFalse(found)
        self.assertEqual(dfpn.result, cshogi.DfPn.UNKNOWN)

    def test_andnode_unknown_by_depth_limit(self):
        for cls in self.CLASSES:
            with self.subTest(cls=cls):
                board = cshogi.Board(MATE5_SFEN)
                dfpn = cls()
                self.assertTrue(dfpn.search(board))
                board.push(dfpn.get_move(board))
                dfpn2 = cls(depth=1)
                self.assertFalse(dfpn2.search_andnode(board))
                self.assertEqual(dfpn2.result, cls.UNKNOWN)

                # 深さ制限で打ち切られても、それまでの探索ノード数を返す
                dfpn3 = cls(depth=3)
                self.assertFalse(dfpn3.search_andnode(board))
                self.assertEqual(dfpn3.result, cls.UNKNOWN)
                self.assertGreater(dfpn3.searched_node, 0)

    # 手数上限までに詰みが間に合わない場合の結果
    # (OSL版は手数上限と深さ制限を区別しないため、不詰めではなく未確定になる)
    DRAW_PLY_NO_MATE = {cshogi.DfPn: cshogi.DfPn.NO_MATE, cshogi.osl.DfPn: cshogi.osl.DfPn.UNKNOWN}

    def test_no_mate_by_draw_ply(self):
        # 対局の手数上限までに詰みが間に合わない場合は不詰め
        # (探索深さだけを制限した test_unknown_by_depth_limit と対になる)
        for cls in self.CLASSES:
            with self.subTest(cls=cls):
                dfpn, found = self.search(cls, MATE5_SFEN, draw_ply=3, nodes=10000)
                self.assertFalse(found)
                self.assertEqual(dfpn.result, self.DRAW_PLY_NO_MATE[cls])

                # 手数上限が間に合えば詰み
                dfpn, found = self.search(cls, MATE5_SFEN, draw_ply=5)
                self.assertTrue(found)
                self.assertEqual(dfpn.result, cls.MATE)

    def test_andnode_draw_ply(self):
        # 受け方の応手が4手目、詰ます手が5手目
        for cls in self.CLASSES:
            for draw_ply, mate in ((4, False), (5, True)):
                with self.subTest(cls=cls, draw_ply=draw_ply):
                    board = cshogi.Board(MATE5_SFEN)
                    for usi in ("R*5c", "5a4a", "5c4c+"):
                        board.push_usi(usi)
                    dfpn = cls(draw_ply=draw_ply, nodes=10000)
                    self.assertEqual(dfpn.search_andnode(board), mate)
                    self.assertEqual(dfpn.result, cls.MATE if mate else self.DRAW_PLY_NO_MATE[cls])

    def test_mate1_draw_ply(self):
        # 5手目に1手詰め
        sfen = MATE1_SFEN.rsplit(" ", 1)[0] + " 5"
        for cls in self.CLASSES:
            for draw_ply, mate in ((4, False), (5, True)):
                with self.subTest(cls=cls, draw_ply=draw_ply):
                    dfpn, found = self.search(cls, sfen, draw_ply=draw_ply, nodes=10000)
                    self.assertEqual(found, mate)
                    self.assertEqual(dfpn.result, cls.MATE if mate else self.DRAW_PLY_NO_MATE[cls])

    def test_mate1_evading_check_at_last_ply(self):
        # 最終許容手(1手目)で、自玉への王手を解除しながら詰ます(2c1b、1d1b+)
        sfen = "8k/4K3r/7G1/8R/9/9/9/9/9 b - 1"
        for cls in self.CLASSES:
            for draw_ply in (1, 2):
                with self.subTest(cls=cls, draw_ply=draw_ply):
                    board = cshogi.Board(sfen)
                    dfpn = cls(draw_ply=draw_ply, nodes=256)
                    self.assertTrue(dfpn.search(board))
                    self.assertEqual(dfpn.result, cls.MATE)
                    pv = [cshogi.move_to_usi(m) for m in dfpn.get_pv(board)]
                    self.assertEqual(len(pv), 1)
                    self.assertIn(pv[0], ("2c1b", "1d1b+"))

    def test_pv_within_draw_ply(self):
        # 最終許容手での1手詰め(3a1c+)を証明した場合、PVも手数上限を超えない
        sfen = "5RB1k/9/7Kr/9/9/8L/9/9/4G4 b G 1"
        for cls in self.CLASSES:
            with self.subTest(cls=cls):
                board = cshogi.Board(sfen)
                dfpn = cls(draw_ply=1, nodes=256)
                self.assertTrue(dfpn.search(board))
                self.assertEqual(dfpn.result, cls.MATE)
                self.assertEqual(dfpn.get_pv(board), [dfpn.get_move(board)])

    def test_no_mate_when_only_defender_move_is_cut(self):
        # 探索深さとdraw_plyの間に受け方の手しかない場合は、深さを増やしても詰ませられないので不詰め
        for depth in (1, 2):
            with self.subTest(depth=depth):
                dfpn, found = self.search(cshogi.DfPn, MATE5_SFEN, depth=depth, draw_ply=2)
                self.assertFalse(found)
                self.assertEqual(dfpn.result, cshogi.DfPn.NO_MATE)

    def test_game_ply_overflow(self):
        # 手数がINT_MAXでも、手数上限を超えて詰みとしない
        INT_MAX = 2147483647
        dfpn, found = self.search(cshogi.DfPn, MATE1_SFEN.rsplit(" ", 1)[0] + f" {INT_MAX}", draw_ply=10)
        self.assertFalse(found)
        self.assertEqual(dfpn.result, cshogi.DfPn.NO_MATE)

        # draw_ply=INT_MAXでも内部の上限(+1)が溢れない
        for cls in self.CLASSES:
            with self.subTest(cls=cls):
                dfpn, found = self.search(cls, MATE1_SFEN, draw_ply=INT_MAX)
                self.assertTrue(found)
                self.assertEqual(dfpn.result, cls.MATE)
                dfpn = cls()
                dfpn.set_draw_ply(INT_MAX)
                self.assertTrue(dfpn.search(cshogi.Board(MATE1_SFEN)))
                self.assertEqual(dfpn.result, cls.MATE)

        # INT_MAX手目からはPVの復元でも着手しない
        for cls in self.CLASSES:
            with self.subTest(cls=cls):
                sfen = MATE1_SFEN.rsplit(" ", 1)[0] + f" {INT_MAX}"
                dfpn, found = self.search(cls, sfen, draw_ply=INT_MAX)
                self.assertFalse(found)
                self.assertEqual(dfpn.result, cls.UNKNOWN)
                self.assertEqual(dfpn.get_pv(cshogi.Board(sfen)), [])
                self.assertEqual(dfpn.get_move(cshogi.Board(sfen)), 0)

        # 手数とdraw_plyがともにINT_MAX付近の場合
        # Positionの手数はintのため、INT_MAX手目は探索できず未確定になる(誤ってMATEにしない)
        base = MATE5_SFEN.rsplit(" ", 1)[0]
        for cls in self.CLASSES:
            for ply, expected in ((INT_MAX - 5, cls.MATE),
                                  (INT_MAX - 4, cls.UNKNOWN),
                                  (INT_MAX - 1, self.DRAW_PLY_NO_MATE[cls]),
                                  (INT_MAX, cls.UNKNOWN)):
                with self.subTest(cls=cls, ply=ply):
                    dfpn, found = self.search(cls, f"{base} {ply}", draw_ply=INT_MAX, nodes=10000)
                    self.assertEqual(found, expected == cls.MATE)
                    self.assertEqual(dfpn.result, expected)
                    pv = dfpn.get_pv(cshogi.Board(f"{base} {ply}"))
                    if found:
                        self.assertEqual(len(pv), 5)
                    else:
                        # 探索で確定しなかった局面から、手数上限を超えて着手しない
                        self.assertLessEqual(len(pv), INT_MAX - ply)

        board = cshogi.Board(MATE5_SFEN)
        board.push_usi("R*5c")
        board = cshogi.Board(board.sfen().rsplit(" ", 1)[0] + f" {INT_MAX}")
        dfpn = cshogi.DfPn(draw_ply=10)
        self.assertFalse(dfpn.search_andnode(board))
        self.assertEqual(dfpn.result, cshogi.DfPn.NO_MATE)

    def test_no_mate_by_reused_proof_beyond_draw_ply(self):
        # 浅い手数で証明された詰みを、残り手数の少ない深い手数で再利用しない
        for draw_ply, expected in ((93, cshogi.DfPn.NO_MATE), (94, cshogi.DfPn.NO_MATE), (95, cshogi.DfPn.MATE)):
            with self.subTest(draw_ply=draw_ply):
                dfpn, found = self.search(cshogi.DfPn, MATE9_AT_95_SFEN, draw_ply=draw_ply, nodes=10000)
                self.assertEqual(found, expected == cshogi.DfPn.MATE)
                self.assertEqual(dfpn.result, expected)
                if found:
                    pv = dfpn.get_pv(cshogi.Board(MATE9_AT_95_SFEN))
                    self.assertLessEqual(len(pv), draw_ply - 87 + 1)

    def test_reuse_proof_within_remaining_plies(self):
        # 浅い手数で証明された詰みも、証明に必要な手数が残り手数に収まれば再利用する
        # (再利用しないと、このノード数では解けない)
        sfen = "1G1+S2bnl/7g1/2p3p2/7PP/1pk2+rPp1/P8/LPSs+s1N2/1KP3+n2/1N7 w rb2g2l8p 180"
        dfpn, found = self.search(cshogi.DfPn, sfen, nodes=2500)
        self.assertTrue(found)
        self.assertEqual(dfpn.result, cshogi.DfPn.MATE)

        # 3手読みの末端の1手詰めは、詰ます手に必要な駒だけを証明駒にする
        # (攻め側の持ち駒をすべて証明駒にすると、このノード数では解けない)
        sfen = "6+S2/9/p1n+Rpp1k1/8p/2S3pp1/5L3/PP1+n1P1GP/GpPG1S1L+b/LN1K5 b BGNL4Prs2p 143"
        dfpn, found = self.search(cshogi.DfPn, sfen, nodes=2000)
        self.assertTrue(found)
        self.assertEqual(dfpn.result, cshogi.DfPn.MATE)

        # 3手読みの末端で証明済みの子局面は、その証明を維持する
        sfen = "l2+R4l/2S2sk2/n1p1p1gp1/p1s5b/1p1P1NP1p/2P1S4/PP2P3P/2GK1G3/LN5RL b GN5Pb 79"
        dfpn, found = self.search(cshogi.DfPn, sfen, nodes=7000)
        self.assertTrue(found)
        self.assertEqual(dfpn.result, cshogi.DfPn.MATE)

        sfen = "ln6l/6k2/1P1p3pn/p4sp1p/4p3n/3P2PR1/P+pNS1ps1P/3B5/L2GK3L b R3G4Pbsp 97"
        dfpn, found = self.search(cshogi.DfPn, sfen, nodes=8000)
        self.assertTrue(found)
        self.assertEqual(dfpn.result, cshogi.DfPn.MATE)

        # 3手読みで全応手が詰んだとき、受け側の局面にある証明を維持する
        sfen = "ln1g4l/3k5/3ppN2b/p1ps1G2p/4n1+R2/1PP6/P1NPPS2P/1pGS1G3/L3KP2L b RBSP4p 85"
        dfpn, found = self.search(cshogi.DfPn, sfen, nodes=5050)
        self.assertTrue(found)
        self.assertEqual(dfpn.result, cshogi.DfPn.MATE)

    def test_andnode_mated_after_draw_ply(self):
        # 5手目のG*5bで詰んだ局面。最終手が手数上限を超えていれば詰みとしない
        board = cshogi.Board(MATE1_SFEN.rsplit(" ", 1)[0] + " 5")
        board.push_usi("G*5b")
        for draw_ply, expected in ((4, cshogi.DfPn.NO_MATE), (5, cshogi.DfPn.MATE)):
            with self.subTest(draw_ply=draw_ply):
                dfpn = cshogi.DfPn(draw_ply=draw_ply)
                self.assertEqual(dfpn.search_andnode(board), expected == cshogi.DfPn.MATE)
                self.assertEqual(dfpn.result, expected)

    def test_draw_ply_equal_to_depth_limit(self):
        # 探索深さと手数上限が同じ手数の場合は、手数上限による打ち切りとして扱う
        dfpn, found = self.search(cshogi.DfPn, MATE5_SFEN, depth=3, draw_ply=3)
        self.assertFalse(found)
        self.assertEqual(dfpn.result, cshogi.DfPn.NO_MATE)

    def test_andnode(self):
        for cls in self.CLASSES:
            with self.subTest(cls=cls):
                board = cshogi.Board(MATE5_SFEN)
                dfpn = cls()
                self.assertTrue(dfpn.search(board))
                board.push(dfpn.get_move(board))
                dfpn2 = cls()
                self.assertTrue(dfpn2.search_andnode(board))
                self.assertEqual(dfpn2.result, cls.MATE)


if __name__ == "__main__":
    unittest.main()
