#include "../src/init.hpp"
#include "../src/cshogi.h"

#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) {
        return 1;
    }
    initTable();
    Position::initZobrist();
    __Board board;
    board.set_position(argv[1]);
    std::cout << "sfen=" << board.pos.toSFEN() << "\n";
    std::cout << "board=" << static_cast<unsigned long long>(board.pos.getBoardKey()) << "\n";
    std::cout << "key=" << static_cast<unsigned long long>(board.pos.getKey()) << "\n";
    return 0;
}
