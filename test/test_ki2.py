from cshogi import *
from cshogi import KI2


def test_move_to_ki2_issue37():
    # issue #37
    board = Board(sfen="ln1gk3l/1r4g2/p1ppssnpp/4p1p2/1p3P3/2P1SgPRP/PPBPP1N2/2SK5/LN1G4L b B2P 1")
    board.push_usi("4e4d")
    move = board.move_from_usi("5c4d")
    move_ki2 = KI2.move_to_ki2(move, board)
    assert move_ki2 == "△同銀右"

    board = Board(sfen="ln1gk3l/1r4g2/p1ppssnpp/4p1p2/1p3P3/2P1SgPRP/PPBPP1N2/2SK5/LN1G4L b B2P 1")
    board.push_usi("4e4d")
    move = board.move_from_usi("4c4d")
    move_ki2 = KI2.move_to_ki2(move, board)
    assert move_ki2 == "△同銀直"


def test_move_to_ki2_dragon_vertical_relative():
    # Two dragons on the same file; pulling dragon must be labeled 左, not 直
    board = Board(sfen="ln1g4l/1ks6/1pp2pn1p/p2p1+R3/6+R2/P1s3P1P/1P1P2N2/2K6/LN2S3L b B2G3Pbgs4p 1")
    move = board.move_from_usi("4d4f")
    move_ki2 = KI2.move_to_ki2(move, board)
    assert move_ki2 == "▲４六龍左"

    # Swapping to the other dragon
    board = Board(sfen="ln1g4l/1ks6/1pp2pn1p/p2p1+R3/6+R2/P1s3P1P/1P1P2N2/2K6/LN2S3L b B2G3Pbgs4p 1")
    move2 = board.move_from_usi("3e4f")
    move_ki2_2 = KI2.move_to_ki2(move2, board)
    assert move_ki2_2 == "▲４六龍右"


def test_move_to_ki2_dragon_pull_motion():
    # Two candidates pulling back: only relative is required when motion alone is insufficient
    board = Board(sfen="ln1g3+Rl/1ks1g4/1pp3n1p/p5p2/3p5/P1P3P1P/1PSPSPN2/2K6/LN2Sr2L b BG2Pbg3p 1")
    move = board.move_from_usi("5g6h")
    move_ki2 = KI2.move_to_ki2(move, board)
    assert move_ki2 == "▲６八銀右"


def test_move_to_ki2_horse_left_vertical():
    board = Board(sfen="ln1g1R2l/1ks6/1pp1p1n1p/p2p+B+B3/9/P5P1P/1P1P2N2/2K6/LN2S2rL b 2G3Pg2s4p 1")
    move = board.move_from_usi("5d5c")
    move_ki2 = KI2.move_to_ki2(move, board)
    assert move_ki2 == "▲５三馬左"


def test_move_to_ki2_drop_with_existing_piece():
    board = Board(sfen="ln1g4l/1ks6/1pp3n1p/p2pS4/9/P1P3P1P/1P1P2N2/2K1p4/LN1GP2+rL b RB2SPb2g4p 1")
    move = board.move_from_usi("S*6c")
    move_ki2 = KI2.move_to_ki2(move, board)
    assert move_ki2 == "▲６三銀打"


def test_move_to_ki2_unique_motion_without_relative():
    # Motion alone distinguishes the move, so no 左/右 should appear
    board = Board(sfen="l+R5nl/3p1bgk1/2+B2p1sp/p3p4/2p3G2/P4P1R1/3gS2pP/4g1S2/L5KPL w N5Ps2n2p 1")
    move = board.move_from_usi("6g5g")
    move_ki2 = KI2.move_to_ki2(move, board)
    assert move_ki2 == "△５七金寄"


def test_move_to_ki2_horse_left_right_branch():
    board = Board(sfen="+B+B3g1nl/1p4sk1/3p1g3/p1p3ppp/5p3/P4PPPP/3S1SN2/4G1K2/L1+r1P3L b RNL2Pgsn2p 1")
    move_left = board.move_from_usi("9a8b")
    move_right = board.move_from_usi("8a8b")
    assert KI2.move_to_ki2(move_left, board) == "▲８二馬左"
    assert KI2.move_to_ki2(move_right, board) == "▲８二馬右"


def test_move_to_ki2_horse_left_right_down_variation():
    board = Board(sfen="3+R3nk/6ggl/7pp/p2bppp2/6lNP/P3P4/5PSPS/6G+nL/1+r4SSK b GL3Pbn4p 1")
    moves = {
        "3g2h": "▲２八銀左引",
        "1g2h": "▲２八銀右",
        "3i2h": "▲２八銀左上",
        "2i2h": "▲２八銀直",
    }
    for usi, expected in moves.items():
        assert KI2.move_to_ki2(board.move_from_usi(usi), board) == expected


def test_move_to_ki2_horse_motion_vs_relative():
    board = Board(sfen="6gnk/7sl/p2+B1g1p1/6p1p/+Bn3p3/P4PPPP/3+r2S2/6SK1/L4G1NL b RL3Pgsn5p 1")
    move_side = board.move_from_usi("9e8e")
    move_pull = board.move_from_usi("6c8e")
    assert KI2.move_to_ki2(move_side, board) == "▲８五馬寄"
    assert KI2.move_to_ki2(move_pull, board) == "▲８五馬引"


def test_move_to_ki2_horse_vertical_motion_selection():
    board = Board(sfen="+B6nl/l5gk1/5g1s1/p1+B2ppnp/7p1/P5P1P/4+p2P1/6GSL/L2+r2GNK b RN3P2s5p 1")
    move_pull = board.move_from_usi("9a9b")
    move_up = board.move_from_usi("7d9b")
    assert KI2.move_to_ki2(move_pull, board) == "▲９二馬引"
    assert KI2.move_to_ki2(move_up, board) == "▲９二馬上"


def test_move_to_ki2_gold_like_straight_and_relative():
    board = Board(sfen="+R6nk/5gggl/6Ppp/p4pp2/2+b1p3P/P8/6SP1/7SL/1+r3PGNK w S2NL2Pbsl5p 1")
    moves = {
        "4b3c": "△３三金右",
        "3b3c": "△３三金直",
        "2b3c": "△３三金左",
    }
    for usi, expected in moves.items():
        assert KI2.move_to_ki2(board.move_from_usi(usi), board) == expected


def test_move_to_ki2_dragon_left_right_branch():
    board = Board(sfen="l2p2gkl/2+R2sgs1/3+R5/p4ppp1/7np/P3pPP1P/7P1/4P1SK1/L4G1NL b BSN3Pbgn2p 1")
    move_left = board.move_from_usi("7b6a")
    move_right = board.move_from_usi("6c6a")
    assert KI2.move_to_ki2(move_left, board) == "▲６一龍左"
    assert KI2.move_to_ki2(move_right, board) == "▲６一龍右"


def test_move_to_ki2_dragon_motion_distinction():
    board = Board(sfen="l+R4gnk/2s1+R1gsl/7pp/p4pp2/8P/P3PPPP1/5GNS1/4+p1GK1/L4s2L b 2N3P2b3p 1")
    move_back = board.move_from_usi("8a7b")
    move_side = board.move_from_usi("5b7b")
    assert KI2.move_to_ki2(move_back, board) == "▲７二龍引"
    assert KI2.move_to_ki2(move_side, board) == "▲７二龍寄"


def test_move_to_ki2_promoted_pawn_relative_and_motion():
    board = Board(sfen="2+R4K1/3+P2G+B1/4+N+P1+L1/2p5p/1p7/p7P/+p+p+p6/2+p5L/k+p1+r5 w G2S3N3Pb2g2s2l3p 1")
    moves = {
        "9g8h": "△８八と右",
        "8g8h": "△８八と直",
        "7g8h": "△８八と左上",
        "8i8h": "△８八と引",
        "7h8h": "△８八と寄",
    }
    for usi, expected in moves.items():
        assert KI2.move_to_ki2(board.move_from_usi(usi), board) == expected
