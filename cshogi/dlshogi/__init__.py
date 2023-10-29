from cshogi._cshogi import _dlshogi_FEATURES1_NUM as FEATURES1_NUM
from cshogi._cshogi import _dlshogi_FEATURES2_NUM as FEATURES2_NUM
from cshogi._cshogi import _dlshogi_make_move_label as make_move_label
from cshogi import Board
import numpy as np

__all__ = ["FEATURES1_NUM", "FEATURES2_NUM", "make_move_label", "make_input_features"]

def make_input_features(board: Board, features1: np.ndarray, features2: np.ndarray):
    """Make input features from the given board for use in the dlshogi model.

    :param board: A Board object representing the current state of a game.
    :param features1: A numpy array of shape (FEATURES1_NUM, 9, 9) to be filled with the first set of features.
    :param features2: A numpy array of shape (FEATURES2_NUM, 9, 9) to be filled with the second set of features.
    """
    board._dlshogi_make_input_features(features1, features2)
