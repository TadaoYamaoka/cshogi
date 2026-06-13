#include "../src/cshogi.h"

#include <cstdio>

int main() {
    initTable();
    Position::initZobrist();

    __Board board("8l/1R2S1kgr/3pp2p1/p2nlpp1p/5ns2/2+BPl3P/1PNK1PP2/1GGS2S2/5G1NL b B3P3p 1");
    __DfPn dfpn;

    std::puts("before search");
    std::fflush(stdout);
    const bool result = dfpn.search(board);
    std::printf("after search result=%d\n", result ? 1 : 0);
    std::fflush(stdout);

    std::puts("before get_pv");
    std::fflush(stdout);
    dfpn.get_pv(board);
    std::printf("after get_pv size=%zu\n", dfpn.pv.size());
    std::fflush(stdout);
    return 0;
}
