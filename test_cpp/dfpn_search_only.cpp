#include "../src/cshogi.h"

#include <chrono>
#include <cstdio>

int main() {
    initTable();
    Position::initZobrist();

    __Board board("1pG1B4/Gs+P6/pP7/n1ls5/3k5/nL4+r1b/1+p1p+R4/1S7/2N5K b SP2gn2l11p 1");
    DfPn dfpn;

    const auto start = std::chrono::steady_clock::now();
    const bool result = dfpn.dfpn(board.pos);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    std::printf("search result=%d ms=%lld nodes=%u\n", result ? 1 : 0, elapsed, dfpn.searchedNode);
    std::fflush(stdout);
    return result ? 0 : 1;
}
