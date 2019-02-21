#include "cshogi.h"
#include "parser.h"

void test_parser() {
	parser::__Parser parser;
	parser.parse_csa_file(R"(R:\csa\0\wdoor+floodgate-600-10F+ABC-XYZ+pinaniwa+20160519230005.csa)");

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

int main()
{
	initTable();
	Position::initZobrist();
	HuffmanCodedPos::init();

	//test_position();
	//test_parser();
	test_draw();

	return 0;
}