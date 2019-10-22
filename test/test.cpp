#include "cshogi.h"
#include "parser.h"

#include <fstream>
#include <vector>
#include <string>

void test_parser() {
	parser::__Parser parser;
	parser.parse_csa_file(R"(R:\test\wdoor+floodgate-600-10+catshogi+tanuki-_5500U+20151011200002.csa)");

	std::cout << parser.sfen << std::endl;
	for (int m : parser.moves) {
		std::cout << __to_usi(m) << std::endl;
	}
	std::cout << parser.win << std::endl;
	std::cout << parser.ratings[0] << std::endl;
	std::cout << parser.ratings[1] << std::endl;
}

void test_position() {
	Position pos(DefaultStartPositionSFEN);

	HuffmanCodedPos hcp;
	pos.toHuffmanCodedPos(hcp.data);
	for (auto d : hcp.data) {
		std::cout << d << std::endl;
	}
}

void test_draw() {
	parser::__Parser parser;
	parser.parse_csa_file("R:\\test\\wdoor+floodgate-300-10F+15gou_i3_2c+ArgoCorse1.10b+20180204233002.csa");

	__Board board;
	board.set(parser.sfen);

	for (auto move : parser.moves) {
		std::cout << __to_usi(move) << std::endl;
		if (board.isDraw(16) == RepetitionDraw) {
			std::cout << "draw" << std::endl;
		}
		board.push(move);
	}
}

void test_copy() {
	__Board board;

	auto move = board.move_from_usi("7g7f");
	board.push(move);
	std::cout << board.dump() << std::endl;
	std::cout << board.ply() << std::endl;

	__Board board2(board);
	move = board2.move_from_usi("3c3d");
	board2.push(move);
	std::cout << board2.dump() << std::endl;
	std::cout << board2.ply() << std::endl;

	std::cout << board.dump() << std::endl;
	std::cout << board.ply() << std::endl;
}

void test_piece_planes() {
	__Board board;

	float data[81 * 28]{};
	board.piece_planes((char*)data);

	for (int i = 0; i < 28; ++i) {
		for (Square sq = SQ11; sq < SquareNum; ++sq)
			std::cout << data[i * 81 + sq];
		std::cout << std::endl;
	}
}

void test_psv() {
	__Board board;

	std::ifstream ifs(R"(F:\yane\shuffled_sfen1_100.bin)", std::ifstream::in | std::ifstream::binary | std::ios::ate);
	const s64 entryNum = ifs.tellg() / sizeof(PackedSfenValue);
	ifs.seekg(std::ios_base::beg);
	PackedSfenValue *psvvec = new PackedSfenValue[entryNum];
	ifs.read(reinterpret_cast<char*>(psvvec), sizeof(PackedSfenValue) * entryNum);
	ifs.close();

	board.set_psfen((char*)psvvec[0].sfen.data);
	int move = board.move_from_psv(psvvec[0].move);
	std::cout << move << std::endl;
}

void test_repetition_win() {
	__Board board;

	std::vector<std::string> moves = { "7g7f", "8c8d", "6g6f", "8d8e", "8h7g", "3c3d", "2h6h", "7a6b", "7i7h", "5a4b", "1g1f", "1c1d", "3i3h", "4b3b", "4g4f", "6a5b", "3g3f", "5c5d", "7h6g", "3a4b", "5i4h", "9c9d", "4h3i", "7c7d", "6i5h", "4b5c", "3i2h", "4a4b", "5g5f", "6b7c", "9i9h", "7c8d", "9g9f", "7d7e", "6h7h", "8b7b", "7g5i", "6c6d", "5i4h", "6d6e", "5h5g", "6e6f", "5g6f", "8e8f", "8g8f", "7e7f", "5f5e", "5d5e", "6g7f", "5e5f", "P*5e", "7b6b", "P*6e", "6b7b", "6f5f", "P*7e", "7f7e", "8d7e", "7h7e", "7b7e", "4h7e", "R*7i", "R*7a", "7i8i+", "7e5c+", "5b5c", "7a8a+", "S*4h", "N*2f", "4h4i+", "2f3d", "B*3i", "2h3g", "3i4h+", "3g2h", "4h3i", "2h3g", "3i4h", "3g2h", "4h3i", "2h3g", "3i4h", "3g2h", "4h3i", "2h3g", "3i4h" };

	for (std::string usi : moves) {
		int move = board.move_from_usi(usi);
		board.push(move);
	}

	int draw = board.isDraw(2147483647);
	std::cout << draw << std::endl;
}

void test_repetition_lose() {
	__Board board;

	std::vector<std::string> moves = { "7g7f", "4a5b", "2g2f", "3c3d", "2f2e", "2b8h+", "7i8h", "3a2b", "8h7g", "2b3c", "3i3h", "5a6b", "B*3b", "3c2b", "3b2a+", "6b7b", "2a2b", "9c9d", "2b1a", "5c5d", "1a3c", "1c1d", "5i6h", "6c6d", "L*4e", "6d6e", "N*6d", "7b6c", "6d5b+", "6a5b", "4e4c+", "5b6b", "3c2c", "7a7b", "S*7a", "8b9b", "2c4a", "6b5b", "4a5b", "6c6d", "5b5c", "6d7d", "5c7e", "7d6c", "7e5c", "6c7d", "5c7e", "7d6c", "7e5c", "6c7d", "5c7e", "7d6c", "7e5c", "6c7d" };

	for (std::string usi : moves) {
		int move = board.move_from_usi(usi);
		board.push(move);
	}

	int draw = board.isDraw(2147483647);
	std::cout << draw << std::endl;
}

int main()
{
	initTable();
	Position::initZobrist();
	HuffmanCodedPos::init();
	PackedSfen::init();

	//test_position();
	//test_parser();
	//test_draw();
	//test_copy();
	//test_piece_planes();
	//test_psv();
	test_repetition_win();
	test_repetition_lose();

	return 0;
}