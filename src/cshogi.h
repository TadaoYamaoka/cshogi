#ifndef CSHOGI_H
#define CSHOGI_H

#include "init.hpp"
#include "position.hpp"
#include "generateMoves.hpp"
#include "usi.hpp"
#include "book.hpp"
#include "mate.h"
#include "dfpn.h"

// 入力特徴量のための定数(dlshogi互換)
constexpr int PIECETYPE_NUM = 14; // 駒の種類
constexpr int MAX_ATTACK_NUM = 3; // 利き数の最大値
constexpr u32 MAX_FEATURES1_NUM = PIECETYPE_NUM/*駒の配置*/ + PIECETYPE_NUM/*駒の利き*/ + MAX_ATTACK_NUM/*利き数*/;

constexpr int MAX_HPAWN_NUM = 8; // 歩の持ち駒の上限
constexpr int MAX_HLANCE_NUM = 4;
constexpr int MAX_HKNIGHT_NUM = 4;
constexpr int MAX_HSILVER_NUM = 4;
constexpr int MAX_HGOLD_NUM = 4;
constexpr int MAX_HBISHOP_NUM = 2;
constexpr int MAX_HROOK_NUM = 2;
constexpr u32 MAX_PIECES_IN_HAND[] = {
	MAX_HPAWN_NUM, // PAWN
	MAX_HLANCE_NUM, // LANCE
	MAX_HKNIGHT_NUM, // KNIGHT
	MAX_HSILVER_NUM, // SILVER
	MAX_HGOLD_NUM, // GOLD
	MAX_HBISHOP_NUM, // BISHOP
	MAX_HROOK_NUM, // ROOK
};
constexpr u32 MAX_PIECES_IN_HAND_SUM = MAX_HPAWN_NUM + MAX_HLANCE_NUM + MAX_HKNIGHT_NUM + MAX_HSILVER_NUM + MAX_HGOLD_NUM + MAX_HBISHOP_NUM + MAX_HROOK_NUM;
constexpr u32 MAX_FEATURES2_HAND_NUM = (int)ColorNum * MAX_PIECES_IN_HAND_SUM;
constexpr u32 MAX_FEATURES2_NUM = MAX_FEATURES2_HAND_NUM + 1/*王手*/;

// 移動の定数
enum MOVE_DIRECTION {
	UP, UP_LEFT, UP_RIGHT, LEFT, RIGHT, DOWN, DOWN_LEFT, DOWN_RIGHT, UP2_LEFT, UP2_RIGHT,
	UP_PROMOTE, UP_LEFT_PROMOTE, UP_RIGHT_PROMOTE, LEFT_PROMOTE, RIGHT_PROMOTE, DOWN_PROMOTE, DOWN_LEFT_PROMOTE, DOWN_RIGHT_PROMOTE, UP2_LEFT_PROMOTE, UP2_RIGHT_PROMOTE,
	MOVE_DIRECTION_NUM
};

int __dlshogi_get_features1_num() {
	return 2 * MAX_FEATURES1_NUM;
}
int __dlshogi_get_features2_num() {
	return MAX_FEATURES2_NUM;
}


bool nyugyoku(const Position& pos);

void HuffmanCodedPos_init() {
	HuffmanCodedPos::init();
}

void PackedSfen_init() {
	PackedSfen::init();
}

void Book_init() {
	Book::init();
}

std::string __to_usi(const int move) {
	return Move(move).toUSI();
}

std::string __to_csa(const int move) {
	return Move(move).toCSA();
}

unsigned short __move16_from_psv(const unsigned short move16);

class __Board
{
public:
	__Board() : pos(DefaultStartPositionSFEN) {}
	__Board(const std::string& sfen) : pos(sfen) {}
	~__Board() {}

	void set(const std::string& sfen) {
		history.clear();
		pos.set(sfen);
	}
	bool set_position(std::string& position) {
		history.clear();
		std::istringstream ssPosCmd(position);
		std::string token;
		std::string sfen;

		ssPosCmd >> token;

		if (token == "startpos") {
			sfen = DefaultStartPositionSFEN;
			ssPosCmd >> token; // "moves" が入力されるはず。
		}
		else if (token == "sfen") {
			while (ssPosCmd >> token && token != "moves")
				sfen += token + " ";
		}
		else
			return false;

		pos.set(sfen);

		while (ssPosCmd >> token) {
			const Move move = usiToMove(pos, token);
			if (!move) return false;
			push(move.value());
		}

		return true;
	}
	void set_pieces(const int pieces[], const int pieces_in_hand[][7]) {
		history.clear();
		pos.set((const Piece*)pieces, pieces_in_hand);
	}
	bool set_hcp(char* hcp) {
		history.clear();
		return pos.set_hcp(hcp);
	}
	bool set_psfen(char* psfen) {
		history.clear();
		return pos.set_psfen(psfen);
	}

	void reset() {
		history.clear();
		pos.set(DefaultStartPositionSFEN);
	}

	std::string dump() const {
		std::stringstream ss;
		pos.print(ss);
		return ss.str();
	}

	void push(const int move) {
		history.emplace_back(move, StateInfo());
		pos.doMove(Move(move), history.back().second);
	}

	int pop() {
		const auto move = history.back().first;
		pos.undoMove(move);
		history.pop_back();
		return move.value();
	}

	int peek() {
		if (history.size() > 0)
			return history.back().first.value();
		else
			return Move::moveNone().value();
	}

	void push_pass() {
		history.emplace_back(Move::moveNull(), StateInfo());
		pos.doNullMove<true>(history.back().second);
	}
	void pop_pass() {
		pos.doNullMove<false>(history.back().second);
		history.pop_back();
	}

	std::vector<int> get_history() const {
		std::vector<int> result;
		result.reserve(history.size());
		for (auto& m : history) {
			result.emplace_back((int)m.first.value());
		}
		return std::move(result);
	}

	bool is_game_over() const {
		const MoveList<LegalAll> ml(pos);
		return ml.size() == 0;
	}

	int isDraw(const int checkMaxPly) const { return (int)pos.isDraw(checkMaxPly); }

	int move(const int from_square, const int to_square, const bool promotion) const {
		if (promotion)
			return makePromoteMove<Capture>(pieceToPieceType(pos.piece((Square)from_square)), (Square)from_square, (Square)to_square, pos).value();
		else
			return makeNonPromoteMove<Capture>(pieceToPieceType(pos.piece((Square)from_square)), (Square)from_square, (Square)to_square, pos).value();
	}

	int drop_move(const int to_square, const int drop_piece_type) const {
		return makeDropMove((PieceType)drop_piece_type, (Square)to_square).value();
	}

	int move_from_usi(const std::string& usi) const {
		return usiToMove(pos, usi).value();
	}

	int move_from_csa(const std::string& csa) const {
		return csaToMove(pos, csa).value();
	}

	int move_from_move16(const unsigned short move16) const {
		return move16toMove(Move(move16), pos).value();
	}

	int move_from_psv(const unsigned short move16) const {
		return move16toMove(Move(__move16_from_psv(move16)), pos).value();
	}

	int turn() const { return pos.turn(); }
	void setTurn(const int turn) { pos.setTurn((Color)turn); }
	int ply() const { return pos.gamePly(); }
	void setPly(const int ply) { pos.setStartPosPly(ply); }
	std::string toSFEN() const { return pos.toSFEN(); }
	std::string toCSAPos() const { return pos.toCSAPos(); }
	void toHuffmanCodedPos(char* data) const { pos.toHuffmanCodedPos((u8*)data); }
	void toPackedSfen(char* data) const { pos.toPackedSfen((u8*)data); }
	int piece(const int sq) const { return (int)pos.piece((Square)sq); }
	bool inCheck() const { return pos.inCheck(); }
	int mateMoveIn1Ply() { return pos.mateMoveIn1Ply().value(); }
	int mateMove(int ply) {
		if (pos.inCheck())
			return mateMoveInOddPlyReturnMove<true>(pos, ply).value();
		else
			return mateMoveInOddPlyReturnMove<false>(pos, ply).value();
	}
	bool is_mate(int ply) {
		return mateMoveInEvenPly(pos, ply);
	}
	unsigned long long getKey() const { return pos.getKey(); }
	bool moveIsPseudoLegal(const int move) const { return pos.moveIsPseudoLegal(Move(move)); }
	bool moveIsLegal(const int move) const { return pos.moveIsLegal(Move(move)); }
	bool is_nyugyoku() const { return nyugyoku(pos); }
	bool isOK() const { return pos.isOK(); }

	std::vector<int> pieces_in_hand(const int color) const {
		const Hand h = pos.hand((Color)color);
		return std::vector<int>{
			(int)h.numOf<HPawn>(), (int)h.numOf<HLance>(), (int)h.numOf<HKnight>(), (int)h.numOf<HSilver>(), (int)h.numOf<HGold>(), (int)h.numOf<HBishop>(), (int)h.numOf<HRook>()
		};
	}

	std::vector<int> pieces() const {
		std::vector<int> board(81);

		bbToVector(Pawn, Black, BPawn, board);
		bbToVector(Lance, Black, BLance, board);
		bbToVector(Knight, Black, BKnight, board);
		bbToVector(Silver, Black, BSilver, board);
		bbToVector(Bishop, Black, BBishop, board);
		bbToVector(Rook, Black, BRook, board);
		bbToVector(Gold, Black, BGold, board);
		bbToVector(King, Black, BKing, board);
		bbToVector(ProPawn, Black, BProPawn, board);
		bbToVector(ProLance, Black, BProLance, board);
		bbToVector(ProKnight, Black, BProKnight, board);
		bbToVector(ProSilver, Black, BProSilver, board);
		bbToVector(Horse, Black, BHorse, board);
		bbToVector(Dragon, Black, BDragon, board);

		bbToVector(Pawn, White, WPawn, board);
		bbToVector(Lance, White, WLance, board);
		bbToVector(Knight, White, WKnight, board);
		bbToVector(Silver, White, WSilver, board);
		bbToVector(Bishop, White, WBishop, board);
		bbToVector(Rook, White, WRook, board);
		bbToVector(Gold, White, WGold, board);
		bbToVector(King, White, WKing, board);
		bbToVector(ProPawn, White, WProPawn, board);
		bbToVector(ProLance, White, WProLance, board);
		bbToVector(ProKnight, White, WProKnight, board);
		bbToVector(ProSilver, White, WProSilver, board);
		bbToVector(Horse, White, WHorse, board);
		bbToVector(Dragon, White, WDragon, board);

		return std::move(board);
	}

	void piece_planes(char* mem) const {
		// P1 piece 14 planes
		// P2 piece 14 planes
		float* data = (float*)mem;
		for (Color c = Black; c < ColorNum; ++c) {
			for (PieceType pt = Pawn; pt < PieceTypeNum; ++pt) {
				Bitboard bb = pos.bbOf(pt, c);
				while (bb) {
					const Square sq = bb.firstOneFromSQ11();
					data[sq] = 1.0f;
				}
				data += 81;
			}
		}
	}

	// 白の場合、盤を反転するバージョン
	void piece_planes_rotate(char* mem) const {
		// P1 piece 14 planes
		// P2 piece 14 planes
		if (pos.turn() == Black) {
			// 黒の場合
			piece_planes(mem);
			return;
		}
		// 白の場合
		float* data = (float*)mem;
		for (Color c = White; c >= Black; --c) {
			for (PieceType pt = Pawn; pt < PieceTypeNum; ++pt) {
				Bitboard bb = pos.bbOf(pt, c);
				while (bb) {
					// 盤面を180度回転
					const Square sq = SQ99 - bb.firstOneFromSQ11();
					data[sq] = 1.0f;
				}
				data += 81;
			}
		}
	}

	// 駒の利き、王手情報を含む特徴量(dlshogi互換)
	void _dlshogi_make_input_features(char* mem1, char* mem2) const {
		typedef float features1_t[ColorNum][MAX_FEATURES1_NUM][SquareNum];
		typedef float features2_t[MAX_FEATURES2_NUM][SquareNum];
		features1_t* const features1 = reinterpret_cast<features1_t* const>(mem1);
		features2_t* const features2 = reinterpret_cast<features2_t* const>(mem2);
		float(* const features2_hand)[ColorNum][MAX_PIECES_IN_HAND_SUM][SquareNum] = reinterpret_cast<float(* const)[ColorNum][MAX_PIECES_IN_HAND_SUM][SquareNum]>(mem2);

		std::fill_n((float*)features1, sizeof(features1_t) / sizeof(float), 0);
		std::fill_n((float*)features2, sizeof(features2_t) / sizeof(float), 0);

		const Bitboard occupied_bb = pos.occupiedBB();

		// 駒の利き(駒種でマージ)
		Bitboard attacks[ColorNum][PieceTypeNum] = {
			{ { 0, 0 },{ 0, 0 },{ 0, 0 },{ 0, 0 },{ 0, 0 },{ 0, 0 },{ 0, 0 },{ 0, 0 },{ 0, 0 },{ 0, 0 },{ 0, 0 },{ 0, 0 },{ 0, 0 },{ 0, 0 },{ 0, 0 } },
			{ { 0, 0 },{ 0, 0 },{ 0, 0 },{ 0, 0 },{ 0, 0 },{ 0, 0 },{ 0, 0 },{ 0, 0 },{ 0, 0 },{ 0, 0 },{ 0, 0 },{ 0, 0 },{ 0, 0 },{ 0, 0 },{ 0, 0 } },
		};
		for (Square sq = SQ11; sq < SquareNum; sq++) {
			const Piece p = pos.piece(sq);
			if (p != Empty) {
				const Color pc = pieceToColor(p);
				const PieceType pt = pieceToPieceType(p);
				const Bitboard bb = pos.attacksFrom(pt, pc, sq, occupied_bb);
				attacks[pc][pt] |= bb;
			}
		}

		for (Color c = Black; c < ColorNum; ++c) {
			// 白の場合、色を反転
			const Color c2 = pos.turn() == Black ? c : oppositeColor(c);

			// 駒の配置
			Bitboard bb[PieceTypeNum];
			for (PieceType pt = Pawn; pt < PieceTypeNum; ++pt) {
				bb[pt] = pos.bbOf(pt, c);
			}

			for (Square sq = SQ11; sq < SquareNum; ++sq) {
				// 白の場合、盤面を180度回転
				const Square sq2 = pos.turn() == Black ? sq : SQ99 - sq;

				for (PieceType pt = Pawn; pt < PieceTypeNum; ++pt) {
					// 駒の配置
					if (bb[pt].isSet(sq)) {
						(*features1)[c2][pt - 1][sq2] = 1;
					}

					// 駒の利き
					if (attacks[c][pt].isSet(sq)) {
						(*features1)[c2][PIECETYPE_NUM + pt - 1][sq2] = 1;
					}
				}

				// 利き数
				const int num = std::min(MAX_ATTACK_NUM, pos.attackersTo(c, sq, occupied_bb).popCount());
				for (int k = 0; k < num; k++) {
					(*features1)[c2][PIECETYPE_NUM + PIECETYPE_NUM + k][sq2] = 1;
				}
			}
			// hand
			const Hand hand = pos.hand(c);
			int p = 0;
			for (HandPiece hp = HPawn; hp < HandPieceNum; ++hp) {
				u32 num = hand.numOf(hp);
				if (num >= MAX_PIECES_IN_HAND[hp]) {
					num = MAX_PIECES_IN_HAND[hp];
				}
				std::fill_n((*features2_hand)[c2][p], (int)SquareNum * num, 1);
				p += MAX_PIECES_IN_HAND[hp];
			}
		}

		// is check
		if (pos.inCheck()) {
			std::fill_n((*features2)[MAX_FEATURES2_HAND_NUM], SquareNum, 1);
		}
	}

	unsigned long long bookKey() {
		return Book::bookKey(pos);
	}

	Position pos;

private:
	std::deque<std::pair<Move, StateInfo>> history;

	void bbToVector(PieceType pt, Color c, Piece piece, std::vector<int>& board) const {
		Bitboard bb = pos.bbOf(pt, c);
		while (bb) {
			const Square sq = bb.firstOneFromSQ11();
			board[sq] = piece;
		}
	}
};

class __LegalMoveList
{
public:
	__LegalMoveList() {}
	__LegalMoveList(const __Board& board) {
		ml.reset(new MoveList<LegalAll>(board.pos));
	}

	bool end() const { return ml->end(); }
	int move() const { return ml->move().value(); }
	void next() { ++(*ml); }
	int size() const { return (int)ml->size(); }

private:
	std::shared_ptr<MoveList<LegalAll>> ml;
};

int __piece_to_piece_type(const int p) { return (int)pieceToPieceType((Piece)p); }

// 移動先
int __move_to(const int move) { return (move >> 0) & 0x7f; }
// 移動元
int __move_from(const int move) { return (move >> 7) & 0x7f; }
// 取った駒の種類
int __move_cap(const int move) { return (move >> 20) & 0xf; }
// 成るかどうか
bool __move_is_promotion(const int move) { return move & Move::PromoteFlag; }
// 駒打ちか
bool __move_is_drop(const int move) { return __move_from(move) >= 81; }
// 移動する駒の種類
int __move_from_piece_type(const int move) { return (move >> 16) & 0xf; };
// 打つ駒の種類
int __move_drop_hand_piece(const int move) { return pieceTypeToHandPiece((PieceType)__move_from(move) - SquareNum + 1); }

unsigned short __move16(const int move) { return (unsigned short)move; }

unsigned short __move16_from_psv(const unsigned short move16) {
	const unsigned short MOVE_DROP = 1 << 14;
	const unsigned short MOVE_PROMOTE = 1 << 15;

	unsigned short to = move16 & 0x7f;
	unsigned short from = (move16 >> 7) & 0x7f;
	if ((move16 & MOVE_DROP) != 0) {
		from += SquareNum - 1;
	}
	return to | (from << 7) | ((move16 & MOVE_PROMOTE) != 0 ? Move::PromoteFlag : 0);
}

unsigned short __move16_to_psv(const unsigned short move16) {
	const unsigned short MOVE_DROP = 1 << 14;
	const unsigned short MOVE_PROMOTE = 1 << 15;

	unsigned short to = move16 & 0x7f;
	unsigned short from = (move16 >> 7) & 0x7f;
	unsigned short drop = 0;
	if (from >= 81) {
		from -= SquareNum - 1;
		drop = MOVE_DROP;
	}
	return to | (from << 7) | drop | ((move16 & Move::PromoteFlag) != 0 ? MOVE_PROMOTE : 0);
}

// 反転
int __move_rotate(const int move) {
	int to = __move_to(move);
	to = SQ99 - to;
	int from = __move_from(move);
	if (!__move_is_drop(move))
		from = SQ99 - from;
	return (move & 0xffff0000) | to | (from << 7);
}

std::string __move_to_usi(const int move) { return Move(move).toUSI(); }
std::string __move_to_csa(const int move) { return Move(move).toCSA(); }

inline MOVE_DIRECTION get_move_direction(const int dir_x, const int dir_y) {
	if (dir_y < 0 && dir_x == 0) {
		return UP;
	}
	else if (dir_y == -2 && dir_x == -1) {
		return UP2_LEFT;
	}
	else if (dir_y == -2 && dir_x == 1) {
		return UP2_RIGHT;
	}
	else if (dir_y < 0 && dir_x < 0) {
		return UP_LEFT;
	}
	else if (dir_y < 0 && dir_x > 0) {
		return UP_RIGHT;
	}
	else if (dir_y == 0 && dir_x < 0) {
		return LEFT;
	}
	else if (dir_y == 0 && dir_x > 0) {
		return RIGHT;
	}
	else if (dir_y > 0 && dir_x == 0) {
		return DOWN;
	}
	else if (dir_y > 0 && dir_x < 0) {
		return DOWN_LEFT;
	}
	else /* if (dir_y > 0 && dir_x > 0) */ {
		return DOWN_RIGHT;
	}
}

// 駒の移動を表すラベル(dlshogi互換)
int __dlshogi_make_move_label(const int move, const int color) {
	// see: move.hpp : 30
	// xxxxxxxx x1111111  移動先
	// xx111111 1xxxxxxx  移動元。駒打ちの際には、PieceType + SquareNum - 1
	// x1xxxxxx xxxxxxxx  1 なら成り
	const u16 move16 = (u16)move;
	u16 to_sq = move16 & 0b1111111;
	u16 from_sq = (move16 >> 7) & 0b1111111;

	if (from_sq < SquareNum) {
		// 白の場合、盤面を180度回転
		if ((Color)color == White) {
			to_sq = (u16)SQ99 - to_sq;
			from_sq = (u16)SQ99 - from_sq;
		}

		const div_t to_d = div(to_sq, 9);
		const int to_x = to_d.quot;
		const int to_y = to_d.rem;
		const div_t from_d = div(from_sq, 9);
		const int from_x = from_d.quot;
		const int from_y = from_d.rem;
		const int dir_x = from_x - to_x;
		const int dir_y = to_y - from_y;

		MOVE_DIRECTION move_direction = get_move_direction(dir_x, dir_y);

		// promote
		if ((move16 & 0b100000000000000) > 0) {
			move_direction = (MOVE_DIRECTION)(move_direction + 10);
		}
		return 9 * 9 * move_direction + to_sq;
	}
	// 持ち駒の場合
	else {
		// 白の場合、盤面を180度回転
		if ((Color)color == White) {
			to_sq = (u16)SQ99 - to_sq;
		}
		const int hand_piece = from_sq - (int)SquareNum;
		const int move_direction_label = MOVE_DIRECTION_NUM + hand_piece;
		return 9 * 9 * move_direction_label + to_sq;
	}
}

class __DfPn
{
public:
	__DfPn() {}
	__DfPn(const int max_depth, const uint32_t max_search_node, const int draw_ply) : dfpn(max_depth, max_search_node, draw_ply) {}
	bool search(__Board& board) {
		pv.clear();
		return dfpn.dfpn(board.pos);
	}
	bool search_andnode(__Board& board) {
		return dfpn.dfpn_andnode(board.pos);
	}
	void stop(bool is_stop) {
		dfpn.dfpn_stop(is_stop);
	}
	int get_move(__Board& board) {
		return dfpn.dfpn_move(board.pos).value();
	}
	void get_pv(__Board& board) {
		pv.clear();
		dfpn.get_pv(board.pos, pv);
	}

	void set_draw_ply(const int draw_ply) {
		dfpn.set_draw_ply(draw_ply);
	}
	void set_maxdepth(const int depth) {
		dfpn.set_maxdepth(depth);
	}
	void set_max_search_node(const uint32_t max_search_node) {
		dfpn.set_max_search_node(max_search_node);
	}

	uint32_t get_searched_node() {
		return dfpn.searchedNode;
	}

	std::vector<u32> pv;
private:
	DfPn dfpn;
};

#endif
