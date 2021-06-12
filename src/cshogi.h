#ifndef CSHOGI_H
#define CSHOGI_H

#include "init.hpp"
#include "position.hpp"
#include "generateMoves.hpp"
#include "usi.hpp"
#include "book.hpp"
#include "mate.h"

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

	void pop() {
		pos.undoMove(history.back().first);
		history.pop_back();
	}

	int peek() {
		if (history.size() > 0)
			return history.back().first.value();
		else
			return Move::moveNone().value();
	}

	bool is_game_over() const {
		MoveList<Legal> ml(pos);
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
	int ply() const { return pos.gamePly(); }
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
		Hand h = pos.hand((Color)color);
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

		return board;
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
		ml.reset(new MoveList<Legal>(board.pos));
	}

	bool end() const { return ml->end(); }
	int move() const { return ml->move().value(); }
	void next() { ++(*ml); }
	int size() const { return (int)ml->size(); }

private:
	std::shared_ptr<MoveList<Legal>> ml;
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

#endif
