#include "../src/cshogi.h"
#include "../src/init.hpp"

#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: codex_cshogi_escape_list <sfen>\n";
        return 1;
    }

    initTable();
    Position::initZobrist();

    __Board board;
    board.set_position(argv[1]);

    DfPn dfpn;
    const auto records = dfpn.debug_fixed_escapes(board.pos);
    std::cout << "sfen=" << board.toSFEN() << "\n";
    for (std::size_t i = 0; i < records.size(); ++i) {
        std::cout << i << ' ' << records[i].escape_move.toUSI() << "\n";
    }
    return 0;
}
