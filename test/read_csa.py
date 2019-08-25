import cshogi
import numpy as np
import sys
import glob
import time
import shogi
import shogi.CSA
import copy

# read kifu
def read_kifu_cython(kifu_list):
    positions = []
    parser = cshogi.Parser()
    for filepath in kifu_list:
        parser.parse_csa_file(filepath)
        board = cshogi.Board()
        for move, score in zip(parser.moves, parser.scores):
            hcpe = np.empty(1, dtype=cshogi.HuffmanCodedPosAndEval)
            # hcp
            board.to_hcp(hcpe[0]['hcp'])
            # eval
            hcpe[0]['eval'] = score
            # move
            hcpe[0]['bestMove16'] = cshogi.move16(move)
            # result
            hcpe[0]['gameResult'] = parser.win

            positions.append(hcpe)
            board.push(move)
    return positions

def read_kifu(kifu_list):
    positions = []
    parser = cshogi.Parser()
    for filepath in kifu_list:
        kifu = shogi.CSA.Parser.parse_file(filepath)[0]
        board = shogi.Board()
        for move in kifu['moves']:
            # bitboard
            piece_bb = copy.deepcopy(board.piece_bb)
            occupied = copy.deepcopy((board.occupied[shogi.BLACK], board.occupied[shogi.WHITE]))
            pieces_in_hand = copy.deepcopy((board.pieces_in_hand[shogi.BLACK], board.pieces_in_hand[shogi.WHITE]))

            positions.append((piece_bb, occupied, pieces_in_hand, move, kifu['win']))
            board.push_usi(move)
    return positions

kifu_list = glob.glob(sys.argv[1] + r"\*")

# cython-shogi
start = time.time()
positions =read_kifu_cython(kifu_list)
elapsed_time = time.time() - start
print ("elapsed_time:{0}".format(elapsed_time) + "[sec]")

# python-shogi
start = time.time()
positions =read_kifu(kifu_list)
elapsed_time = time.time() - start
print ("elapsed_time:{0}".format(elapsed_time) + "[sec]")
