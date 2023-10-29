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
