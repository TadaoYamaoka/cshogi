from typing import Dict, List, Optional
import re
from datetime import datetime

import cshogi
from cshogi import Board, KIF, BLACK_WIN, WHITE_WIN, DRAW, move_to, move_from, move_is_drop, move_is_promotion, move_from_piece_type, move_drop_hand_piece, hand_piece_to_piece_type

class Parser:
    """A class for parsing Japanese Shogi notation in KI2 format."""

    MOVE_RE = re.compile(r'(▲|△)(([１２３４５６７８９])([零一二三四五六七八九])|同　?)([歩香桂銀金角飛玉と杏圭全馬龍])(左|直|右)?(上|寄|引)?(打|成|不成)?\s*')

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

    RESULT_RE = re.compile(r'まで(\d+)手で((先|下|後|上)手の(勝ち|入玉勝ち|反則勝ち|反則負け)|千日手|持将棋|中断)')

    @staticmethod
    def parse_file(path: str) -> "Parser":
        """Parses a KI2 format Shogi game notation file.

        :param path: Path to the file containing the KI2 notation.
        :return: An instance of the Parser class containing all the extracted information.
        :raises KIF.ParserException: In the case of a parse error.
        """
        with open(path, 'r', encoding='cp932') as f:
            return Parser.parse_str(f.read())

    @staticmethod
    def parse_pieces_in_hand(target: str) -> Dict:
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
                raise KIF.ParserException('Invalid pieces in hand')
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

        moves = []
        pos = 0
        while True:
            m = Parser.MOVE_RE.match(line[pos:])
            if m:
                piece_type = cshogi.PIECE_JAPANESE_SYMBOLS.index(m.group(5))
                if m.group(2).rstrip('　') == '同':
                    # same position
                    to_square = move_to(board.peek())
                else:
                    to_file = cshogi.NUMBER_JAPANESE_NUMBER_SYMBOLS.index(m.group(3)) - 1
                    to_rank = cshogi.NUMBER_JAPANESE_KANJI_SYMBOLS.index(m.group(4)) - 1
                    to_square = to_rank + to_file * 9

                # 候補手
                candidates = [move for move in board.legal_moves if move_to(move) == to_square and (not move_is_drop(move) and move_from_piece_type(move) == piece_type or move_is_drop(move) and hand_piece_to_piece_type(move_drop_hand_piece(move)) == piece_type)]

                if m.group(6) or m.group(7):
                    from_file_min = from_rank_min = 9
                    from_file_max = from_rank_max = 0
                    for move in candidates:
                        from_file, from_rank = divmod(move_from(move), 9)
                        if from_file < from_file_min:
                            from_file_min = from_file
                        if from_file > from_file_max:
                            from_file_max = from_file
                        if from_rank < from_rank_min:
                            from_rank_min = from_rank
                        if from_rank > from_rank_max:
                            from_rank_max = from_rank

                    if m.group(6) == '左':
                        if board.turn == cshogi.BLACK:
                            candidates = [move for move in candidates if move_from(move) // 9 == from_file_max]
                        else:
                            candidates = [move for move in candidates if move_from(move) // 9 == from_file_min]
                    elif m.group(6) == '右':
                        if board.turn == cshogi.BLACK:
                            candidates = [move for move in candidates if move_from(move) // 9 == from_file_min]
                        else:
                            candidates = [move for move in candidates if move_from(move) // 9 == from_file_max]
                    elif m.group(6) == '直':
                        candidates = [move for move in candidates if move_to(move) // 9 == move_from(move) // 9]

                    if m.group(7) == '上':
                        if board.turn == cshogi.BLACK:
                            candidates = [move for move in candidates if move_from(move) % 9 == from_rank_max]
                        else:
                            candidates = [move for move in candidates if move_from(move) % 9 == from_rank_min]
                    elif m.group(7) == '引':
                        if board.turn == cshogi.BLACK:
                            candidates = [move for move in candidates if move_from(move) % 9 == from_rank_min]
                        else:
                            candidates = [move for move in candidates if move_from(move) % 9 == from_rank_max]
                    elif m.group(7) == '寄':
                        candidates = [move for move in candidates if move_to(move) % 9 == move_from(move) % 9]

                if m.group(8) == '打':
                    candidates = [move for move in candidates if move_is_drop(move)]
                if m.group(8) == '成':
                    candidates = [move for move in candidates if move_is_promotion(move)]
                if m.group(8) == '不成':
                    candidates = [move for move in candidates if not move_is_promotion(move)]

                if len(candidates) > 1:
                    candidates = [move for move in candidates if not move_is_drop(move)]
                    assert len(candidates) == 1
                move = candidates[0]
                moves.append(move)
                if board.is_legal(move):
                    board.push(move)
                pos = m.end()
            else:
                break
        return moves

    @staticmethod
    def parse_str(kif_str: str) -> "Parser":
        """Parses a KI2 formatted string into a Parser object.

        :param kif_str: The KI2 formatted string representing the Shogi game.
        :return: An instance of the Parser class containing all the extracted information.
        :raises KIF.ParserException: In the case of a parse error.
        """
        line_no = 1

        starttime = None
        names = [None, None]
        sfen = cshogi.STARTING_SFEN
        var_info = {}
        header_comments = []
        moves = []
        comments = []
        win = None
        endgame = ''
        kif_str = kif_str.replace('\r\n', '\n').replace('\r', '\n')
        board = Board()
        for line in kif_str.split('\n'):
            if len(line) == 0:
                pass
            elif line[0] == "*":
                if len(moves) > 0:
                    if len(moves) - len(comments) > 1:
                        comments.extend([None]*(len(moves) - len(comments) - 1))
                    if line[:2] == "**":
                        comments.append(line[2:])
                    else:
                        comments.append(line[1:])
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
                elif key == '先手' or key == '下手': # sente or shitate
                    # Blacks's name
                    names[cshogi.BLACK] = value
                elif key == '後手' or key == '上手': # gote or uwate
                    # White's name
                    names[cshogi.WHITE] = value
                elif key == '手合割': # teai wari
                    sfen = Parser.HANDYCAP_SFENS[value]
                    if sfen is None:
                        raise KIF.ParserException('Cannot support handycap type "other"')
                    board.set_sfen(sfen)
                else:
                    var_info[key] = value
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
                    elif m.group(2) == '持将棋':
                        win = DRAW
                        endgame = '%JISHOGI'
                    elif m.group(2) == '中断':
                        win = None
                        endgame = '%CHUDAN'
                    else:
                        # TODO: repetition of moves with continuous check
                        win = DRAW
                        endgame = '%SENNICHITE'
                    break

                for move in Parser.parse_move_str(line, board):
                    moves.append(move)
            line_no += 1

        if len(moves) - len(comments) > 0:
            comments.extend([None]*(len(moves) - len(comments)))

        parser = Parser()
        parser.starttime = starttime
        parser.names = names
        parser.sfen = sfen
        parser.var_info = var_info
        parser.comment = '\n'.join(header_comments)
        parser.moves = moves
        parser.comments = comments
        parser.win = win
        parser.endgame = endgame

        return parser

PIECE_JAPANESE_SYMBOLS2 = [
    '',
    '歩', '香', '桂', '銀', '角', '飛', '金', '玉', 'と', '成香', '成桂', '成銀', '馬', '龍'
]

def move_to_ki2(move: int, board: Board) -> str:
    """Convert a given move to Japanese KI2 notation.

    :param move: An integer representing the move.
    :param board: A Board object representing the current state of the game.
    :return: A string representing the move in KI2 notation.
    """
    kifu_turn = '▲' if board.turn == cshogi.BLACK else '△'
    to_square = move_to(move)
    kifu_to = KIF.KIFU_TO_SQUARE_NAMES[to_square]
    if board.peek() != cshogi.MOVE_NONE:
        if move_to(board.peek()) == to_square:
            kifu_to = "同"
    if not move_is_drop(move):
        relative = '' # 駒の相対位置
        motion = '' # 駒の動作
        from_square = move_from(move)
        piece_type = move_from_piece_type(move)
        kifu_piece = PIECE_JAPANESE_SYMBOLS2[piece_type]
        candidates = []
        promotion = {}
        for move2 in board.pseudo_legal_moves:
            if move_to(move2) == to_square and not move_is_drop(move2) and move_from_piece_type(move2) == piece_type:
                from_square2 = move_from(move2)
                if from_square2 in promotion:
                    promotion[from_square2] += 1
                    continue
                promotion[from_square2] = 1
                candidates.append(move2)
        if len(candidates) > 1:
            # 後手は180度回転
            to_file, to_rank = divmod(to_square if board.turn == cshogi.BLACK else 80 - to_square, 9)
            from_file, from_rank = divmod(from_square if board.turn == cshogi.BLACK else 80 - from_square, 9)
            candidates_left = candidates_right = candidates_up = candidates_down = candidates_side = 0
            for move2 in candidates:
                from_square2 = move_from(move2)
                # 後手は180度回転
                from_file2, from_rank2 = divmod(from_square2 if board.turn == cshogi.BLACK else 80 - from_square2, 9)
                if from_file2 > to_file: # 左
                    candidates_left += 1
                elif from_file2 < to_file: # 右
                    candidates_right += 1

                if from_rank2 > to_rank: # 上
                    candidates_up += 1
                elif from_rank2 < to_rank: # 引
                    candidates_down += 1
                else: # 寄
                    candidates_side += 1

            if from_file == to_file and from_rank > to_rank: # 直
                relative = '直'
            else:
                if from_rank > to_rank: # 上
                    if candidates_up == 1:
                        motion = '上'
                    elif from_file > to_file: # 左
                        relative = '左'
                        if candidates_left > 1:
                            motion = '上'
                    else: # 右
                        relative = '右'
                        if candidates_right > 1:
                            motion = '上'
                elif from_rank < to_rank: # 引
                    if candidates_down == 1:
                        motion = '引'
                    elif from_file > to_file: # 左
                        relative = '左'
                        if candidates_left > 1:
                            motion = '引'
                    else: # 右
                        relative = '右'
                        if candidates_right > 1:
                            motion = '上'
                else: # 寄
                    if candidates_side == 1:
                        motion = '寄'
                    elif from_file > to_file: # 左
                        relative = '左'
                        if candidates_left > 1:
                            motion = '寄'
                    else: # 右
                        relative = '右'
                        if candidates_right > 1:
                            motion = '寄'

        move_str = '{}{}{}{}{}{}'.format(
            kifu_turn,
            kifu_to,
            kifu_piece,
            relative,
            motion,
            '成' if move_is_promotion(move) else ('不成' if promotion[from_square] == 2 else '')
            )

    else: # 打
        hand_peice = move_drop_hand_piece(move)
        piece_type = hand_piece_to_piece_type(hand_peice)
        candidates = [move for move in board.legal_moves if move_to(move) == to_square and not move_is_drop(move) and move_from_piece_type(move) == piece_type]
        kifu_piece = cshogi.HAND_PIECE_JAPANESE_SYMBOLS[hand_peice]
        move_str = '{}{}{}{}'.format(
            kifu_turn,
            kifu_to,
            kifu_piece,
            '打' if len(candidates) > 0 else ''
            )
    if move_str[1] == '同' and len(move_str) == 3:
        move_str = move_str[:2] + '　' + move_str[2]
    return move_str

class Exporter:
    """A class to handle the exporting of a game to KI2 format.

    :param path: Optional path to the file where the KI2 formatted game will be written. If None, no file is opened initially.
    """

    def __init__(self, path: Optional[str] = None):
        if path:
            self.open(path)
        else:
            self.kifu = None

    def open(self, path: str):
        """Open a file for writing the KI2 formatted game.

        :param path: Path to the file.
        """
        self.kifu = open(path, 'w', encoding='cp932')
        self.prev_move = None
        self.move_number = 1
        self.board = Board()

    def close(self):
        """Close the file."""
        self.kifu.close()

    def header(self, names: List[str], starttime: Optional[datetime] = None):
        """Write the game's header information, including date and player names.

        :param names: A list containing the names of the players [first_player, second_player].
        :param starttime: Optional start time of the game. If None, the current time is used.
        """
        if starttime is None:
            starttime = datetime.now()
        self.kifu.write('開始日時：' + starttime.strftime('%Y/%m/%d %H:%M:%S\n'))
        self.kifu.write('手合割：平手\n')
        self.kifu.write('先手：' + names[0] + '\n')
        self.kifu.write('後手：' + names[1] + '\n\n')

    def move(self, move: int, comment: Optional[str] = None):
        """Write a move to the file in KI2 format.

        :param move: An integer representing the move.
        :param comment: Optional comment string to be associated with the move.
        """
        move_str = move_to_ki2(move, self.board)
        self.board.push(move)

        self.kifu.write('{}\n'.format(move_str))
        if comment:
            self.kifu.write('**{}\n'.format(comment))

    def end(self, reason: str):
        """Write the game's ending condition.

        :param reason: A string representing the reason for the game's end (e.g., %TORYO, %SENNICHITE).
        """
        # 結果出力
        if reason == '%TORYO':
            self.kifu.write('まで{}手で{}の勝ち\n'.format(self.board.move_number - 1, '先手' if self.board.turn == cshogi.WHITE else '後手'))
        elif reason == '%JISHOGI':
            self.kifu.write('まで{}手で持将棋\n'.format(self.board.move_number + 1))
        elif reason == '%KACHI':
            self.kifu.write('まで{}手で{}の入玉勝ち\n'.format(self.board.move_number - 1, '先手' if self.board.turn == cshogi.BLACK else '後手'))
        elif reason == '%SENNICHITE':
            self.kifu.write('まで{}手で千日手\n'.format(self.board.move_number - 1))
        elif reason == '%+ILLEGAL_ACTION' or reason == '%-ILLEGAL_ACTION':
            self.kifu.write('まで{}手で{}の反則勝ち\n'.format(self.board.move_number - 1, '先手' if self.board.turn == cshogi.WHITE else '後手'))
        elif reason == '%ILLEGAL_MOVE':
            self.kifu.write('まで{}手で{}の反則負け\n'.format(self.move_number - 1, '先手' if self.board.turn == cshogi.WHITE else '後手'))
