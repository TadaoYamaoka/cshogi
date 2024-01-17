# This file is part of the python-shogi library.
# Copyright (C) 2015- Tasuku SUENAGA <tasuku-s-github@titech.ac>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.

from typing import List, Union, Optional
import os
import re
import codecs
from datetime import datetime
import math

import cshogi
from cshogi import Board, BLACK_WIN, WHITE_WIN, DRAW, move_to

KIFU_TO_SQUARE_NAMES = [
    '１一', '１二', '１三', '１四', '１五', '１六', '１七', '１八', '１九',
    '２一', '２二', '２三', '２四', '２五', '２六', '２七', '２八', '２九',
    '３一', '３二', '３三', '３四', '３五', '３六', '３七', '３八', '３九',
    '４一', '４二', '４三', '４四', '４五', '４六', '４七', '４八', '４九',
    '５一', '５二', '５三', '５四', '５五', '５六', '５七', '５八', '５九',
    '６一', '６二', '６三', '６四', '６五', '６六', '６七', '６八', '６九',
    '７一', '７二', '７三', '７四', '７五', '７六', '７七', '７八', '７九',
    '８一', '８二', '８三', '８四', '８五', '８六', '８七', '８八', '８九',
    '９一', '９二', '９三', '９四', '９五', '９六', '９七', '９八', '９九',
]

KIFU_FROM_SQUARE_NAMES = [
    '11', '12', '13', '14', '15', '16', '17', '18', '19',
    '21', '22', '23', '24', '25', '26', '27', '28', '29',
    '31', '32', '33', '34', '35', '36', '37', '38', '39',
    '41', '42', '43', '44', '45', '46', '47', '48', '49',
    '51', '52', '53', '54', '55', '56', '57', '58', '59',
    '61', '62', '63', '64', '65', '66', '67', '68', '69',
    '71', '72', '73', '74', '75', '76', '77', '78', '79',
    '81', '82', '83', '84', '85', '86', '87', '88', '89',
    '91', '92', '93', '94', '95', '96', '97', '98', '99',
]

PIECE_BOD_SYMBOLS = [
    ' ・', ' 歩', ' 香', ' 桂', ' 銀', ' 角', ' 飛', ' 金', 
    ' 玉', ' と', ' 杏', ' 圭', ' 全', ' 馬', ' 龍', 
    '', 
    ' ・', 'v歩', 'v香', 'v桂', 'v銀', 'v角', 'v飛', 'v金', 
    'v玉', 'vと', 'v杏', 'v圭', 'v全', 'v馬', 'v龍'
]

class ParserException(Exception):
    pass

class Parser:
    """A class for parsing Japanese Shogi notation in KIF format."""

    MOVE_RE = re.compile(r'\A *[0-9]+\s+(中断|投了|持将棋|千日手|詰み|切れ負け|反則勝ち|反則負け|(([１２３４５６７８９])([零一二三四五六七八九])|同　)([歩香桂銀金角飛玉と杏圭全馬龍])(打|(成?)\(([0-9])([0-9])\)))\s*(\( *([:0-9]+)/([:0-9]+)\))?.*\Z')

    HANDYCAP_SFENS = {
        '平手': cshogi.STARTING_SFEN,
        '香落ち': 'lnsgkgsn1/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1',
        '右香落ち': '1nsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1',
        '角落ち': 'lnsgkgsnl/1r7/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1',
        '飛車落ち': 'lnsgkgsnl/7b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1',
        '飛香落ち': 'lnsgkgsn1/7b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1',
        '二枚落ち': 'lnsgkgsnl/9/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1',
        '三枚落ち': 'lnsgkgsn1/9/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1',
        '四枚落ち': '1nsgkgsn1/9/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1',
        '五枚落ち': '2sgkgsn1/9/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1',
        '左五枚落ち': '1nsgkgs2/9/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1',
        '六枚落ち': '2sgkgs2/9/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1',
        '八枚落ち': '3gkg3/9/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1',
        '十枚落ち': '4k4/9/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1',
        'その他': None
    }

    RESULT_RE = re.compile(r'　*まで、?(\d+)手で((先|下|後|上)手の(勝ち|入玉勝ち|反則勝ち|反則負け)|千日手|持将棋|中断)')

    @staticmethod
    def parse_file(path: str) -> "Parser":
        """Parses a KI2 format Shogi game notation file.

        :param path: Path to the file containing the KIF notation.
        :return: An instance of the Parser class containing all the extracted information.
        :raises KIF.ParserException: In the case of a parse error.
        """
        prefix, ext = os.path.splitext(path)
        enc = 'utf-8' if ext == '.kifu' else 'cp932'
        with codecs.open(path, 'r', enc) as f:
            return Parser.parse_str(f.read())

    @staticmethod
    def parse_pieces_in_hand(target):
        """Parses pieces in hand from a given string.

        :param target: String containing the description of the pieces in hand.
        :return: A dictionary representing the pieces in hand.
        :raises KIF.ParserException: In the case of a parse error.
        """
        if target == 'なし': # None in japanese
            return {}

        result = {}
        for item in target.split('　'):
            if len(item) == 1:
                result[cshogi.PIECE_JAPANESE_SYMBOLS.index(item)] = 1
            elif len(item) == 2 or len(item) == 3:
                result[cshogi.PIECE_JAPANESE_SYMBOLS.index(item[0])] = \
                    cshogi.NUMBER_JAPANESE_KANJI_SYMBOLS.index(item[1:])
            elif len(item) == 0:
                pass
            else:
                raise ParserException('Invalid pieces in hand')
        return result

    @staticmethod
    def parse_move_str(line: str, board: Board):
        """Parses a string of moves and applies them to a given board.

        :param line: String containing the moves in Japanese Shogi notation.
        :param board: Board object to apply the moves to.
        :return: A list of moves parsed from the line.
        """
        # Normalize king/promoted kanji
        line = line.replace('王', '玉')
        line = line.replace('竜', '龍')
        line = line.replace('成銀', '全')
        line = line.replace('成桂', '圭')
        line = line.replace('成香', '杏')

        m = Parser.MOVE_RE.match(line)
        if m:
            if m.group(11):
                time = 0
                for i, t in enumerate(reversed(m.group(11).split(':'))):
                    time += int(t) * 60**i
            else:
                time = None
            if m.group(1) not in [
                    '入玉勝ち',
                    '中断',
                    '投了',
                    '持将棋',
                    '千日手',
                    '詰み',
                    '切れ負け',
                    '反則勝ち',
                    '反則負け'
                ]:
                piece_type = cshogi.PIECE_JAPANESE_SYMBOLS.index(m.group(5))
                if m.group(2) == '同　':
                    # same position
                    to_square = move_to(board.peek())
                else:
                    to_field = cshogi.NUMBER_JAPANESE_NUMBER_SYMBOLS.index(m.group(3)) - 1
                    to_rank = cshogi.NUMBER_JAPANESE_KANJI_SYMBOLS.index(m.group(4)) - 1
                    to_square = to_rank + to_field * 9

                if m.group(6) == '打' or (m.group(8) == '0' and m.group(9) == '0'):
                    # piece drop
                    return board.drop_move(to_square, piece_type), time, None
                else:
                    from_field = int(m.group(8)) - 1
                    from_rank = int(m.group(9)) - 1
                    from_square = from_rank + from_field * 9

                    promotion = (m.group(7) == '成')
                    return board.move(from_square, to_square, promotion), time, None
            else:
                return None, time, m.group(1)
        return None, None, None

    @staticmethod
    def parse_str(kif_str):
        """Parses a KIF formatted string into a Parser object.

        :param kif_str: The KIF formatted string representing the Shogi game.
        :return: An instance of the Parser class containing all the extracted information.
        :raises KIF.ParserException: In the case of a parse error.
        """
        line_no = 1

        starttime = None
        names = [None, None]
        pieces_in_hand = [{}, {}]
        sfen = cshogi.STARTING_SFEN
        var_info = {}
        header_comments = []
        moves = []
        times = []
        comments = []
        win = None
        endgame = None
        board = Board()
        kif_str = kif_str.replace('\r\n', '\n').replace('\r', '\n')
        for line in kif_str.split('\n'):
            if len(line) == 0:
                pass
            elif line[0] == "*":
                if len(moves) > 0:
                    if len(moves) - len(comments) > 1:
                        comments.extend([None]*(len(moves) - len(comments) - 1))
                    if line[:2] == "**":
                        comment = line[2:]
                    else:
                        comment = line[1:]
                    if len(comments) == len(moves):
                        comments[-1] += "\n" + comment
                    else:
                        comments.append(comment)
                else:
                    header_comments.append(line[1:])
            elif '：' in line:
                (key, value) = line.split('：', 1)
                value = value.rstrip('　')
                if key == '開始日時':
                    try:
                        starttime = datetime.strptime(value, '%Y/%m/%d %H:%M:%S')
                    except ValueError:
                        try:
                            # if KIF file has not second information, try another parse
                            starttime = datetime.strptime(value, '%Y/%m/%d %H:%M') 
                        except ValueError:
                            pass

                if key == '先手' or key == '下手': # sente or shitate
                    # Blacks's name
                    names[cshogi.BLACK] = value
                elif key == '後手' or key == '上手': # gote or uwate
                    # White's name
                    names[cshogi.WHITE] = value
                elif key == '先手の持駒' or key == '下手の持駒': # sente or shitate's pieces in hand
                    # First player's pieces in hand
                    pieces_in_hand[cshogi.BLACK] == Parser.parse_pieces_in_hand(value)
                elif key == '後手の持駒' or key == '上手の持駒': # gote or uwate's pieces in hand
                    # Second player's pieces in hand
                    pieces_in_hand[cshogi.WHITE] == Parser.parse_pieces_in_hand(value)
                elif key == '手合割': # teai wari
                    sfen = Parser.HANDYCAP_SFENS[value]
                    if sfen is None:
                        raise ParserException('Cannot support handycap type "other"')
                    board.set_sfen(sfen)
                else:
                    var_info[key] = value
            else:
                move, time, endmove = Parser.parse_move_str(line, board)
                if move is not None:
                    moves.append(move)
                    if board.is_legal(move):
                        board.push(move)
                    if time is not None:
                        times.append(time)
                elif time is not None:
                    times.append(time)
                else:
                    m = Parser.RESULT_RE.match(line)
                    if m:
                        win_side_str = m.group(3)
                        if win_side_str == '先' or win_side_str == '下':
                            if m.group(4) == '反則負け':
                                win = WHITE_WIN
                                endgame = '%ILLEGAL_MOVE'
                            else:
                                win = BLACK_WIN
                                endgame = '%+ILLEGAL_ACTION' if m.group(4) == '反則勝ち' else ('%KACHI' if m.group(4) == '入玉勝ち' else '%TORYO')
                        elif win_side_str == '後' or win_side_str == '上':
                            if m.group(4) == '反則負け':
                                win = BLACK_WIN
                                endgame = '%ILLEGAL_MOVE'
                            else:
                                win = WHITE_WIN
                                endgame = '%-ILLEGAL_ACTION' if m.group(4) == '反則勝ち' else ('%KACHI' if m.group(4) == '入玉勝ち' else '%TORYO')
                        elif m.group(2) == '中断':
                            win = None
                            endgame = '%CHUDAN'
                        else:
                            # TODO: repetition of moves with continuous check
                            win = DRAW
                            endgame = '%SENNICHITE'
                        # 変化には対応していないため、終局以降の行は読まない
                        break
            line_no += 1

        parser = Parser()
        parser.starttime = starttime
        parser.names = names
        parser.sfen = sfen
        parser.var_info = var_info
        parser.comment = '\n'.join(header_comments)
        parser.moves = moves
        parser.times = times
        parser.comments = comments
        parser.win = win
        parser.endgame = endgame

        return parser

def sec_to_time(sec):
    h, m_ = divmod(math.ceil(sec), 60*60)
    m, s = divmod(m_, 60)
    return h, m, s

def move_to_kif(move: int, prev_move: Optional[int] = None) -> str:
    """Convert a given move to Japanese KIF notation.

    :param move: An integer representing the move.
    :param board: A Board object representing the current state of the game.
    :return: A string representing the move in KIF notation.
    """
    to_sq = cshogi.move_to(move)
    move_to = KIFU_TO_SQUARE_NAMES[to_sq]
    if prev_move:
        if cshogi.move_to(prev_move) == to_sq:
            move_to = "同　"
    if not cshogi.move_is_drop(move):
        from_sq = cshogi.move_from(move)
        move_piece = cshogi.PIECE_JAPANESE_SYMBOLS[cshogi.move_from_piece_type(move)]
        if cshogi.move_is_promotion(move):
            return '{}{}成({})'.format(
                move_to,
                move_piece,
                KIFU_FROM_SQUARE_NAMES[from_sq],
                )
        else:
            return '{}{}({})'.format(
                move_to,
                move_piece,
                KIFU_FROM_SQUARE_NAMES[from_sq],
                )
    else:
        move_piece = cshogi.HAND_PIECE_JAPANESE_SYMBOLS[cshogi.move_drop_hand_piece(move)]
        return '{}{}打'.format(
            move_to,
            move_piece
            )

def board_to_bod(board: Board) -> str:
    """Convert a given board to a Board Diagram (BOD) representation.

    :param board: A Board object representing the current state of the game.
    :return: A string representing the Board Diagram (BOD) of the game.
    """
    def hand_pieces_str(color):
        if any(board.pieces_in_hand[color]):
            str_list = []
            for symbol, n in zip(reversed(cshogi.HAND_PIECE_JAPANESE_SYMBOLS), reversed(board.pieces_in_hand[color])):
                if n > 1:
                    str_list.append(symbol + cshogi.NUMBER_JAPANESE_KANJI_SYMBOLS[n])
                elif n == 1:
                    str_list.append(symbol)
            return '　'.join(str_list)
        else:
            return 'なし'

    str_list = [
        '後手の持駒：' + hand_pieces_str(cshogi.WHITE),
        '  ９ ８ ７ ６ ５ ４ ３ ２ １',
        '+---------------------------+'
    ]
    str_list.extend(
        ['|' + ''.join([PIECE_BOD_SYMBOLS[board.piece(f * 9 + r)] for f in reversed(range(9))]) + '|' + cshogi.NUMBER_JAPANESE_KANJI_SYMBOLS[r + 1] for r in range(9)]
    )
    str_list.append('+---------------------------+')
    str_list.append('先手の持駒：' + hand_pieces_str(cshogi.BLACK))
    if board.turn == cshogi.WHITE:
        str_list.append('後手番')

    return '\n'.join(str_list)

def move_to_bod(move: int, board: Board) -> str:
    """Convert a given move to a Board Diagram (BOD) representation.

    :param move: An integer representing a specific move.
    :param board: A Board object representing the current state of the Shogi game.
    :return: A string representing the move in Board Diagram (BOD) format.
    """
    import cshogi.KI2
    move_str = cshogi.KI2.move_to_ki2(move, board)
    if move_str[1] == '同':
        to_sq = cshogi.move_to(move)
        return move_str[0] + KIFU_TO_SQUARE_NAMES[to_sq] + '同' + move_str[3:]
    else:
        return move_str

class Exporter:
    """A class to handle the exporting of a game to KIF format.

    :param path: Optional path to the file where the KIF formatted game will be written. If None, no file is opened initially.
    """

    def __init__(self, path: Optional[str] = None):
        if path:
            self.open(path)
        else:
            self.kifu = None

    def open(self, path: str):
        """Open a file for writing the KIF formatted game.

        :param path: Path to the file.
        """
        _, ext = os.path.splitext(path)
        enc = 'utf-8' if ext == '.kifu' else 'cp932'
        self.kifu = open(path, 'w', encoding=enc)
        self.prev_move = None
        self.move_number = 1

    def close(self):
        """Close the file."""
        self.kifu.close()

    def header(self, names: List[str], starttime: Optional[datetime] = None, handicap: Optional[Union[str, Board]] = None):
        """Write the header information to the file.

        :param names: List of player names.
        :param starttime: Start time of the game, defaults to current time.
        :param handicap: Handicap settings for the game.
        """
        if starttime is None:
            starttime = datetime.now()
        self.kifu.write('開始日時：' + starttime.strftime('%Y/%m/%d %H:%M:%S\n'))
        if handicap is None:
            self.kifu.write('手合割：平手\n')
        elif type(handicap) is Board or type(handicap) is str and handicap[:5] == 'sfen ':
            if type(handicap) is Board:
                board = handicap
            else:
                board = Board(sfen=handicap[5:])
            self.kifu.write(board_to_bod(board) + '\n')
        else:
            self.kifu.write('手合割：' + handicap + '\n')
        self.kifu.write('先手：' + names[0] + '\n')
        self.kifu.write('後手：' + names[1] + '\n')
        self.kifu.write('手数----指手---------消費時間--\n')

    def move(self, move: int, sec: int = 0, sec_sum: int = 0):
        """Record a move in the game.

        :param move: The move to record.
        :param sec: Seconds spent on the move.
        :param sec_sum: Total seconds spent so far.
        """
        m, s = divmod(math.ceil(sec), 60)
        h_sum, m_sum, s_sum = sec_to_time(sec_sum)

        if cshogi.move_is_drop(move):
            padding = '    '
        elif cshogi.move_is_promotion(move):
            padding = ''
        else:
            padding = '  '
        move_str = move_to_kif(move, self.prev_move) + padding

        self.kifu.write('{:>4} {}      ({:>2}:{:02}/{:02}:{:02}:{:02})\n'.format(
            self.move_number,
            move_str,
            m, s,
            h_sum, m_sum, s_sum))

        self.move_number += 1
        self.prev_move = move

    def end(self, reason: str, sec: int = 0, sec_sum: int = 0):
        """Record the end of the game.

        :param reason: The reason for the end of the game  (e.g., resign, sennichite).
        :param sec: Seconds spent on the last move.
        :param sec_sum: Total seconds spent during the game.
        """
        m, s = divmod(math.ceil(sec), 60)
        h_sum, m_sum, s_sum = sec_to_time(sec_sum)

        if reason == 'resign':
            move_str = '投了        '
        elif reason == 'win':
            move_str = '入玉宣言    '
        elif reason == 'draw':
            move_str = '持将棋      '
        elif reason == 'sennichite':
            move_str = '千日手      '
        elif reason == 'illegal_win':
            move_str = '反則勝ち    '
        elif reason == 'illegal_lose':
            move_str = '反則負け    '

        self.kifu.write('{:>4} {}      ({:>2}:{:02}/{:02}:{:02}:{:02})\n'.format(
            self.move_number,
            move_str,
            m, s,
            h_sum, m_sum, s_sum))

        # 結果出力
        if reason == 'resign':
            self.kifu.write('まで{}手で{}の勝ち\n'.format(self.move_number - 1, '先手' if self.move_number % 2 == 0 else '後手'))
        elif reason == 'draw':
            self.kifu.write('まで{}手で持将棋\n'.format(self.move_number + 1))
        elif reason == 'win':
            self.kifu.write('まで{}手で入玉宣言\n'.format(self.move_number - 1))
        elif reason == 'sennichite':
            self.kifu.write('まで{}手で千日手\n'.format(self.move_number - 1))
        elif reason == 'illegal_win':
            self.kifu.write('まで{}手で{}の反則勝ち\n'.format(self.move_number - 1, '先手' if self.move_number % 2 == 0 else '後手'))
        elif reason == 'illegal_lose':
            self.kifu.write('まで{}手で{}の反則負け\n'.format(self.move_number - 1, '先手' if self.move_number % 2 == 0 else '後手'))

    def info(self, info):
        """Record additional information related to the game.

        :param info: A string containing additional information.
        """
        turn = self.move_number % 2
        items = info.split(' ')
        comment = '**対局'
        i = 1
        while i < len(items):
            if items[i] == 'time':
                i += 1
                m, s = divmod(int(items[i]) / 1000, 60)
                s_str = '{:.1f}'.format(s)
                if s_str[1:2] == '.':
                    s_str = '0' + s_str
                comment += ' 時間 {:>02}:{}'.format(int(m), s_str)
            elif items[i] == 'depth':
                i += 1
                comment += ' 深さ {}'.format(items[i])
            elif items[i] == 'nodes':
                i += 1
                comment += ' ノード数 {}'.format(items[i])
            elif items[i] == 'score':
                i += 1
                if items[i] == 'cp':
                    i += 1
                    comment += ' 評価値 {}'.format(items[i] if turn == cshogi.BLACK else -int(items[i]))
                elif items[i] == 'mate':
                    i += 1
                    if items[i][0:1] == '+':
                        comment += ' +詰' if turn == cshogi.BLACK else ' -詰'
                    else:
                        comment += ' -詰' if turn == cshogi.BLACK else ' +詰'
                    comment += str(items[i][1:])
            else:
                i += 1
        self.kifu.write(comment + '\n')
