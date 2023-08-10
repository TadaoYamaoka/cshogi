from typing import Dict, List, Union, Optional
import cshogi
from cshogi import Parser

COLOR_SYMBOLS = ['+', '-']
JAPANESE_END_GAMES = {
    '%TORYO': '投了',
    '%CHUDAN': '中断',
    '%SENNICHITE': '千日手',
    '%TIME_UP': '切れ負け',
    '%ILLEGAL_MOVE': '反則負け',
    '%JISHOGI': '持将棋',
    '%KACHI': '入力宣言',
}

class Exporter:
    """A class to handle the exporting of a game to CSA format.

    :param path: The file path to export to, defaults to None.
    :param append: Whether to append to the file, defaults to False.
    :param encoding: The encoding of the file, defaults to None.
    """

    def __init__(self, path: Optional[str] = None, append: bool = False, encoding: Optional[str] = None):
        if path:
            self.open(path, append, encoding=encoding)
        else:
            self.f = None

    def open(self, path: str, append: bool = False, encoding: Optional[str]=None):
        """Open the file for writing.

        :param path: The file path to export to.
        :type path: str
        :param append: Whether to append to the file, defaults to False.
        :type append: bool, optional
        :param encoding: The encoding of the file, defaults to None.
        :type encoding: str, optional
        """
        self.f = open(path, 'a' if append else 'w', newline='\n', encoding=encoding)

    def close(self):
        """Close the file."""
        self.f.close()

    def info(self, init_board: Optional[Union[str, cshogi.Board]] = None, names: Optional[List[str]] = None, var_info: Optional[Dict] = None, comment: Optional[str] = None, version: Optional[str] = None):
        """Write game information to the file.

        :param init_board: The initial board state, defaults to None.
        :param names: The names of the players, defaults to None.
        :param var_info: Additional variable information, defaults to None.
        :param comment: Comments about the game, defaults to None.
        :param version: Version information, defaults to None.
        """
        if self.f.tell() != 0:
            self.f.write('/\n')
        if version:
                self.f.write(version)
                self.f.write('\n')
        if names:
            for name, turn in zip(names, ['+', '-']):
                self.f.write('N' + turn + name)
                self.f.write('\n')
        if comment:
            if comment[0] == "'":
                self.f.write(comment)
            else:
                self.f.write("'")
                self.f.write(comment)
                self.f.write('\n')
        if var_info:
            for k, v in var_info.items():
                self.f.write('$' + k + ':' + v)
                self.f.write('\n')
        if init_board:
            if type(init_board) is str:
                if init_board == cshogi.STARTING_SFEN:
                    self.f.write('PI\n+\n')
                    self.turn = cshogi.BLACK
                    return
                board = cshogi.Board(sfen=init_board)
            else:
                board = init_board
            csa_pos = board.csa_pos()
            if csa_pos == 'P1-KY-KE-GI-KI-OU-KI-GI-KE-KY\nP2 * -HI *  *  *  *  * -KA * \nP3-FU-FU-FU-FU-FU-FU-FU-FU-FU\nP4 *  *  *  *  *  *  *  *  * \nP5 *  *  *  *  *  *  *  *  * \nP6 *  *  *  *  *  *  *  *  * \nP7+FU+FU+FU+FU+FU+FU+FU+FU+FU\nP8 * +KA *  *  *  *  * +HI * \nP9+KY+KE+GI+KI+OU+KI+GI+KE+KY\n+\n':
                self.f.write('PI\n+\n')
            else:
                self.f.write(csa_pos)
            self.turn = init_board.turn
        else:
            self.f.write('PI\n+\n')
            self.turn = cshogi.BLACK

    def move(self, move: int, time: Optional[int] = None, comment: Optional[str] = None, sep: str = '\n'):
        """Write a move to the file.

        :param move: The move to write.
        :param time: The time taken for the move, defaults to None.
        :param comment: A comment about the move, defaults to None.
        :param sep: Separator character, defaults to newline.
        """
        self.f.write(COLOR_SYMBOLS[self.turn])
        self.f.write(cshogi.move_to_csa(move))
        if time is not None:
            self.f.write(sep)
            self.f.write('T' + str(time))
        if comment:
            self.f.write(sep)
            self.f.write("'" + comment)
        self.f.write('\n')
        self.turn = cshogi.opponent(self.turn)

    def endgame(self, endgame: str, time: Optional[int] = None):
        """Write the endgame result to the file.

        :param endgame: The result of the endgame.
        :param time: The time taken for the endgame, defaults to None.
        """
        self.f.write(endgame)
        self.f.write('\n')
        if time is not None:
            self.f.write('T' + str(time))
            self.f.write('\n')
        self.f.flush()
