#include "../src/init.hpp"
#include "../src/cshogi.h"
#include "../src/dfpn.h"

#include <iostream>
#include <string>

namespace {
std::string hand_string(const Hand hand) {
    static constexpr std::pair<HandPiece, const char*> pieces[] = {
        { HPawn, "P" }, { HLance, "L" }, { HKnight, "N" }, { HSilver, "S" },
        { HGold, "G" }, { HBishop, "B" }, { HRook, "R" },
    };
    std::string out;
    for (const auto& item : pieces) {
        if (!out.empty()) {
            out += ' ';
        }
        out += item.second;
        out += '=';
        out += std::to_string(hand.numOf(item.first));
    }
    return out;
}
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: codex_fixed_escape_probe <sfen>\n";
        return 2;
    }

    initTable();
    Position::initZobrist();

    __Board board;
    board.set_position(argv[1]);

    DfPn dfpn;
    const auto records = dfpn.debug_fixed_escapes(board.pos);

    std::cout << "sfen=" << board.toSFEN() << "\n";
    std::cout << "moves=" << records.size() << "\n";
    for (size_t i = 0; i < records.size(); ++i) {
        const auto& record = records[i];
        std::cout << "  fixed_escape " << i << ' ' << record.escape_move.toUSI()
                  << " pdp=" << record.proof_disproof.proof << ',' << record.proof_disproof.disproof
                  << " best=" << (record.best_move ? record.best_move.toUSI() : "-")
                  << " proof=" << hand_string(record.proof_pieces)
                  << "\n";
    }
    return 0;
}
