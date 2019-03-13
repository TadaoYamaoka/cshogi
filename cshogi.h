#ifndef CSHOGI_H
#define CSHOGI_H

#include "init.hpp"
#include "position.hpp"
#include "generateMoves.hpp"
#include "usi.hpp"

bool nyugyoku(const Position& pos);

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

	void set(const std::string& sfen) {
		states.clear();
		pos.set(sfen);
	}
	bool set_hcp(const unsigned char* hcp) {
		states.clear();
		return pos.set_hcp((const char*)hcp);
	}
	bool set_psfen(const char* psfen) {
		states.clear();
		return pos.set_psfen(psfen);
	}

	void reset() {
		states.clear();
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
	int ply() const { return pos.gamePly() + (int)states.size(); }
	std::string toSFEN() const { return pos.toSFEN(); }
	void toHuffmanCodedPos(char* data) const { std::memcpy(data, pos.toHuffmanCodedPos().data, sizeof(HuffmanCodedPos)); }
	int piece(const int sq) const { return (int)pos.piece((Square)sq); }
	bool inCheck() const { return pos.inCheck(); }
	int mateMoveIn1Ply() { return pos.mateMoveIn1Ply().value(); }
	unsigned long long getKey() const { return pos.getKey(); }
	bool moveIsPseudoLegal(const int move) const { return pos.moveIsPseudoLegal(Move(move)); }
	bool moveIsLegal(const int move) const { return pos.moveIsLegal(Move(move)); }
	bool is_nyugyoku() const { return nyugyoku(pos); }

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

	Position pos;

private:
	std::deque<StateInfo> states;

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

std::string __move_to_usi(const int move) { return Move(move).toUSI(); }
std::string __move_to_csa(const int move) { return Move(move).toCSA(); }

struct node_hash_t {
	unsigned long long hash;
	Color color;
	int moves;
	unsigned int generation;
};

class __NodeHash
{
public:
	~__NodeHash() {
		delete[] node_hash;
	}

	//  ハッシュテーブルのサイズの設定
	void SetHashSize(const unsigned int hash_size) {
		if (!(hash_size & (hash_size - 1))) {
			uct_hash_size = hash_size;
			uct_hash_limit = hash_size * 9 / 10;
		}
		else {
			throw std::domain_error("Hash size must be 2 ^ n");
		}

		node_hash = new node_hash_t[hash_size];

		if (node_hash == nullptr) {
			throw std::bad_alloc();
		}

		used = 0;
		enough_size = true;

		for (unsigned int i = 0; i < uct_hash_size; i++) {
			node_hash[i].generation = 0;
		}
	}

	// ハッシュをクリアする（世代を新しくする）
	void Clear() {
		enough_size = true;
		generation++;
		// 世代を使い切ったときは1から振りなおす
		if (generation == 0)
			generation = 1;
	}

	// 古いハッシュを削除する
	void DeleteOldHash(const int moves) {
		used = 0;
		enough_size = true;

		for (unsigned int i = 0; i < uct_hash_size; i++) {
			if (node_hash[i].generation != 0) {
				if (node_hash[i].moves < moves)
					node_hash[i].generation = 0;
				else
					used++;
			}
		}
	}

	// 未使用のインデックスを探す
	unsigned int SearchEmptyIndex(const unsigned long long hash, const int color, const int moves) {
		const unsigned int key = TransHash(hash);
		unsigned int i = key;

		do {
			if (node_hash[i].generation != generation) {
				node_hash[i].hash = hash;
				node_hash[i].moves = moves;
				node_hash[i].color = (Color)color;
				node_hash[i].generation = generation;
				used++;
				if (used > uct_hash_limit)
					enough_size = false;
				return i;
			}
			i++;
			if (i >= uct_hash_size) i = 0;
		} while (i != key);

		return uct_hash_size;
	}

	// ハッシュ値に対応するインデックスを返す
	unsigned int FindSameHashIndex(const unsigned long long hash, const int moves) const {
		const unsigned int key = TransHash(hash);
		unsigned int i = key;

		do {
			if (node_hash[i].generation != generation) {
				return uct_hash_size;
			}
			else if (node_hash[i].hash == hash &&
				node_hash[i].moves == moves &&
				node_hash[i].generation == generation) {
				return i;
			}
			i++;
			if (i >= uct_hash_size) i = 0;
		} while (i != key);

		return uct_hash_size;
	}

	//  ハッシュ表が埋まっていないか確認
	bool CheckEnoughSize() const { return enough_size; }

private:
	//  UCT用ハッシュテーブルのサイズ
	unsigned int uct_hash_size;
	unsigned int uct_hash_limit;

	//  UCT用ハッシュテーブル
	node_hash_t* node_hash = nullptr;
	unsigned int used;
	bool enough_size;

	// 世代
	unsigned int generation = 1;

	//  インデックスの取得
	unsigned int TransHash(const unsigned long long hash) const {
		return ((hash & 0xffffffff) ^ ((hash >> 32) & 0xffffffff)) & (uct_hash_size - 1);
	}
};

#endif
