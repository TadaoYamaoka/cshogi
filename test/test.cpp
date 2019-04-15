#include "cshogi.h"
#include "parser.h"

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

	HuffmanCodedPos hcp = pos.toHuffmanCodedPos();
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
		if (board.isDraw() == RepetitionDraw) {
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

int main()
{
	initTable();
	Position::initZobrist();
	HuffmanCodedPos::init();

	//test_position();
	test_parser();
	//test_draw();
	//test_copy();
	//test_piece_planes();

	return 0;
}