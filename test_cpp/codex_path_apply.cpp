#include "../src/init.hpp"
#include "../src/cshogi.h"
#include "../src/generateMoves.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {
Move exact_usi_to_move(const Position& pos, const std::string& usi) {
    for (MoveList<LegalAll> ml(pos); !ml.end(); ++ml) {
        if (ml.move().toUSI() == usi) {
            return ml.move();
        }
    }
    ExtMove buffer[MaxLegalMoves];
    ExtMove* last = generateMoves<CheckAllOslmate>(buffer, pos);
    for (ExtMove* it = buffer; it != last; ++it) {
        if (it->move.toUSI() == usi) {
            return it->move;
        }
    }
    return Move::moveNone();
}
}

int main(int argc, char** argv) {
    initTable();
    Position::initZobrist();

    if (argc < 5 || std::string(argv[1]) != "sfen") {
        std::cerr << "usage: codex_path_apply sfen <board> <turn> <hand> <move-number> [moves...]\n";
        return 2;
    }

    std::string sfen = "sfen ";
    sfen += argv[2];
    sfen += ' ';
    sfen += argv[3];
    sfen += ' ';
    sfen += argv[4];
    sfen += ' ';
    sfen += argv[5];

    __Board board;
    board.set_position(sfen);
    for (int i = 6; i < argc; ++i) {
        const std::string usi = argv[i];
        const Move move = exact_usi_to_move(board.pos, usi);
        if (!move) {
            std::cerr << "illegal index=" << (i - 5)
                      << " move=" << usi
                      << " sfen=" << board.toSFEN() << "\n";
            return 1;
        }
        board.push(move.value());
        std::cout << (i - 5) << ' ' << usi << ' ' << board.toSFEN() << "\n";
    }
    return 0;
}
