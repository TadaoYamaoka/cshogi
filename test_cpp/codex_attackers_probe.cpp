#include "../src/init.hpp"
#include "../src/cshogi.h"

#include <iostream>
#include <string>

namespace {
std::string square_to_usi(const Square sq) {
    if (sq == SquareNum) {
        return "-";
    }
    std::string s;
    s += static_cast<char>('1' + static_cast<int>(makeFile(sq)));
    s += static_cast<char>('a' + static_cast<int>(makeRank(sq)));
    return s;
}

void print_attackers(const Position& pos, const Color color, const Square sq) {
    Bitboard attackers = pos.attackersTo(color, sq);
    std::cout << (color == Black ? "black" : "white") << "_attackers_to_" << square_to_usi(sq) << ':';
    while (attackers.isAny()) {
        const Square from = attackers.firstOneFromSQ11();
        std::cout << ' ' << square_to_usi(from)
                  << '(' << static_cast<int>(pos.piece(from)) << ')';
    }
    std::cout << "\n";
}
}

int main(int argc, char** argv) {
    initTable();
    Position::initZobrist();
    if (argc < 2) {
        std::cerr << "usage: codex_attackers_probe <position>\n";
        return 1;
    }
    __Board board;
    board.set_position(argv[1]);
    std::cout << "sfen=" << board.toSFEN() << "\n";
    const Square black_king = board.pos.kingSquare(Black);
    const Square white_king = board.pos.kingSquare(White);
    std::cout << "black_king=" << square_to_usi(black_king)
              << " white_king=" << square_to_usi(white_king)
              << " turn=" << (board.pos.turn() == Black ? "black" : "white") << "\n";
    print_attackers(board.pos, Black, white_king);
    print_attackers(board.pos, White, black_king);
    return 0;
}
