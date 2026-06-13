#include <iostream>
#include <string>

#include "../src/init.hpp"
#include "../src/cshogi.h"

int main() {
    initTable();
    Position::initZobrist();

    const std::string sfen1 = "sfen 5+S1kl/1R5gr/3p3p1/p2nppp1p/5ns2/3Pl3P/1PNK1PP2/1GGS2S2/5G1NL b BL3Pb3p 1";
    const std::string sfen2 = "sfen 5+S1kl/4+R2gr/3p3p1/p2nppp1p/5ns2/3Pl3P/1PNK1PP2/1GGS2S2/5G1NL b 2BL4P2p 1";

    __Board b1;
    __Board b2;
    b1.set_position(sfen1);
    b2.set_position(sfen2);

    b1.push(b1.move_from_usi("8b3b+"));
    b2.push(b2.move_from_usi("5b3b"));

    std::cout << "p1 " << b1.pos.toSFEN() << "\n";
    std::cout << "p2 " << b2.pos.toSFEN() << "\n";
    std::cout << "board1 " << static_cast<unsigned long long>(b1.pos.getBoardKey()) << "\n";
    std::cout << "board2 " << static_cast<unsigned long long>(b2.pos.getBoardKey()) << "\n";
    std::cout << "key1 " << static_cast<unsigned long long>(b1.pos.getKey()) << "\n";
    std::cout << "key2 " << static_cast<unsigned long long>(b2.pos.getKey()) << "\n";
}
