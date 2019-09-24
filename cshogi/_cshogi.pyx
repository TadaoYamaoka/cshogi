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

cdef modify_move(np.ndarray[np.uint16_t, ndim=1] move):
	cdef np.ndarray[np.uint16_t, ndim=1] drop_mask
	cdef np.ndarray[np.uint16_t, ndim=1] drop_offset
	cdef np.ndarray[np.uint16_t, ndim=1] promotion_mask

	# 駒打ちのフラグを取り出す
	drop_mask = move & 0x4000

	drop_flag = drop_mask >> 14
	drop_offset = (drop_flag * 80) << 7
	move += drop_offset
	# 駒打ちのフラグを消す
	move ^= drop_mask

	# 成りのフラグを取り出す
	promotion_mask = move & 0x8000
	promotion_mask |= promotion_mask >> 1
	# 成りのフラグをbit15からbit14に移動
	move ^= promotion_mask

	return move

cdef load_packed_sfen_value(str path):
	packed_sfen_value = np.fromfile(path, dtype=PackedSfenValue)
	packed_sfen_value['move'] = modify_move(packed_sfen_value['move'])
	return packed_sfen_value

dtypeKey = np.dtype(np.uint64)
BookEntry = np.dtype([
    ('key', dtypeKey),
    ('fromToPro', dtypeMove16),
    ('count', np.uint16),
    ('score', np.int32),
	])

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

COLORS = [BLACK, WHITE] = range(2)

GAME_RESULTS = [
    DRAW, BLACK_WIN, WHITE_WIN,
] = range(3)

PIECE_TYPES_WITH_NONE = [NONE,
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

PIECES = [NONE,
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
SVG_PIECE_DEF_IDS = [None,
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
HAND_PIECE_STRS = [
	"歩", "香", "桂", "銀",
	"金",
	"角", "飛",
]
JAPANESE_NUMBERS = [ None, "一", "二", "三", "四", "五", "六", "七", "八", "九" ]
SVG_SQUARES = '<g stroke="black"><rect x="20" y="10" width="181" height="181" fill="none" stroke-width="1.5" /><line x1="20.5" y1="30.5" x2="200.5" y2="30.5" stroke-width="1.0" /><line x1="20.5" y1="50.5" x2="200.5" y2="50.5" stroke-width="1.0" /><line x1="20.5" y1="70.5" x2="200.5" y2="70.5" stroke-width="1.0" /><line x1="20.5" y1="90.5" x2="200.5" y2="90.5" stroke-width="1.0" /><line x1="20.5" y1="110.5" x2="200.5" y2="110.5" stroke-width="1.0" /><line x1="20.5" y1="130.5" x2="200.5" y2="130.5" stroke-width="1.0" /><line x1="20.5" y1="150.5" x2="200.5" y2="150.5" stroke-width="1.0" /><line x1="20.5" y1="170.5" x2="200.5" y2="170.5" stroke-width="1.0" /><line x1="40.5" y1="10.5" x2="40.5" y2="203.5" stroke-width="1.0" /><line x1="60.5" y1="10.5" x2="60.5" y2="203.5" stroke-width="1.0" /><line x1="80.5" y1="10.5" x2="80.5" y2="203.5" stroke-width="1.0" /><line x1="100.5" y1="10.5" x2="100.5" y2="203.5" stroke-width="1.0" /><line x1="120.5" y1="10.5" x2="120.5" y2="203.5" stroke-width="1.0" /><line x1="140.5" y1="10.5" x2="140.5" y2="203.5" stroke-width="1.0" /><line x1="160.5" y1="10.5" x2="160.5" y2="203.5" stroke-width="1.0" /><line x1="180.5" y1="10.5" x2="180.5" y2="203.5" stroke-width="1.0" /></g>'
SVG_COORDINATES = '<g><text font-family="serif" text-anchor="middle" font-size="9" x="30.5" y="8">9</text><text font-family="serif" text-anchor="middle" font-size="9" x="50.5" y="8">8</text><text font-family="serif" text-anchor="middle" font-size="9" x="70.5" y="8">7</text><text font-family="serif" text-anchor="middle" font-size="9" x="90.5" y="8">6</text><text font-family="serif" text-anchor="middle" font-size="9" x="110.5" y="8">5</text><text font-family="serif" text-anchor="middle" font-size="9" x="130.5" y="8">4</text><text font-family="serif" text-anchor="middle" font-size="9" x="150.5" y="8">3</text><text font-family="serif" text-anchor="middle" font-size="9" x="170.5" y="8">2</text><text font-family="serif" text-anchor="middle" font-size="9" x="190.5" y="8">1</text><text font-family="serif" font-size="9" x="203.5" y="23">一</text><text font-family="serif" font-size="9" x="203.5" y="43">二</text><text font-family="serif" font-size="9" x="203.5" y="63">三</text><text font-family="serif" font-size="9" x="203.5" y="83">四</text><text font-family="serif" font-size="9" x="203.5" y="103">五</text><text font-family="serif" font-size="9" x="203.5" y="123">六</text><text font-family="serif" font-size="9" x="203.5" y="143">七</text><text font-family="serif" font-size="9" x="203.5" y="163">八</text><text font-family="serif" font-size="9" x="203.5" y="183">九</text></g>'

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
	return __to_usi(move)

def to_csa(int move):
	return __to_csa(move)

cdef extern from "cshogi.h":
	cdef cppclass __Board:
		__Board() except +
		__Board(const string& sfen) except +
		__Board(const __Board& board) except +
		bool set(const string& sfen)
		bool set_hcp(char* hcp)
		bool set_psfen(char* psfen)
		void reset()
		string dump()
		void push(const int move)
		void pop(const int move)
		bool is_game_over()
		int isDraw(const int checkMaxPly)
		int move(const int from_square, const int to_square, const bool promotion)
		int drop_move(const int to_square, const int drop_piece_type)
		int move_from_usi(const string& usi)
		int move_from_csa(const string& csa)
		int move_from_move16(const unsigned short move16)
		int turn()
		int ply()
		string toSFEN()
		void toHuffmanCodedPos(char* data)
		int piece(const int sq)
		bool inCheck()
		int mateMoveIn1Ply()
		unsigned long long getKey()
		bool moveIsPseudoLegal(const int move)
		bool moveIsLegal(const int move)
		vector[int] pieces_in_hand(const int color)
		vector[int] pieces()
		bool is_nyugyoku()
		void piece_planes(char* mem)
		bool isOK()
		unsigned long long bookKey()

cdef class Board:
	cdef __Board __board

	def __cinit__(self, str sfen=None, Board board=None):
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
		return Board(board=self)

	def set_sfen(self, str sfen):
		cdef string sfen_b = sfen.encode('ascii')
		self.__board.set(sfen_b)

	def set_hcp(self, np.ndarray hcp):
		return self.__board.set_hcp(hcp.data)

	def set_psfen(self, np.ndarray psfen):
		return self.__board.set_psfen(psfen.data)

	def reset(self):
		self.__board.reset()

	def __repr__(self):
		return self.__board.dump().decode('ascii')

	def push(self, int move):
		self.__board.push(move)

	def push_usi(self, str usi):
		cdef string usi_b = usi.encode('ascii')
		move = self.__board.move_from_usi(usi_b)
		self.__board.push(move)
		return move

	def push_csa(self, str csa):
		cdef string csa_b = csa.encode('ascii')
		move = self.__board.move_from_csa(csa_b)
		self.__board.push(move)
		return move

	def push_move16(self, unsigned short move16):
		move = self.__board.move_from_move16(move16)
		self.__board.push(move)
		return move

	def pop(self, int move):
		self.__board.pop(move)

	def is_game_over(self):
		return self.__board.is_game_over()

	def is_draw(self, ply=None):
		cdef int _ply
		if ply:
			_ply = ply
		else:
			_ply = 2147483648
		return self.__board.isDraw(_ply)

	def move(self, int from_square, int to_square, bool promotion):
		return self.__board.move(from_square, to_square, promotion)

	def drop_move(self, int to_square, int drop_piece_type):
		return self.__board.drop_move(to_square, drop_piece_type)

	def move_from_usi(self, str usi):
		cdef string usi_b = usi.encode('ascii')
		return self.__board.move_from_usi(usi_b)

	def move_from_csa(self, str csa):
		cdef string csa_b = csa.encode('ascii')
		return self.__board.move_from_csa(csa_b)

	def move_from_move16(self, unsigned short move16):
		return self.__board.move_from_move16(move16)

	@property
	def legal_moves(self):
		return LegalMoveList(self)

	@property
	def turn(self):
		return self.__board.turn()

	@property
	def move_number(self):
		return self.__board.ply()

	def sfen(self):
		return self.__board.toSFEN().decode('ascii')

	def to_hcp(self, np.ndarray hcp):
		return self.__board.toHuffmanCodedPos(hcp.data)

	def piece(self, int sq):
		return self.__board.piece(sq)

	def is_check(self):
		return self.__board.inCheck()

	def mate_move_in_1ply(self):
		return self.__board.mateMoveIn1Ply()

	def zobrist_hash(self):
		return self.__board.getKey()

	def is_pseudo_legal(self, int move):
		return self.__board.moveIsPseudoLegal(move)

	def is_legal(self, int move):
		return self.__board.moveIsLegal(move)

	@property
	def pieces_in_hand(self):
		return (self.__board.pieces_in_hand(BLACK), self.__board.pieces_in_hand(WHITE))

	@property
	def pieces(self):
		return self.__board.pieces()

	def is_nyugyoku(self):
		return self.__board.is_nyugyoku()

	def piece_planes(self, np.ndarray features):
		return self.__board.piece_planes(features.data)

	def is_ok(self):
		return self.__board.isOK()

	def book_key(self):
		return self.__board.bookKey()

	def to_svg(self, lastmove=None, scale=1.0):
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
				if n >= 2:
					hand_pieces[c].append((i, JAPANESE_NUMBERS[n]))
					i += 1
				if n >= 1:
					hand_pieces[c].append((i, HAND_PIECE_STRS[hp]))
					i += 1
			i += 1
			hand_pieces[c].append((i, "手"))
			i += 1
			hand_pieces[c].append((i, "先" if c == BLACK else "後"))
			i += 1
			hand_pieces[c].append(( i, "☗" if c == BLACK else "☖"))

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
		return self.to_svg()

cdef extern from "cshogi.h":
	cdef cppclass __LegalMoveList:
		__LegalMoveList() except +
		__LegalMoveList(const __Board& board) except +
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
	string __move_to_usi(const int move)
	string __move_to_csa(const int move)

cdef class LegalMoveList:
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

def move_to(int move):
	return __move_to(move)

def move_from(int move):
	return __move_from(move)

def move_cap(int move):
	return __move_cap(move)

def move_is_promotion(int move):
	return __move_is_promotion(move)

def move_is_drop(int move):
	return __move_is_drop(move)

def move_from_piece_type(int move):
	return __move_from_piece_type(move)

def move_drop_hand_piece(int move):
	return __move_drop_hand_piece(move)

def move16(int move):
	return __move16(move)

def move_to_usi(int move):
	return __move_to_usi(move).decode('ascii')

def move_to_csa(int move):
	return __move_to_csa(move).decode('ascii')

def opponent(color):
	return BLACK if color == WHITE else WHITE

cdef extern from "parser.h" namespace "parser":
	cdef cppclass __Parser:
		__Parser() except +
		string sfen
		string endgame
		vector[string] names
		vector[float] ratings
		vector[int] moves
		vector[int] scores
		int win
		void parse_csa_file(const string& path) except +
		void parse_csa_str(const string& csa_str) except +

cdef class Parser:
	cdef __Parser __parser

	def __cinit__(self):
		self.__parser = __Parser()

	def parse_csa_file(self, str path):
		cdef string path_b = path.encode(locale.getpreferredencoding())
		self.__parser.parse_csa_file(path_b)

	def parse_csa_str(self, str csa_str):
		cdef string csa_str_b = csa_str.encode('utf-8')
		self.__parser.parse_csa_str(csa_str_b)

	@property
	def sfen(self):
		return self.__parser.sfen.decode('ascii')

	@property
	def endgame(self):
		return self.__parser.endgame.decode('ascii')

	@property
	def names(self):
		return [name.decode('ascii') for name in self.__parser.names]

	@property
	def ratings(self):
		return self.__parser.ratings

	@property
	def moves(self):
		return self.__parser.moves

	@property
	def scores(self):
		return self.__parser.scores

	@property
	def win(self):
		return self.__parser.win
