#include "../src/init.hpp"
#include "../src/cshogi.h"
#include <array>
#include <cmath>
#include <iostream>
#include <tuple>

namespace {
int osl_sort_ptype(const PieceType pt) {
    switch (pt) {
    case ProPawn: return 2;
    case ProLance: return 3;
    case ProKnight: return 4;
    case ProSilver: return 5;
    case Horse: return 6;
    case Dragon: return 7;
    case King: return 8;
    case Gold: return 9;
    case Pawn: return 10;
    case Lance: return 11;
    case Knight: return 12;
    case Silver: return 13;
    case Bishop: return 14;
    case Rook: return 15;
    default: return 0;
    }
}

int osl_square_index(const Square square) {
    if (square == SquareNum) {
        return 0;
    }
    const int x = static_cast<int>(makeFile(square)) + 1;
    const int y = static_cast<int>(makeRank(square)) + 1;
    return x * 16 + y + 1;
}

auto key(const Position& pos, const Color turn, const Move move) {
    const int a = pos.attackersTo(turn, move.to()).popCount() + (move.isDrop() ? 1 : 0);
    const int d = pos.attackersTo(oppositeColor(turn), move.to()).popCount();
    const int sign = turn == Black ? 1 : -1;
    const int x = static_cast<int>(makeFile(move.to())) + 1;
    const int y = static_cast<int>(makeRank(move.to())) + 1;
    const int to_y = sign * y;
    const int to_x = (5 - std::abs(5 - x)) * 2 + (x > 5);
    int from_to = (to_y * 16 + to_x) * 256;
    from_to += move.isDrop() ? osl_sort_ptype(move.pieceTypeDropped()) : osl_square_index(move.from());
    return std::make_tuple(a > d, from_to, move.isPromotion(), a, d);
}
}

int main() {
    initTable();
    Position::initZobrist();
    __Board b;
    b.set_position("sfen 1pG6/Gs+P6/pP7/n1lsS4/1k6R/n7b/1N+Bp5/1S7/8K w Pr2gn3l12p 14");
    for (bool sort : { false, true }) {
        std::array<ExtMove, 1024> buf{};
        auto last = generateOslmateEscapeMoves(buf.data(), b.pos, true, sort);
        std::cout << (sort ? "sort" : "raw") << "\n";
        int i = 0;
        for (auto p = buf.data(); p != last; ++p) {
            const auto [support, from_to, promo, a, d] = key(b.pos, b.pos.turn(), p->move);
            std::cout << i++ << ' ' << p->move.toUSI()
                      << " fromType=" << static_cast<int>(p->move.isDrop() ? p->move.pieceTypeDropped() : p->move.pieceTypeFrom())
                      << " key=" << support << ',' << from_to << ',' << promo
                      << " effects=" << a << ',' << d << "\n";
        }
    }
}
