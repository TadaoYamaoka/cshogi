#ifndef SEARCH_HPP
#define SEARCH_HPP

#include "position.hpp"

enum NyugyokuResult {
    NyugyokuNone = 0,
    NyugyokuWin = 1,
    NyugyokuDraw = 2,
};

enum NyugyokuRule {
    LAW_24,
    LAW_27,
};

NyugyokuResult nyugyoku(const Position& pos, const NyugyokuRule rule = LAW_27);

#endif // SEARCH_HPP
