#include "cshogi.h"
#include "parser.h"

void test_parser() {
	parser::__Parser parser;
	parser.parse_csa_file(R"(R:\wdoor+floodgate-300-10F+1c_note_test+coduck_pi2_600MHz_1c+20161218120004.csa)");

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

int main()
{
	initTable();
	Position::initZobrist();
	HuffmanCodedPos::init();

	test_position();

	return 0;
}