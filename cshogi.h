#ifndef CSHOGI_H
#define CSHOGI_H

#include "init.hpp"
#include "position.hpp"
#include "generateMoves.hpp"
#include "usi.hpp"

void HuffmanCodedPos_init() {
	HuffmanCodedPos::init();
}

std::string __to_usi(const int move) {
	return Move(move).toUSI();
}

std::string __to_csa(const int move) {
	return Move(move).toCSA();
}

class __Board
{
public:
	__Board() : pos(DefaultStartPositionSFEN) {}
	__Board(const std::string& sfen) : pos(sfen) {}
	~__Board() {}

	void set(const std::string& sfen) { pos.set(sfen); }
	bool set_hcp(const char* hcp) { return pos.set_hcp(hcp); }
	bool set_psfen(const char* psfen) { return pos.set_psfen(psfen); }

	void reset() {
		pos.set(DefaultStartPositionSFEN);
	}

	std::string dump() const {
		std::stringstream ss;
		pos.print(ss);
		return ss.str();
	}

	void push(const int move) {
		states.push_back(StateInfo());
		pos.doMove(Move(move), states.back());
	}

	void pop(const int move) {
		pos.undoMove(Move(move));
		states.pop_back();
	}

	bool is_game_over() const {
		MoveList<Legal> ml(pos);
		return ml.size() == 0;
	}

	int isDraw() const { return (int)pos.isDraw(); }

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

	int turn() const { return pos.turn(); }
	int ply() const { return pos.gamePly() + states.size(); }
	std::string toSFEN() const { return pos.toSFEN(); }
	void toHuffmanCodedPos(char* data) const { std::memcpy(data, pos.toHuffmanCodedPos().data, sizeof(HuffmanCodedPos)); }
	int piece(const int sq) const { return (int)pos.piece((Square)sq); }
	bool inCheck() const { return pos.inCheck(); }
	int mateMoveIn1Ply() { return pos.mateMoveIn1Ply().value(); }
	long long getKey() const { return pos.getKey(); }
	bool moveIsPseudoLegal(const int move) const { return pos.moveIsPseudoLegal(Move(move)); }
	bool moveIsLegal(const int move) const { return pos.moveIsLegal(Move(move)); }

	std::vector<unsigned int> pieces_in_hand(const int color) const {
		Hand h = pos.hand(Black);
		return std::vector<unsigned int>{
			h.numOf<HPawn>(), h.numOf<HLance>(), h.numOf<HKnight>(), h.numOf<HSilver>(), h.numOf<HGold>(), h.numOf<HBishop>(), h.numOf<HRook>()
		};
	}

	Position pos;

private:
	std::vector<StateInfo> states;
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
	int size() const { return ml->size(); }

private:
	std::shared_ptr<MoveList<Legal>> ml;
};


// 移動先
int __move_to(const int move) { return (move >> 0) & 0x7f; }
// 移動元
int __move_from(const int move) { return (move >> 7) & 0x7f; }
// 取った駒の種類
int __move_cap(const int move) { return (move >> 20) & 0xf; }
// 成るかどうか
bool __move_is_promotion(const int move) { return move & Move::PromoteFlag; }

unsigned short __move16(const int move) { return (unsigned short)move; }

std::string __move_to_usi(const int move) { return Move(move).toUSI(); }
std::string __move_to_csa(const int move) { return Move(move).toCSA(); }

#endif
