#include "../src/init.hpp"
#include "../src/cshogi.h"

#include <array>
#include <iostream>
#include <string>

namespace {
template <MoveType MT>
void print_checks(const char* label, const Position& pos) {
    std::array<ExtMove, MaxLegalMoves> buffer{};
    ExtMove* last = generateMoves<MT>(buffer.data(), pos);
    CheckInfo ci(pos);
    std::cout << label << ':';
    for (ExtMove* it = buffer.data(); it != last; ++it) {
        if (pos.moveIsLegal(it->move) && pos.moveGivesCheck(it->move, ci)) {
            std::cout << ' ' << it->move.toUSI();
        }
    }
    std::cout << "\n";
}
}

int main(int argc, char** argv) {
    initTable();
    Position::initZobrist();
    if (argc < 2) {
        std::cerr << "usage: codex_check_probe <position>\n";
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
    std::cout << "sfen=" << board.toSFEN() << "\n";
    print_checks<CheckAll>("checkall", board.pos);
    print_checks<CheckAllOslmate>("oslmate", board.pos);
    return 0;
}
