from cshogi._cshogi import _dlshogi_FEATURES1_NUM as FEATURES1_NUM
from cshogi._cshogi import _dlshogi_FEATURES2_NUM as FEATURES2_NUM
from cshogi._cshogi import _dlshogi_make_move_label as make_move_label

def make_input_features(board, features1, features2):
    board._dlshogi_make_input_features(features1, features2)
