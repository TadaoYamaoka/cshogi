#include "../src/init.hpp"
#include "../src/cshogi.h"

#include <cstdio>
#include <string>

namespace {
std::string join_pv(const std::vector<u32>& pv) {
    std::string result;
    for (size_t i = 0; i < pv.size(); ++i) {
        if (i) {
            result += ' ';
        }
        result += Move(pv[i]).toUSI();
    }
    return result;
}
}

int main() {
    initTable();
    Position::initZobrist();

    __DfPn dfpn;
    __Board board("1pG1B4/Gs+P6/pP7/n1ls5/3k5/nL4+r1b/1+p1p+R4/1S7/2N5K b SP2gn2l11p 1");

    const bool result = dfpn.search(board);
    std::printf("search_result=%d nodes=%u\n", result ? 1 : 0, dfpn.get_searched_node());
    std::printf("pv_after_search=%s\n", join_pv(dfpn.pv).c_str());

    dfpn.get_pv(board);
    std::printf("pv_after_get_pv=%s\n", join_pv(dfpn.pv).c_str());
    std::printf("nodes_after_get_pv=%u\n", dfpn.get_searched_node());
    return result ? 0 : 1;
}
