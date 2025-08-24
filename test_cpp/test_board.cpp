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
    EXPECT_EQ(Move::moveNone(), Move(move));
}

TEST(TestSfen, rotate_sfen) {
    {
        const auto result = __rotate_sfen("lnsgkgsnl/1r5b1/ppppppppp/9/9/7P1/PPPPPPP1P/1B5R1/LNSGKGSNL w - 2");
        EXPECT_EQ("lnsgkgsnl/1r5b1/p1ppppppp/1p7/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 2", result);
    }
    {
        const auto result = __rotate_sfen("lr5nl/3k2g2/4ps1p1/p1pg1pp1p/3s3P1/P1P2PP1P/1PN1P1N2/1KG2G3/L2Rs3L b BNPbs2p 75");
        EXPECT_EQ("l3Sr2l/3g2gk1/2n1p1np1/p1pp2p1p/1p3S3/P1PP1GP1P/1P1SP4/2G2K3/LN5RL w BS2Pbnp 75", result);
    }
    {
        const auto result = __rotate_sfen("l3r3l/2+P6/2n2G1k1/p1p2+B1pp/3p5/PPPn2P1P/2S1P1s2/2G5R/LNK1s3L w NPb2gs5p 118");
        EXPECT_EQ("l3S1knl/r5g2/2S1p1s2/p1p2Nppp/5P3/PP1+b2P1P/1K1g2N2/6+p2/L3R3L b B2GS5Pnp 118", result);
    }
    {
        const auto result = __rotate_sfen("l+S5nl/5+R3/2+Pspn1p1/p4ks1p/2G2g1P1/PP4p1P/1G3+p2B/1K1p5/LN3B1NL w SPrg5p 142");
        EXPECT_EQ("ln1b3nl/5P1k1/b2+P3g1/p1P4pp/1p1G2g2/P1SK4P/1P1NPS+p2/3+r5/LN5+sL b RG5Psp 142", result);
    }
}

TEST(TestDfPn, get_pv_issue56) {
    using namespace ns_dfpn;

    initTable();
    Position::initZobrist();

    __DfPn dfpn(11, 100000, 32767);

    auto board = __Board("8l/1R2S1kgr/3pp2p1/p2nlpp1p/5ns2/2+BPl3P/1PNK1PP2/1GGS2S2/5G1NL b B3P3p 1");

    auto ret = dfpn.search(board);
    EXPECT_TRUE(ret);

    EXPECT_NO_THROW(
        dfpn.get_pv(board)
    );
    EXPECT_EQ(
        (std::vector<u32>{ 1331887, 526729, 417855, 10422, 1974198, 10413, 1973037, 525459, 923301, 526738, 922276, 526611, 922158, 526738, 923437, 526611, 923319, 526738, 924598, 526620, 924462, 527899, 923429, 527762, 10908, 526611, 347684, 526729, 922267 }),
        dfpn.pv
    );
}
