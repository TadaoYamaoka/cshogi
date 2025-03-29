from libcpp.string cimport string
from libcpp.vector cimport vector
from libcpp cimport bool

import numpy as np
cimport numpy as np

import locale

dtypeHcp = np.dtype((np.uint8, 32))
dtypeEval = np.dtype(np.int16)
dtypeMove16 = np.dtype(np.int16)
dtypeGameResult = np.dtype(np.int8)

HuffmanCodedPos = np.dtype([
    ('hcp', dtypeHcp),
    ])

HuffmanCodedPosAndEval = np.dtype([
    ('hcp', dtypeHcp),
    ('eval', dtypeEval),
    ('bestMove16', dtypeMove16),
    ('gameResult', dtypeGameResult),
    ('dummy', np.uint8),
    ])

PackedSfen = np.dtype([
    ('sfen', np.uint8, 32),
    ])

PackedSfenValue = np.dtype([
    ('sfen', np.uint8, 32),
    ('score', np.int16),
    ('move', np.uint16),
    ('gamePly', np.uint16),
    ('game_result', np.int8),
    ('padding', np.uint8),
    ])

dtypeKey = np.dtype(np.uint64)
BookEntry = np.dtype([
    ('key', dtypeKey),
    ('fromToPro', dtypeMove16),
    ('count', np.uint16),
    ('score', np.int32),
    ])


STARTING_SFEN = 'lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1'

SQUARES = [
    A1, B1, C1, D1, E1, F1, G1, H1, I1,
    A2, B2, C2, D2, E2, F2, G2, H2, I2,
    A3, B3, C3, D3, E3, F3, G3, H3, I3,
    A4, B4, C4, D4, E4, F4, G4, H4, I4,
    A5, B5, C5, D5, E5, F5, G5, H5, I5,
    A6, B6, C6, D6, E6, F6, G6, H6, I6,
    A7, B7, C7, D7, E7, F7, G7, H7, I7,
    A8, B8, C8, D8, E8, F8, G8, H8, I8,
    A9, B9, C9, D9, E9, F9, G9, H9, I9,
] = range(81)

SQUARE_NAMES = [
    '1a', '1b', '1c', '1d', '1e', '1f', '1g', '1h', '1i',
    '2a', '2b', '2c', '2d', '2e', '2f', '2g', '2h', '2i',
    '3a', '3b', '3c', '3d', '3e', '3f', '3g', '3h', '3i',
    '4a', '4b', '4c', '4d', '4e', '4f', '4g', '4h', '4i',
    '5a', '5b', '5c', '5d', '5e', '5f', '5g', '5h', '5i',
    '6a', '6b', '6c', '6d', '6e', '6f', '6g', '6h', '6i',
    '7a', '7b', '7c', '7d', '7e', '7f', '7g', '7h', '7i',
    '8a', '8b', '8c', '8d', '8e', '8f', '8g', '8h', '8i',
    '9a', '9b', '9c', '9d', '9e', '9f', '9g', '9h', '9i',
]

COLORS = [BLACK, WHITE] = range(2)

GAME_RESULTS = [
    DRAW, BLACK_WIN, WHITE_WIN,
] = range(3)

PIECE_TYPES_WITH_NONE = [
           NONE,
           PAWN,      LANCE,      KNIGHT,      SILVER,
         BISHOP,       ROOK,
           GOLD,
           KING,
      PROM_PAWN, PROM_LANCE, PROM_KNIGHT, PROM_SILVER,
    PROM_BISHOP,  PROM_ROOK,
] = range(15)

PIECE_TYPES = [
           PAWN,      LANCE,      KNIGHT,      SILVER,
         BISHOP,       ROOK,
           GOLD,
           KING,
      PROM_PAWN, PROM_LANCE, PROM_KNIGHT, PROM_SILVER,
    PROM_BISHOP,  PROM_ROOK,
]

PIECES = [
           NONE,
          BPAWN,      BLANCE,      BKNIGHT,      BSILVER,
        BBISHOP,       BROOK,
          BGOLD,
          BKING,
     BPROM_PAWN, BPROM_LANCE, BPROM_KNIGHT, BPROM_SILVER,
   BPROM_BISHOP,  BPROM_ROOK,       NOTUSE,       NOTUSE,
          WPAWN,      WLANCE,      WKNIGHT,      WSILVER,
        WBISHOP,       WROOK,
          WGOLD,
          WKING,
     WPROM_PAWN, WPROM_LANCE, WPROM_KNIGHT, WPROM_SILVER,
   WPROM_BISHOP, WPROM_ROOK,
] = range(31)

HAND_PIECES = [
          HPAWN,     HLANCE,     HKNIGHT,     HSILVER,
          HGOLD,
        HBISHOP,      HROOK,
] = range(7)

MAX_PIECES_IN_HAND = [
    18, 4, 4, 4,
    4,
    2, 2,
]

MOVE_NONE = 0

REPETITION_TYPES = [
    NOT_REPETITION, REPETITION_DRAW, REPETITION_WIN, REPETITION_LOSE,
    REPETITION_SUPERIOR, REPETITION_INFERIOR
] = range(6)

SVG_PIECE_DEFS = [
    '<g id="black-pawn"><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="16.5">歩</text></g>',
    '<g id="black-lance"><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="16.5">香</text></g>',
    '<g id="black-knight"><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="16.5">桂</text></g>',
    '<g id="black-silver"><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="16.5">銀</text></g>',
    '<g id="black-gold"><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="16.5">金</text></g>',
    '<g id="black-bishop"><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="16.5">角</text></g>',
    '<g id="black-rook"><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="16.5">飛</text></g>',
    '<g id="black-king"><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="16.5">王</text></g>',
    '<g id="black-pro-pawn"><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="16.5">と</text></g>',
    '<g id="black-pro-lance" transform="scale(1.0, 0.5)"><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="18">成</text><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="34">香</text></g>',
    '<g id="black-pro-knight" transform="scale(1.0, 0.5)"><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="18">成</text><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="34">桂</text></g>',
    '<g id="black-pro-silver" transform="scale(1.0, 0.5)"><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="18">成</text><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="34">銀</text></g>',
    '<g id="black-horse"><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="16.5">馬</text></g>',
    '<g id="black-dragon"><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="16.5">龍</text></g>',
    '<g id="white-pawn" transform="rotate(180)"><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-3.5">歩</text></g>',
    '<g id="white-lance" transform="rotate(180)"><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-3.5">香</text></g>',
    '<g id="white-knight" transform="rotate(180)"><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-3.5">桂</text></g>',
    '<g id="white-silver" transform="rotate(180)"><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-3.5">銀</text></g>',
    '<g id="white-gold" transform="rotate(180)"><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-3.5">金</text></g>',
    '<g id="white-bishop" transform="rotate(180)"><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-3.5">角</text></g>',
    '<g id="white-rook" transform="rotate(180)"><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-3.5">飛</text></g>',
    '<g id="white-king" transform="rotate(180)"><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-3.5">王</text></g>',
    '<g id="white-pro-pawn" transform="rotate(180)"><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-3.5">と</text></g>',
    '<g id="white-pro-lance" transform="scale(1.0, 0.5) rotate(180)"><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-22">成</text><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-6">香</text></g>',
    '<g id="white-pro-knight" transform="scale(1.0, 0.5) rotate(180)"><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-22">成</text><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-6">桂</text></g>',
    '<g id="white-pro-silver" transform="scale(1.0, 0.5) rotate(180)"><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-22">成</text><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-6">銀</text></g>',
    '<g id="white-horse" transform="rotate(180)"><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-3.5">馬</text></g>',
    '<g id="white-dragon" transform="rotate(180)"><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-3.5">龍</text></g>',
]
SVG_PIECE_DEF_IDS = [
    None,
    "black-pawn", "black-lance", "black-knight", "black-silver",
    "black-bishop", "black-rook",
    "black-gold",
    "black-king",
    "black-pro-pawn", "black-pro-lance", "black-pro-knight", "black-pro-silver",
    "black-horse", "black-dragon", None, None,
    "white-pawn", "white-lance", "white-knight", "white-silver",
    "white-bishop", "white-rook",
    "white-gold",
    "white-king",
    "white-pro-pawn", "white-pro-lance", "white-pro-knight", "white-pro-silver",
    "white-horse", "white-dragon",
]
NUMBER_JAPANESE_NUMBER_SYMBOLS = [None, '１', '２', '３', '４', '５', '６', '７', '８', '９']
NUMBER_JAPANESE_KANJI_SYMBOLS = [None, "一", "二", "三", "四", "五", "六", "七", "八", "九", "十", "十一", "十二", "十三", "十四", "十五", "十六", "十七", "十八"]
SVG_SQUARES = '<g stroke="black"><rect x="20" y="10" width="181" height="181" fill="none" stroke-width="1.5" /><line x1="20.5" y1="30.5" x2="200.5" y2="30.5" stroke-width="1.0" /><line x1="20.5" y1="50.5" x2="200.5" y2="50.5" stroke-width="1.0" /><line x1="20.5" y1="70.5" x2="200.5" y2="70.5" stroke-width="1.0" /><line x1="20.5" y1="90.5" x2="200.5" y2="90.5" stroke-width="1.0" /><line x1="20.5" y1="110.5" x2="200.5" y2="110.5" stroke-width="1.0" /><line x1="20.5" y1="130.5" x2="200.5" y2="130.5" stroke-width="1.0" /><line x1="20.5" y1="150.5" x2="200.5" y2="150.5" stroke-width="1.0" /><line x1="20.5" y1="170.5" x2="200.5" y2="170.5" stroke-width="1.0" /><line x1="40.5" y1="10.5" x2="40.5" y2="190.5" stroke-width="1.0" /><line x1="60.5" y1="10.5" x2="60.5" y2="190.5" stroke-width="1.0" /><line x1="80.5" y1="10.5" x2="80.5" y2="190.5" stroke-width="1.0" /><line x1="100.5" y1="10.5" x2="100.5" y2="190.5" stroke-width="1.0" /><line x1="120.5" y1="10.5" x2="120.5" y2="190.5" stroke-width="1.0" /><line x1="140.5" y1="10.5" x2="140.5" y2="190.5" stroke-width="1.0" /><line x1="160.5" y1="10.5" x2="160.5" y2="190.5" stroke-width="1.0" /><line x1="180.5" y1="10.5" x2="180.5" y2="190.5" stroke-width="1.0" /></g>'
SVG_COORDINATES = '<g><text font-family="serif" text-anchor="middle" font-size="9" x="30.5" y="8">9</text><text font-family="serif" text-anchor="middle" font-size="9" x="50.5" y="8">8</text><text font-family="serif" text-anchor="middle" font-size="9" x="70.5" y="8">7</text><text font-family="serif" text-anchor="middle" font-size="9" x="90.5" y="8">6</text><text font-family="serif" text-anchor="middle" font-size="9" x="110.5" y="8">5</text><text font-family="serif" text-anchor="middle" font-size="9" x="130.5" y="8">4</text><text font-family="serif" text-anchor="middle" font-size="9" x="150.5" y="8">3</text><text font-family="serif" text-anchor="middle" font-size="9" x="170.5" y="8">2</text><text font-family="serif" text-anchor="middle" font-size="9" x="190.5" y="8">1</text><text font-family="serif" font-size="9" x="203.5" y="23">一</text><text font-family="serif" font-size="9" x="203.5" y="43">二</text><text font-family="serif" font-size="9" x="203.5" y="63">三</text><text font-family="serif" font-size="9" x="203.5" y="83">四</text><text font-family="serif" font-size="9" x="203.5" y="103">五</text><text font-family="serif" font-size="9" x="203.5" y="123">六</text><text font-family="serif" font-size="9" x="203.5" y="143">七</text><text font-family="serif" font-size="9" x="203.5" y="163">八</text><text font-family="serif" font-size="9" x="203.5" y="183">九</text></g>'
PIECE_SYMBOLS = [
    '',
    'p', 'l', 'n', 's', 'b', 'r', 'g', 'k', '+p', '+l', '+n', '+s', '+b', '+r'
]
PIECE_JAPANESE_SYMBOLS = [
    '',
    '歩', '香', '桂', '銀', '角', '飛', '金', '玉', 'と', '杏', '圭', '全', '馬', '龍'
]
HAND_PIECE_SYMBOLS = [
    'p', 'l', 'n', 's', 'g', 'b', 'r'
]
HAND_PIECE_JAPANESE_SYMBOLS = [
    "歩", "香", "桂", "銀",
    "金",
    "角", "飛"
]


class SvgWrapper(str):
    def _repr_svg_(self):
        return self


cdef extern from "init.hpp":
    void initTable()

initTable()


cdef extern from "position.hpp":
    cdef cppclass Position:
        @staticmethod
        void initZobrist()

Position.initZobrist()


cdef extern from "cshogi.h":
    void HuffmanCodedPos_init()
    void PackedSfen_init()
    void Book_init()

HuffmanCodedPos_init()
PackedSfen_init()
Book_init()


cdef extern from "cshogi.h":
    string __to_usi(const int move)
    string __to_csa(const int move)


def to_usi(int move):
    """Convert a move to the Universal Shogi Interface (USI) format.

    :param move: A move represented as an integer.
    :type move: int
    :return: The USI representation of the move.
    :rtype: bytes
    """
    return __to_usi(move)


def to_csa(int move):
    """Convert a move to the Computer Shogi Association (CSA) format.

    :param move: A move represented as an integer.
    :type move: int
    :return: The CSA representation of the move.
    :rtype: bytes
    """
    return __to_csa(move)


cdef extern from "cshogi.h":
    cdef cppclass __Board:
        __Board() except +
        __Board(const string& sfen) except +
        __Board(const __Board& board) except +
        void set(const string& sfen)
        void set_position(const string& position) except +
        void set_pieces(const int pieces[], const int pieces_in_hand[][7])
        void set_hcp(char* hcp) except +
        void set_psfen(char* psfen) except +
        void reset()
        string dump()
        void push(const int move)
        int pop()
        int peek()
        vector[int] get_history()
        bool is_game_over()
        int isDraw(const int checkMaxPly)
        int moveIsDraw(const int move, const int checkMaxPly)
        int move(const int from_square, const int to_square, const bool promotion)
        int drop_move(const int to_square, const int drop_piece_type)
        int move_from_usi(const string& usi)
        int move_from_csa(const string& csa)
        int move_from_move16(const unsigned short move16)
        int move_from_psv(const unsigned short move16)
        int turn()
        void setTurn(const int turn)
        int ply()
        void setPly(const int ply)
        string toSFEN()
        string toCSAPos()
        void toHuffmanCodedPos(char* data)
        void toPackedSfen(char* data)
        int piece(const int sq)
        int kingSquare(const int c)
        bool inCheck()
        int mateMoveIn1Ply()
        int mateMove(int ply)
        bool is_mate(int ply)
        unsigned long long getKey()
        bool moveIsPseudoLegal(const int move)
        bool pseudoLegalMoveIsLegal(const int move)
        bool moveIsLegal(const int move)
        vector[int] pieces_in_hand(const int color)
        vector[int] pieces()
        bool is_nyugyoku()
        void piece_planes(char* mem)
        void piece_planes_rotate(char* mem)
        void _dlshogi_make_input_features(char* mem1, char* mem2)
        void push_pass()
        void pop_pass()
        bool isOK()
        unsigned long long bookKey()
        unsigned long long bookKeyAfter(const unsigned long long key, const int move)

    int __piece_to_piece_type(const int p)
    int __hand_piece_to_piece_type(const int hp)
    int __make_file(const int sq)
    int __make_rank(const int sq)


cdef class Board:
    """Board class to represent a Shogi board.

    :param sfen: A string representing the board in SFEN format, optional.
    :type sfen: str, optional
    :param board: A string representing the board in SFEN format, optional.
    :type board: Board, optional

    :Example:

    .. code-block:: python

        # Initialize board
        board = Board()

        # Initialize board with sfen
        board = Board(sfen="lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1")

        # Initialize board with board
        board2 = Board(board=board)
    """

    cdef __Board __board

    def __cinit__(self, str sfen=None, Board board=None):
        """Initializes the board.

        :param sfen: A string representing the board in SFEN format, optional.
        :type sfen: str, optional
        :param board: A string representing the board in SFEN format, optional.
        :type board: Board, optional
        :raises RuntimeError: If the sfen string is incorrect.
        """
        cdef string sfen_b
        if sfen:
            sfen_b = sfen.encode('ascii')
            self.__board = __Board(sfen_b)
        elif board is not None:
            self.__board = __Board(board.__board)
        else:
            self.__board = __Board()

    def __copy__(self):
        return Board(board=self)

    def copy(self):
        """Creates a copy of the current board.

        :return: A new Board object with the same state.
        """
        return Board(board=self)

    def set_sfen(self, str sfen):
        """Sets the board state using a given SFEN string.

        :param sfen: String representing the board state in SFEN.
        :type sfen: str
        :raises RuntimeError: If the sfen string is incorrect.

        :Example:

        .. code-block:: python

            board.set_sfen("lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1")
        """
        cdef string sfen_b = sfen.encode('ascii')
        self.__board.set(sfen_b)

    def set_position(self, str position):
        """Sets the position on the board using a given string.

        :param position: String representing the position to be set.
        :type position: str
        :raises RuntimeError: If the position string is incorrect.

        :Example:

        .. code-block:: python

            board.set_position("startpos moves 2g2f")

            board.set_position("sfen lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1")
        """
        cdef string position_b = position.encode('ascii')
        self.__board.set_position(position_b)

    def set_pieces(self, list pieces, tuple pieces_in_hand):
        """Sets the pieces on the board and pieces in hand.

        :param pieces: A list of pieces on the board.
        :type pieces: list
        :param pieces_in_hand: A tuple representing pieces in hand.
        :type pieces_in_hand: tuple

        :Example:

        .. code-block:: python

            # Edit the pieces on the board and the pieces in hand
            board = Board()
            pieces_src = board.pieces
            pieces_in_hand_src = board.pieces_in_hand

            pieces_dst = pieces_src.copy()
            pieces_dst[G1] = NONE
            pieces_dst[F1] = BPAWN
            pieces_dst[C1] = NONE

            pieces_in_hand_dst = (pieces_in_hand_src[0].copy(), pieces_in_hand_src[1].copy())
            pieces_in_hand_dst[BLACK][HPAWN] = 1

            board_dst = board.copy()
            board_dst.set_pieces(pieces_dst, pieces_in_hand_dst)
        """
        cdef int __pieces[81]
        cdef int __pieces_in_hand[2][7]
        cdef int sq, c, hp
        for sq in range(81):
            __pieces[sq] = pieces[sq]
        for c in range(2):
            for hp in range(7):
                __pieces_in_hand[c][hp] = pieces_in_hand[c][hp]
        self.__board.set_pieces(__pieces, __pieces_in_hand)

    def set_hcp(self, np.ndarray hcp):
        """Sets the board using HuffmanCodedPos (hcp) format, a compression format used in Apery.

        :param hcp: The HuffmanCodedPos data to set the board.
        :type hcp: np.ndarray
        :raises RuntimeError: If the hcp is incorrect.

        :Example:

        .. code-block:: python

            hcp = np.fromfile("hcpfile", HuffmanCodedPos)
            board.set_hcp(np.asarray(hcp[0]))

        """
        self.__board.set_hcp(hcp.data)

    def set_psfen(self, np.ndarray psfen):
        """Sets the board using PackedSfen (psfen) format, a compression format used in YaneuraOu.

        :param psfen: The PackedSfen data to set the board.
        :type psfen: np.ndarray
        :raises RuntimeError: If the psfen is incorrect.

        :Example:

        .. code-block:: python

            psfen = np.fromfile("psfenfile", PackedSfen)
            board.set_psfen(np.asarray(psfen[0]))

        """
        self.__board.set_psfen(psfen.data)

    def reset(self):
        """Resets the board to its initial state."""
        self.__board.reset()

    def __repr__(self):
        return self.__board.dump().decode('ascii')

    def push(self, int move):
        """Push a move to the board.

        The move is represented as an integer, where each bit has the following meaning:

        - xxxxxxxx xxxxxxxx xxxxxxxx x1111111: Destination square.
        - xxxxxxxx xxxxxxxx xx111111 1xxxxxxx: Source square, or PieceType + SquareNum - 1 when dropping a piece.
        - xxxxxxxx xxxxxxxx x1xxxxxx xxxxxxxx: Promotion flag (1 if promoted).
        - xxxxxxxx xxxx1111 xxxxxxxx xxxxxxxx: Moving piece's PieceType, not used when dropping a piece.
        - xxxxxxxx 1111xxxx xxxxxxxx xxxxxxxx: Captured piece's PieceType.

        :param move: The move to be applied to the board, encoded as an integer.
        :type move: int
        """
        self.__board.push(move)

    def push_usi(self, str usi):
        """Push a move to the board in Universal Shogi Interface (USI) format.

        :param usi: The move in USI format.
        :type usi: str
        :return: The move as an integer, or 0 if the move is invalid.
        :rtype: int
        """
        cdef string usi_b = usi.encode('ascii')
        cdef int move = self.__board.move_from_usi(usi_b)
        if move != 0:
            self.__board.push(move)
        return move

    def push_csa(self, str csa):
        """Push a move to the board in Computer Shogi Association (CSA) format.

        :param str csa: The move in CSA format.
        :type csa: str
        :return: The move as an integer, or 0 if the move is invalid.
        :rtype: int
        """
        cdef string csa_b = csa.encode('ascii')
        cdef int move = self.__board.move_from_csa(csa_b)
        if move != 0:
            self.__board.push(move)
        return move

    def push_move16(self, unsigned short move16):
        """Push a 16-bit move to the board.

        The `move16` is the lower 16 bits of the ingeter move format.

        :param move16: The move in 16-bit format.
        :type move16: unsigned short
        :return: The move as an integer.
        :rtype: int
        """
        cdef int move = self.__board.move_from_move16(move16)
        self.__board.push(move)
        return move

    def push_psv(self, unsigned short move16):
        """Push a move to the board in PSV format, which is used in YaneuraOu.

        :param move16: The move in PSV format.
        :type move16: unsigned short
        :return: The move as an integer.
        :rtype: int
        """
        cdef int move = self.__board.move_from_psv(move16)
        self.__board.push(move)
        return move

    def push_pass(self):
        """Push a pass move to the board.

        :return: The result of the pass move.
        :rtype: int
        """
        assert not self.is_check()
        return self.__board.push_pass()

    def pop_pass(self):
        """Pop the last pass move from the board."""
        self.__board.pop_pass()

    def pop(self):
        """Pop the last move from the board.

        :return: The move as an integer.
        :rtype: int
        """
        return self.__board.pop()

    def peek(self):
        """Peek at the last move on the board.

        :return: The last move as an integer.
        :rtype: int
        """
        return self.__board.peek()

    @property
    def history(self):
        """Gets the history of moves in the game.

        :return: The list of moves.
        :rtype: list
        """
        return self.__board.get_history()

    def is_game_over(self):
        """Check if the game is over.

        :return: True if the game is over, otherwise False.
        """
        return self.__board.is_game_over()

    def is_draw(self, ply=None):
        """Determines whether the current game state represents a draw or another special condition.

        :param ply: Optional parameter to check up to a specific ply. Defaults to maximum int value.
        :type ply: int or None
        :return: A status that could be one of the following:

            - REPETITION_DRAW: In case of a repeated position.
            - REPETITION_WIN: In case of a win due to consecutive checks.
            - REPETITION_LOSE: In case of a loss due to consecutive checks.
            - REPETITION_SUPERIOR: In case of a superior position.
            - REPETITION_INFERIOR: In case of an inferior position.
            - NOT_REPETITION: If none of the above conditions apply.

        :rtype: int
        """
        cdef int _ply
        if ply:
            _ply = ply
        else:
            _ply = 2147483647
        return self.__board.isDraw(_ply)

    def move_is_draw(self, int move, ply=None):
        """Check if a given move results in a draw.

        :param move: The move to check.
        :type move: int
        :param ply: Optional parameter to check up to a specific ply. Defaults to maximum int value.
        :type ply: int or None
        :return: A status that could be one of the following:

            - REPETITION_DRAW: In case of a repeated position.
            - REPETITION_WIN: In case of a win due to consecutive checks.
            - REPETITION_LOSE: In case of a loss due to consecutive checks.
            - REPETITION_SUPERIOR: In case of a superior position.
            - REPETITION_INFERIOR: In case of an inferior position.
            - NOT_REPETITION: If none of the above conditions apply.

        :rtype: int
        """
        cdef int _ply
        if ply:
            _ply = ply
        else:
            _ply = 2147483647
        return self.__board.moveIsDraw(move, _ply)

    def move(self, int from_square, int to_square, bool promotion):
        """Make a move on the board.

        :param from_square: The starting square index.
        :type from_square: int
        :param to_square: The destination square index.
        :type to_square: int
        :param promotion: Whether the move involves a promotion.
        :type promotion: bool
        :return: An integer representing the move.
        :rtype: int
        """
        return self.__board.move(from_square, to_square, promotion)

    def drop_move(self, int to_square, int drop_piece_type):
        """Make a drop move on the board.

        :param to_square: The destination square index.
        :type to_square: int
        :param drop_piece_type: The type of piece to drop.
        :type drop_piece_type: int
        :return: An integer representing the move.
        :rtype: int
        """
        return self.__board.drop_move(to_square, drop_piece_type)

    def move_from_usi(self, str usi):
        """Make a move on the board using a USI-formatted string.

        :param usi: The move in USI format.
        :type usi: str
        :return: An integer representing the move.
        :rtype: int
        """
        cdef string usi_b = usi.encode('ascii')
        return self.__board.move_from_usi(usi_b)

    def move_from_csa(self, str csa):
        """Make a move on the board using a CSA-formatted string.

        :param csa: The move in CSA format.
        :type csa: str
        :return: An integer representing the move.
        :rtype: int
        """
        cdef string csa_b = csa.encode('ascii')
        return self.__board.move_from_csa(csa_b)

    def move_from_move16(self, unsigned short move16):
        """Make a move on the board using a 16-bit move code.

        :param move16: The move in 16-bit format.
        :type move16: unsigned short
        :return: An integer representing the move.
        :rtype: int
        """
        return self.__board.move_from_move16(move16)

    def move_from_psv(self, unsigned short move16):
        """Make a move on the board using a PSV-formatted move code.

        :param move16: The move in PSV format.
        :type move16: unsigned short
        :return: An integer representing the move.
        :rtype: int
        """
        return self.__board.move_from_psv(move16)

    @property
    def legal_moves(self):
        """Generates a list of legal moves from the current board position.

        :return: An iterator that yields legal moves.
        :rtype: LegalMoveList
        """
        return LegalMoveList(self)

    @property
    def pseudo_legal_moves(self):
        """Generates a list of pseudo-legal moves from the current board position.

        :return: An iterator that yields pseudo-legal moves.
        :rtype: PseudoLegalMoveList
        """
        return PseudoLegalMoveList(self)

    @property
    def turn(self):
        """Gets the current turn.

        :return: The current turn, either BLACK (for the first player) or WHITE (for the second player).
        """
        return self.__board.turn()

    @turn.setter
    def turn(self, int turn):
        """Sets the current turn.

        :param turn: The turn to be set, either BLACK or WHITE.
        :type turn: int
        """
        assert turn == BLACK or turn == WHITE
        self.__board.setTurn(turn)

    @property
    def move_number(self):
        """Gets the current move number.

        :return: Current move number.
        """
        return self.__board.ply()

    @move_number.setter
    def move_number(self, int ply):
        """Sets the current move number.

        :param ply: The move number to set.
        :type ply: int
        """
        assert ply > 0
        self.__board.setPly(ply)

    def sfen(self):
        """Returns the current board position in Shogi Forsyth-Edwards Notation (SFEN) format.

        :return: A string representing the board in SFEN format.
        :rtype: str
        """
        return self.__board.toSFEN().decode('ascii')

    def csa_pos(self):
        """Returns the current board position in Computer Shogi Association (CSA) format.

        :return: A string representing the board in CSA format.
        :rtype: str
        """
        return self.__board.toCSAPos().decode('ascii')

    def to_hcp(self, np.ndarray hcp):
        """Converts the current board to HuffmanCodedPos (hcp) format.

        :param hcp: An array to store the hcp data.
        :type hcp: np.ndarray
        """
        return self.__board.toHuffmanCodedPos(hcp.data)

    def to_psfen(self, np.ndarray psfen):
        """Converts the current board to PackedSfen (psfen) format.

        :param psfen: An array to store the psfen data.
        :type psfen: np.ndarray
        """
        return self.__board.toPackedSfen(psfen.data)

    def piece(self, int sq):
        """Returns the piece at a given square.

        :param sq: The square index.
        :type sq: int
        :return: The piece at the given square.
        :rtype: int
        """
        return self.__board.piece(sq)

    def piece_type(self, int sq):
        """Returns the type of piece at a given square.

        :param sq: The square index.
        :type sq: int
        :return: The type of piece at the given square.
        :rtype: int
        """
        return __piece_to_piece_type(self.__board.piece(sq))

    def king_square(self, int c):
        """Returns the square index of the king for a given color.

        :param c: The color of the king, either BLACK or WHITE.
        :type c: int
        :return: The square index of the king for the specified color.
        :rtype: int
        """
        return self.__board.kingSquare(c)

    def is_check(self):
        """Determines if the king is in check.

        :return: True if the king is in check, False otherwise.
        :rtype: bool
        """
        return self.__board.inCheck()

    def mate_move_in_1ply(self):
        """Finds a mating move in one ply.

        :return: integer representing the mating move.
        :rtype: int
        """
        return self.__board.mateMoveIn1Ply()

    def mate_move(self, int ply):
        """Finds a mating move in a given number of ply.

        :param ply: odd integer, should be greater or equal to 3, representing the number of ply.
        :type ply: int
        :return: integer representing the mating move.
        :rtype: int
        """
        assert ply % 2 == 1
        assert ply >= 3
        return self.__board.mateMove(ply)

    def is_mate(self, int ply):
        """Checks if a mate condition is met in a given number of ply.

        :param ply: even integer representing the number of ply.
        :type ply: int
        :return: True if mate, False otherwise.
        :rtype: bool
        """
        assert ply % 2 == 0
        return self.__board.inCheck() and self.__board.is_mate(ply)

    def zobrist_hash(self):
        """Calculates the Zobrist hash key for the current board position.

        :return: 64-bit integer representing the Zobrist hash key.
        :rtype: long long
        """
        return self.__board.getKey()

    def is_pseudo_legal(self, int move):
        """Checks if a move is pseudo-legal.

        :param move: integer representing the move.
        :type move: int
        :return: True if the move is pseudo-legal, False otherwise.
        :rtype: bool
        """
        return self.__board.moveIsPseudoLegal(move)

    def pseudo_legal_move_is_legal(self, int move):
        """Checks if a pseudo-legal move is legal.

        :param move: integer representing the move.
        :type move: int
        :return: True if the move is legal, False otherwise.
        :rtype: bool
        """
        return self.__board.pseudoLegalMoveIsLegal(move)

    def is_legal(self, int move):
        """Checks if a move is legal.

        :param move: integer representing the move.
        :type move: int
        :return: True if the move is legal, False otherwise.
        :rtype: bool
        """
        return self.__board.moveIsLegal(move)

    @property
    def pieces_in_hand(self):
        """Gets the pieces in hand for both BLACK and WHITE players.

        :return: A tuple containing the number of pieces in hand for both BLACK and WHITE players.
        :rtype: tuple
        """
        return (self.__board.pieces_in_hand(BLACK), self.__board.pieces_in_hand(WHITE))

    @property
    def pieces(self):
        """Gets the pieces on the board.

        :return: An array representing the pieces on the board.
        :rtype: list
        """
        return self.__board.pieces()

    def is_nyugyoku(self):
        """Check for a win according to the Nyūgyoku declaration rule (27-point rule).

        :return: True if the game is in a state of Nyūgyoku declaration (a win by entering king according to the 27-point rule), False otherwise.
        :rtype: bool
        """
        return self.__board.is_nyugyoku()

    def piece_planes(self, np.ndarray features):
        """Generate piece planes representing the current state of the board. The result is stored in the given ndarray.

        :param features: An ndarray with dimensions (FEATURES_NUM, 9, 9) and dtype np.float32, where FEATURES_NUM is defined as len(PIECE_TYPES) * 2 + sum(MAX_PIECES_IN_HAND) * 2.
        :type features: np.ndarray
        """
        self.__board.piece_planes(features.data)

    def piece_planes_rotate(self, np.ndarray features):
        """Generate 180-degree rotated piece planes representing the current state of the board. The result is stored in the given ndarray.

        :param features: An ndarray with dimensions (FEATURES_NUM, 9, 9) and dtype np.float32, where FEATURES_NUM is defined as len(PIECE_TYPES) * 2 + sum(MAX_PIECES_IN_HAND) * 2.
        :type features: np.ndarray
        """
        self.__board.piece_planes_rotate(features.data)

    def _dlshogi_make_input_features(self, np.ndarray features1, np.ndarray features2):
        self.__board._dlshogi_make_input_features(features1.data, features2.data)

    def is_ok(self):
        """Check if the board is in a valid state.

        :return: True if the board state is valid, False otherwise.
        :rtype: bool
        """
        return self.__board.isOK()

    def book_key(self):
        """Gets the key for the opening book.

        :return: The key for the current board state in the opening book.
        :rtype: long long
        """
        return self.__board.bookKey()

    def book_key_after(self, unsigned long long key, int move):
        """Gets the key for the opening book after a specific move.

        :param key: The current key.
        :type key: long long
        :param move: The move to be applied.
        :type move: int
        :return: The key for the resulting board state in the opening book.
        :rtype: long long
        """
        return self.__board.bookKeyAfter(key, move)

    def to_svg(self, lastmove=None, scale=1.0):
        """Generate an SVG representation of the current board state.

        :param lastmove: The last move made on the board, if any (default is None).
        :type lastmove: int
        :param scale: The scaling factor for the SVG (default is 1.0).
        :type scale: float
        :return: An SVG representation of the current board state.
        :rtype: SvgWrapper
        """
        import xml.etree.ElementTree as ET

        width = 230
        height = 192

        svg = ET.Element("svg", {
            "xmlns": "http://www.w3.org/2000/svg",
            "version": "1.1",
            "xmlns:xlink": "http://www.w3.org/1999/xlink",
            "width": str(width * scale),
            "height": str(height * scale),
            "viewBox": "0 0 {} {}".format(width, height),
        })

        defs = ET.SubElement(svg, "defs")
        for piece_def in SVG_PIECE_DEFS:
            defs.append(ET.fromstring(piece_def))

        if lastmove is not None:
            i, j = divmod(move_to(lastmove), 9)
            ET.SubElement(svg, "rect", {
                "x": str(20.5 + (8 - i) * 20),
                "y": str(10.5 + j * 20),
                "width": str(20),
                "height": str(20),
                "fill": "#f6b94d"
            })
            if not move_is_drop(lastmove):
                i, j = divmod(move_from(lastmove), 9)
                ET.SubElement(svg, "rect", {
                    "x": str(20.5 + (8 - i) * 20),
                    "y": str(10.5 + j * 20),
                    "width": str(20),
                    "height": str(20),
                    "fill": "#fdf0e3"
                })

        svg.append(ET.fromstring(SVG_SQUARES))
        svg.append(ET.fromstring(SVG_COORDINATES))

        for sq in SQUARES:
            pc = self.__board.piece(sq)
            if pc != NONE:
                i, j = divmod(sq, 9)
                x = 20.5 + (8 - i) * 20
                y = 10.5 + j * 20

                ET.SubElement(svg, "use", {
                    "xlink:href": "#{}".format(SVG_PIECE_DEF_IDS[pc]),
                    "x": str(x),
                    "y": str(y),
                })

        hand_pieces = [[], []]
        for c in COLORS:
            i = 0
            for hp, n in zip(HAND_PIECES, self.__board.pieces_in_hand(c)):
                if n >= 11:
                    hand_pieces[c].append((i, NUMBER_JAPANESE_KANJI_SYMBOLS[n % 10]))
                    i += 1
                    hand_pieces[c].append((i, NUMBER_JAPANESE_KANJI_SYMBOLS[10]))
                    i += 1
                elif n >= 2:
                    hand_pieces[c].append((i, NUMBER_JAPANESE_KANJI_SYMBOLS[n]))
                    i += 1
                if n >= 1:
                    hand_pieces[c].append((i, HAND_PIECE_JAPANESE_SYMBOLS[hp]))
                    i += 1
            i += 1
            hand_pieces[c].append((i, "手"))
            i += 1
            hand_pieces[c].append((i, "先" if c == BLACK else "後"))
            i += 1
            hand_pieces[c].append((i, "☗" if c == BLACK else "☖"))

        for c in COLORS:
            if c == BLACK:
                x = 214
                y = 190
            else:
                x = -16
                y = -10
            scale = 1
            if len(hand_pieces[c]) + 1 > 13:
                scale = 13.0 / (len(hand_pieces[c]) + 1)
            for i, text in hand_pieces[c]:
                e = ET.SubElement(svg, "text", {
                    "font-family": "serif",
                    "font-size": str(14 * scale),
                })
                e.set("x", str(x))
                e.set("y", str(y - 14 * scale * i))
                if c == WHITE:
                    e.set("transform", "rotate(180)")
                e.text = text

        return SvgWrapper(ET.tostring(svg).decode("utf-8"))

    def _repr_svg_(self):
        cdef int move = self.__board.peek()
        if move == 0:
            return self.to_svg()
        else:
            return self.to_svg(move)

    def to_bod(self):
        """Convert the current state of the board to a Board Diagram (BOD) format.

        :return: A string representing the board in BOD format.
        :rtype: str
        """
        import cshogi.KIF
        history = self.history
        if len(history) > 0:
            move = self.pop()
            move_str = '\n手数＝' + str(self.move_number) + '　' + cshogi.KIF.move_to_bod(move, self) + '　まで'
            self.push(move)
            return cshogi.KIF.board_to_bod(self) + move_str
        else:
            return cshogi.KIF.board_to_bod(self)


def piece_to_piece_type(int p):
    """Convert a piece identifier to its corresponding piece type.

    :param p: The piece identifier, typically an integer value representing a specific piece.
    :type p: int
    :return: The corresponding piece type for the given piece identifier.
    :rtype: int
    """
    return __piece_to_piece_type(p)


def hand_piece_to_piece_type(int hp):
    """Convert a hand piece identifier to its corresponding piece type.

    :param hp: The hand piece identifier, typically an integer value representing a specific hand piece.
    :type hp: int
    :return: The corresponding piece type for the given hand piece identifier.
    :rtype: int
    """
    return __hand_piece_to_piece_type(hp)


def make_file(int sq):
    """Determine the file (column) of a square.

    :param sq: The square index.
    :type sq: int
    :return: The file (column) of the given square.
    :rtype: int
    """
    return __make_file(sq)


def make_rank(int sq):
    """Determine the rank (row) of a square.

    :param sq: The square index.
    :type sq: int
    :return: The rank (row) of the given square.
    :rtype: int
    """
    return __make_rank(sq)


cdef extern from "cshogi.h":
    cdef cppclass __LegalMoveList:
        __LegalMoveList() except +
        __LegalMoveList(const __Board& board) except +
        bool end()
        int move()
        void next()
        int size()
    cdef cppclass __PseudoLegalMoveList:
        __PseudoLegalMoveList() except +
        __PseudoLegalMoveList(const __Board& board) except +
        bool end()
        int move()
        void next()
        int size()

    int __move_to(const int move)
    int __move_from(const int move)
    int __move_cap(const int move)
    bool __move_is_promotion(const int move)
    bool __move_is_drop(const int move)
    int __move_from_piece_type(const int move)
    int __move_drop_hand_piece(const int move)
    unsigned short __move16(const int move)
    unsigned short __move16_from_psv(const unsigned short move16)
    unsigned short __move16_to_psv(const unsigned short move16)
    int __move_rotate(const int move)
    string __move_to_usi(const int move)
    string __move_to_csa(const int move)
    int __dlshogi_get_features1_num()
    int __dlshogi_get_features2_num()
    int __dlshogi_make_move_label(const int move, const int color)
    void __dlshogi_use_nyugyoku_features(bool use)


cdef class LegalMoveList:
    """An iterator class to generate legal moves."""

    cdef __LegalMoveList __ml

    def __cinit__(self, Board board):
        self.__ml = __LegalMoveList(board.__board)

    def __iter__(self):
        return self

    def __next__(self):
        if self.__ml.end():
            raise StopIteration()
        move = self.__ml.move()
        self.__ml.next()
        return move

    def __len__(self):
        return self.__ml.size()


cdef class PseudoLegalMoveList:
    """An iterator class to generate pseudo-legal moves."""

    cdef __PseudoLegalMoveList __ml

    def __cinit__(self, Board board):
        self.__ml = __PseudoLegalMoveList(board.__board)

    def __iter__(self):
        return self

    def __next__(self):
        if self.__ml.end():
            raise StopIteration()
        move = self.__ml.move()
        self.__ml.next()
        return move

    def __len__(self):
        return self.__ml.size()


def move_to(int move):
    """Extract the destination square of a move.

    :param move: A move represented as an integer.
    :type move: int
    :return: The destination square of the move.
    :rtype: int
    """

    return __move_to(move)


def move_from(int move):
    """Extract the source square of a move.

    :param move: A move represented as an integer.
    :type move: int
    :return: The source square of the move.
    :rtype: int
    """
    return __move_from(move)


def move_cap(int move):
    """Extract the captured piece of a move.

    :param move: A move represented as an integer.
    :type move: int
    :return: The captured piece of the move.
    :rtype: int
    """
    return __move_cap(move)


def move_is_promotion(int move):
    """Checks if a move is a promotion move.

    :param move: A move represented as an integer.
    :type move: int
    :return: True if the move is a promotion move, False otherwise.
    :rtype: bool
    """
    return __move_is_promotion(move)


def move_is_drop(int move):
    """Checks if a move is a drop move.

    :param move: A move represented as an integer.
    :type move: int
    :return: True if the move is a drop move, False otherwise.
    :rtype: bool
    """
    return __move_is_drop(move)


def move_from_piece_type(int move):
    """Extract the piece type moved from a move.

    :param move: A move represented as an integer.
    :type move: int
    :return: The piece type moved from the move.
    :rtype: int
    """
    return __move_from_piece_type(move)


def move_drop_hand_piece(int move):
    """
    Extract the hand piece of a drop move.

    :param move: A drop move represented as an integer.
    :type move: int
    :return: The hand piece of the move.
    :rtype: int
    """
    return __move_drop_hand_piece(move)


def move16(int move):
    """Convert a move to a 16-bit representation.

    :param move: A move represented as an integer.
    :type move: int
    :return: The 16-bit representation of the move.
    :rtype: int
    """
    return __move16(move)


def move16_from_psv(unsigned short move16):
    """Convert a 16-bit move representation from a move in PSV format.

    :param move16: A 16-bit move representation in PSV format.
    :type move16: unsigned short
    :return: The corresponding 16-bit move representation.
    :rtype: unsigned short
    """
    return __move16_from_psv(move16)


def move16_to_psv(unsigned short move16):
    """Convert a 16-bit move representation to a move in PSV format.

    :param move16: A 16-bit move representation.
    :type move16: unsigned short
    :return: The corresponding 16-bit move representation in PSV format.
    :rtype: unsigned short
    """
    return __move16_to_psv(move16)


def move_rotate(int move):
    """Convert a move to a move rotated by 180 degrees.

    :param move: A move represented as an integer.
    :type move: int
    :return: The move rotated by 180 degrees.
    :rtype: int
    """
    return __move_rotate(move)


def move_to_usi(int move):
    """Convert a move to the Universal Shogi Interface (USI) format.

    :param move: A move represented as an integer.
    :type move: int
    :return: The USI representation of the move.
    :rtype: str
    """
    return __move_to_usi(move).decode('ascii')


def move_to_csa(int move):
    """Convert a move to the Computer Shogi Association (CSA) format.

    :param move: A move represented as an integer.
    :type move: int
    :return: The CSA representation of the move.
    :rtype: str
    """
    return __move_to_csa(move).decode('ascii')


def opponent(int color):
    """Gets the opponent's color.

    :param color: The player's color, either BLACK or WHITE.
    :type color: int
    :return: The opponent's color, either BLACK or WHITE.
    :rtype: int
    """
    return BLACK if color == WHITE else WHITE


_dlshogi_FEATURES1_NUM = __dlshogi_get_features1_num()
_dlshogi_FEATURES2_NUM = __dlshogi_get_features2_num()


def _dlshogi_make_move_label(int move, int color):
    """Make a move label for the given move and color in the context of the dlshogi model.

    :param move: An integer representing the move.
    :param color: An integer representing the color.
    :return: An integer label generated for the move and color.
    """
    return __dlshogi_make_move_label(move, color)


def _dlshogi_use_nyugyoku_features(bool use):
    """Set whether to use the Nyūgyoku features in the dlshogi model.

    :param use: A boolean indicating whether to use the Nyūgyoku features.
    """
    __dlshogi_use_nyugyoku_features(use)
    global _dlshogi_FEATURES2_NUM
    _dlshogi_FEATURES2_NUM = __dlshogi_get_features2_num()


cdef extern from "parser.h" namespace "parser":
    cdef cppclass __Parser:
        __Parser() except +
        string version
        vector[string] informations
        string sfen
        string endgame
        vector[string] names
        vector[float] ratings
        vector[int] moves
        vector[int] times
        vector[int] scores
        vector[string] comments
        string comment
        int win
        void parse_csa_file(const string& path) except +
        void parse_csa_str(const string& csa_str) except +


cdef class Parser:
    """A class to parse Shogi game records in the CSA standard file format."""

    @staticmethod
    def parse_file(file, encoding=None):
        """Parse CSA standard Shogi game records from a file.

        :param file: The file path or file object containing the game records.
        :type file: str or file object
        :param encoding: The character encoding of the file, defaults to None.
        :type encoding: str, optional
        :return: A list of Parser objects representing the parsed game records.
        :rtype: list
        """
        if type(file) is str:
            with open(file, 'r', encoding=encoding) as f:
                return Parser.parse_str(f.read())
        else:
            return Parser.parse_str(file.read())

    @staticmethod
    def parse_str(csa_str):
        """Parse CSA standard Shogi game records from a string.

        :param csa_str: A string containing one or more Shogi game records.
        :type csa_str: str
        :return: A list of Parser objects representing the parsed game records.
        :rtype: list
        """
        parsers = []
        # split multiple matches
        matches = csa_str.split('\n/\n')
        for one_csa_str in matches:
            parser = Parser()
            parser.parse_csa_str(one_csa_str)
            parsers.append(parser)
        return parsers

    cdef __Parser __parser

    def __cinit__(self):
        self.__parser = __Parser()

    def parse_csa_file(self, str path):
        """Parse a CSA standard Shogi game record from a file path.

        :param path: The file path containing the game record.
        :type path: str
        """
        cdef string path_b = path.encode(locale.getpreferredencoding())
        self.__parser.parse_csa_file(path_b)

    def parse_csa_str(self, str csa_str):
        """Parse a CSA standard Shogi game record from a string.

        :param csa_str: A string containing the game record.
        :type csa_str: str
        """
        cdef string csa_str_b = csa_str.encode('utf-8')
        self.__parser.parse_csa_str(csa_str_b)

    @property
    def version(self):
        """Gets the version of the CSA standard game record file format.

        :return: The version of the CSA standard game record file format.
        :rtype: str
        """
        return self.__parser.version.decode('ascii')

    @property
    def var_info(self):
        """Gets a dictionary containing various information about the game.

        :return: A dictionary containing various information about the game.
        :rtype: dict
        """
        d = {}
        for information in self.__parser.informations:
            k, v = information.decode('utf-8').split(':', 1)
            d[k[1:]] = v
        return d

    @property
    def sfen(self):
        """Gets the SFEN representation of the initial position.

        :return: The SFEN representation of the initial position.
        :rtype: str
        """
        return self.__parser.sfen.decode('ascii')

    @property
    def endgame(self):
        """Gets the endgame result of the game.

        :return: The endgame result of the game.
        :rtype: str
        """
        return self.__parser.endgame.decode('ascii')

    @property
    def names(self):
        """Gets the names of the players.

        :return: The names of the players.
        :rtype: list
        """
        return [name.decode('utf-8') for name in self.__parser.names]

    @property
    def ratings(self):
        """Gets the ratings of the players.

        :return: The ratings of the players.
        :rtype: list
        """
        return self.__parser.ratings

    @property
    def moves(self):
        """Gets the list of moves in the game.

        :return: The list of moves in the game.
        :rtype: list
        """
        return self.__parser.moves

    @property
    def times(self):
        """Gets the list of times for each move in the game.

        :return: The list of times for each move in the game.
        :rtype: list
        """
        return self.__parser.times

    @property
    def scores(self):
        """Gets the list of scores for each move of the game.

        :return: The list of scores for each move of the game.
        :rtype: list
        """
        return self.__parser.scores

    @property
    def comments(self):
        """Gets the list of comments for each move of the game.

        :return: The list of comments for each move of the game.
        :rtype: list
        """
        return [comment.decode('ascii') for comment in self.__parser.comments]

    @property
    def win(self):
        """Gets the result of the game.

        :return: The result of the game, which can be one of BLACK_WIN, WHITE_WIN, or DRAW.
        :rtype: one of (BLACK_WIN, WHITE_WIN, DRAW)
        """
        return self.__parser.win

    @property
    def comment(self):
        """Gets the general comment about the game.

        :return: The general comment about the game.
        :rtype: str
        """
        return self.__parser.comment.decode('utf-8')


cdef extern from "dfpn.h":
    cdef cppclass __DfPn:
        __DfPn() except +
        __DfPn(const int max_depth, const unsigned int max_search_node, const int draw_ply) except +
        bool search(__Board& board)
        bool search_andnode(__Board& board)
        void stop(const bool stop)
        int get_move(__Board& board)
        void get_pv(__Board& board)
        vector[unsigned int] pv
        void set_draw_ply(const int draw_ply)
        void set_maxdepth(const int depth)
        void set_max_search_node(int max_search_node)
        unsigned int get_searched_node()


cdef class DfPn:
    """Class to perform mate search using the df-pn algorithm.

    :param int depth: Depth of the search.
    :type depth: int, optional
    :param nodes: Number of nodes in the search.
    :type nodes: int, optional
    :param draw_ply: The number of plies to consider as a draw.
    :type draw_ply: int, optional
    """

    cdef __DfPn __dfpn

    def __cinit__(self, depth=31, nodes=1048576, draw_ply=32767):
        self.__dfpn = __DfPn(depth, nodes, draw_ply)

    def search(self, Board board):
        """Perform a checkmate search on the given board.

        :param board: Board state.
        :type board: Board
        :return: True if checkmate is found, False otherwise.
        :rtype: bool
        """
        return self.__dfpn.search(board.__board)

    def search_andnode(self, Board board):
        """Perform a checkmate search at the AND node.

        :param board: Board state.
        :type board: Board
        :return: True if checkmate is found, False otherwise.
        :rtype: bool
        """
        return self.__dfpn.search_andnode(board.__board)

    def stop(self, bool stop):
        """Stop the search.

        :param stop: Flag to stop the search.
        :type stop: bool
        """
        self.__dfpn.stop(stop)

    def get_move(self, Board board):
        """Gets the mating move found by the search.

        :param board: Current board position.
        :type board: Board
        :return: The mating move.
        :rtype: int
        """
        return self.__dfpn.get_move(board.__board)

    def get_pv(self, Board board):
        """Gets the principal variation (PV) of the mating sequence found by the search.

        :param board: Current board position.
        :type board: Board
        :return: The PV of the mating sequence.
        :rtype: list of unsigned int
        """
        self.__dfpn.get_pv(board.__board)
        return self.__dfpn.pv

    def set_draw_ply(self, int draw_ply):
        """Sets the number of plies for a draw.

        :param draw_ply: Number of plies for a draw.
        :type draw_ply: int
        """
        self.__dfpn.set_draw_ply(draw_ply)

    def set_max_depth(self, int max_depth):
        """Sets the maximum search depth.

        :param max_depth: Maximum search depth.
        :type max_depth: int
        """
        self.__dfpn.set_maxdepth(max_depth)

    def set_max_search_node(self, int max_search_node):
        """Sets the maximum number of search nodes.

        :param max_search_node: Maximum number of search nodes.
        :type max_search_node: int
        """
        self.__dfpn.set_max_search_node(max_search_node)

    @property
    def searched_node(self):
        """Gets the number of nodes searched.

        :return: Number of nodes searched.
        :rtype: unsigned int
        """
        return self.__dfpn.get_searched_node()
