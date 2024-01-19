#include "pch.h"

#include "../src/cshogi.h"
#include <array>


TEST(TestBoard, to_hcp_issue17) {
    HuffmanCodedPos_init();

    auto board = __Board("lnsgkgsnl/9/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1");
    HuffmanCodedPos hcp;
    board.toHuffmanCodedPos((char*)hcp.data);
    const auto expected = std::array<u8, 32>{ 89, 164, 73, 33, 12, 151, 66, 252, 28, 155, 66, 88, 94, 133, 240, 40, 132, 87, 33, 60, 155, 66, 88, 46, 133, 248, 56, 38, 133, 48, 60, 94 };
    const auto actual = *reinterpret_cast<std::array<u8, 32>*>(hcp.data);
    EXPECT_EQ(expected, actual);

    board.set_hcp((char*)hcp.data);
    EXPECT_EQ("lnsgkgsnl/9/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1", board.toSFEN());
}

TEST(TestBoard, to_psfen_issue17) {
    PackedSfen_init();

    auto board = __Board("lnsgkgsnl/9/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1");
    PackedSfen psfen;
    board.toPackedSfen((char*)psfen.data);
    const auto expected = std::array<u8, 32>{ 89, 164, 81, 34, 12, 171, 68, 252, 44, 167, 68, 56, 94, 137, 240, 72, 132, 87, 34, 60, 167, 68, 56, 86, 137, 248, 88, 70, 137, 48, 188, 126 };
    const auto actual = *reinterpret_cast<std::array<u8, 32>*>(psfen.data);
    EXPECT_EQ(expected, actual);

    board.set_psfen((char*)psfen.data);
    EXPECT_EQ("lnsgkgsnl/9/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL w - 1", board.toSFEN());
}

TEST(TestBoard, set_position_issue48) {
    initTable();

    auto board = __Board();
    EXPECT_THROW(
        board.set_position("sfen sfen"),
        std::runtime_error
    );
    EXPECT_NO_THROW(
        board.set_position("startpos")
    );
    EXPECT_NO_THROW(
        board.set_position("sfen lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1")
    );
    EXPECT_THROW(
        board.set_position("startpos moves abc"),
        std::runtime_error
    );
    EXPECT_NO_THROW(
        board.set_position("startpos moves 2g2f")
    );
}

TEST(TestBoard, mateMove_issue45) {
    initTable();

    auto board = __Board("lr6l/4g4/p3p4/1pp5p/P2S1p3/2P1N2+bP/1PGP1Pk1R/2N3pp1/4K3L w b2g3s2nl4p 152");

    const auto move = board.mateMove(7);
    EXPECT_EQ(Move::moveNone(), Move(move));
}

TEST(TestBoard, mateMove_issue46) {
    initTable();

    auto board = __Board("pk7/9/G8/2LKP4/9/9/9/9/9 b Bn 1");

    auto move = board.mateMove(3);
    EXPECT_EQ(Move::moveNone(), Move(move).toUSI());
}
