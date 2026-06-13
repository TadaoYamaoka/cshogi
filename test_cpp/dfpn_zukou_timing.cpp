#include "../src/cshogi.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
long long elapsed_ms(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
}

std::string join_pv(const std::vector<u32>& pv) {
    std::string out;
    for (size_t i = 0; i < pv.size(); ++i) {
        if (i != 0) {
            out += ' ';
        }
        out += Move(pv[i]).toUSI();
    }
    return out;
}
}

int main(int argc, char** argv) {
    initTable();
    Position::initZobrist();

    __Board board("1pG1B4/Gs+P6/pP7/n1ls5/3k5/nL4+r1b/1+p1p+R4/1S7/2N5K b SP2gn2l11p 1");
    DfPn dfpn;
    if (argc >= 2) {
        dfpn.set_max_search_node(static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 10)));
    }
    std::vector<u32> pv;

    auto start = std::chrono::steady_clock::now();
    const bool result = dfpn.dfpn(board.pos);
    std::printf("search result=%d ms=%lld nodes=%u\n", result ? 1 : 0, elapsed_ms(start), dfpn.searchedNode);
    if (!result) {
        return 1;
    }

    start = std::chrono::steady_clock::now();
    dfpn.get_pv(board.pos, pv);
    std::printf("get_pv size=%zu ms=%lld pv=%s\n", pv.size(), elapsed_ms(start), join_pv(pv).c_str());

    start = std::chrono::steady_clock::now();
    for (size_t i = 0; i < pv.size(); ++i) {
        const int move = static_cast<int>(pv[i]);
        const bool legal = board.moveIsLegal(move);
        std::printf("ply=%zu legal=%d move=%s ms=%lld\n",
            i, legal ? 1 : 0, Move(move).toUSI().c_str(), elapsed_ms(start));
        if (!legal) {
            return 2;
        }
        board.push(move);
    }
    std::printf("push-all ms=%lld final=%s\n", elapsed_ms(start), board.toSFEN().c_str());

    start = std::chrono::steady_clock::now();
    const bool in_check = board.inCheck();
    std::printf("inCheck=%d ms=%lld\n", in_check ? 1 : 0, elapsed_ms(start));

    start = std::chrono::steady_clock::now();
    const bool over = board.is_game_over();
    std::printf("is_game_over=%d ms=%lld\n", over ? 1 : 0, elapsed_ms(start));
    return over ? 0 : 3;
}
