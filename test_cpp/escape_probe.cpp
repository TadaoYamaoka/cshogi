#include "../src/init.hpp"
#include "../src/cshogi.h"

#include <array>
#include <iostream>

int main(int argc, char** argv) {
    initTable();
    Position::initZobrist();
    if (argc < 2) {
        return 1;
    }
    __Board board;
    board.set_position(argv[1]);
    for (int i = 2; i < argc; ++i) {
        const int move = board.move_from_usi(argv[i]);
        if (!board.moveIsLegal(move)) {
            std::cerr << "illegal move: " << argv[i] << "\n";
            return 2;
        }
        board.push(move);
    }
    std::cout << "sfen=" << board.toSFEN() << '\n';
    std::array<ExtMove, 1024> buffer{};
    ExtMove* last = generateOslmateEscapeMoves(buffer.data(), board.pos, false, true);
    size_t index = 0;
    for (ExtMove* it = buffer.data(); it != last; ++it, ++index) {
        std::cout << index << ' ' << it->move.toUSI() << '\n';
    }
    return 0;
}
