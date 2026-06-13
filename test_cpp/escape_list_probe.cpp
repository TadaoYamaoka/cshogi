#include "../src/init.hpp"
#include "../src/cshogi.h"

#include <iostream>
#include <string>
#include <vector>

namespace {
void print_moves(const char* label, ExtMove* first, ExtMove* last) {
    std::cout << label << ":";
    for (ExtMove* it = first; it != last; ++it) {
        std::cout << ' ' << it->move.toUSI();
    }
    std::cout << "\n";
}
}

int main(int argc, char** argv) {
    initTable();
    Position::initZobrist();
    if (argc < 2) {
        std::cerr << "usage: escape_list_probe <sfen>\n";
        return 1;
    }

    __Board board;
    board.set_position(argv[1]);
    std::cout << "sfen=" << board.toSFEN() << "\n";
    std::cout << "inCheck=" << (board.pos.inCheck() ? 1 : 0) << "\n";

    std::array<ExtMove, MaxLegalMoves> cheap{};
    std::array<ExtMove, MaxLegalMoves> full{};
    std::array<ExtMove, MaxLegalMoves> evasion{};
    std::array<ExtMove, MaxLegalMoves> legal{};
    print_moves("osl-cheap", cheap.data(), generateOslmateEscapeMoves(cheap.data(), board.pos, true, false));
    print_moves("osl-full", full.data(), generateOslmateEscapeMoves(full.data(), board.pos, false, false));
    print_moves("evasion", evasion.data(), generateMoves<Evasion>(evasion.data(), board.pos));
    print_moves("legal-all", legal.data(), generateMoves<LegalAll>(legal.data(), board.pos));
    return 0;
}
