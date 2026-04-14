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

    __DfPn dfpn;

    auto board = __Board("8l/1R2S1kgr/3pp2p1/p2nlpp1p/5ns2/2+BPl3P/1PNK1PP2/1GGS2S2/5G1NL b B3P3p 1");

    auto ret = dfpn.search(board);
    EXPECT_TRUE(ret);

    EXPECT_NO_THROW(
        dfpn.get_pv(board)
    );

    /*std::string pvUsi;
    for (size_t i = 0; i < dfpn.pv.size(); i++) {
        if (i > 0) pvUsi += ' ';
        pvUsi += Move(dfpn.pv[i]).toUSI();
    }
    EXPECT_EQ(
        "7f5d 5c5d 5b4a+ B*6b 8b6b+ 3b2a B*4c 2b3b 4c3b+ 1b3b 6b3b 2a3b R*4b 3b3c B*2b 3c2d P*2e 2d2e L*2h R*2f 2h2f 3e2f G*3f 2e2d R*2e",
        pvUsi
    );*/
}

TEST(TestDfPn, mate3) {
    using namespace ns_dfpn;

    initTable();
    Position::initZobrist();

    __DfPn dfpn;

    auto board = __Board("+B+R5n1/5gk2/p1pps1gp1/4ppnsK/6pP1/1PPSP3L/PR1P1PP2/6S2/L2G1G3 w B2N2LP2p 1");

    auto ret = dfpn.search(board);
    EXPECT_TRUE(ret);

    EXPECT_NO_THROW(
        dfpn.get_pv(board)
    );

    std::string pvUsi;
    for (size_t i = 0; i < dfpn.pv.size(); i++) {
        if (i > 0) pvUsi += ' ';
        pvUsi += Move(dfpn.pv[i]).toUSI();
    }
    EXPECT_EQ(
        "2d2e 1d2e 3c2d",
        pvUsi
    );
}

TEST(TestDfPn, mate7) {
    using namespace ns_dfpn;

    initTable();
    Position::initZobrist();

    __DfPn dfpn;

    auto board = __Board("ln1g3+Rl/2sk1s+P2/2ppppb1p/p1b3p2/8P/P4P3/2PPP1P2/1+r2GS3/LN+p2KGNL w GN2Ps 36");

    auto ret = dfpn.search(board);
    EXPECT_TRUE(ret);

    EXPECT_NO_THROW(
        dfpn.get_pv(board)
    );

    std::string pvUsi;
    for (size_t i = 0; i < dfpn.pv.size(); i++) {
        if (i > 0) pvUsi += ' ';
        pvUsi += Move(dfpn.pv[i]).toUSI();
    }
    EXPECT_EQ(
        "8h5h 4i5h S*6i 5h6h 7i7h 6h5i G*5h",
        pvUsi
    );
}

TEST(TestDfPn, mate9) {
    using namespace ns_dfpn;

    initTable();
    Position::initZobrist();

    __DfPn dfpn;

    auto board = __Board("ln6l/4g1G2/2s1pk3/3p1s2p/p1P4b1/2pPPbp1P/PP1S3K1/3g2RP1/4+r2NL w N2Pgsnl3p 98");

    auto ret = dfpn.search(board);
    EXPECT_TRUE(ret);

    EXPECT_NO_THROW(
        dfpn.get_pv(board)
    );

    std::string pvUsi;
    for (size_t i = 0; i < dfpn.pv.size(); i++) {
        if (i > 0) pvUsi += ' ';
        pvUsi += Move(dfpn.pv[i]).toUSI();
    }
    EXPECT_EQ(
        "S*2f 2g2f 4f3e 2f2g G*2f 2g1h L*1g 2i1g 2f1g",
        pvUsi
    );
}

TEST(TestDfPn, mate9_2) {
    using namespace ns_dfpn;

    initTable();
    Position::initZobrist();

    __DfPn dfpn;

    auto board = __Board("ln5kl/2r6/p1ps2+N1p/4p1pg1/3l3p1/P1P1PPP1P/1P1P1G3/2GS2+n2/+b2K1s2L b RGS2Pbn2p 101");

    auto ret = dfpn.search(board);
    EXPECT_TRUE(ret);
}

TEST(TestDfPn, mate11) {
    using namespace ns_dfpn;

    initTable();
    Position::initZobrist();

    __DfPn dfpn;

    auto board = __Board("l6nl/3kPs3/5p2p/p1ppg2P1/7+r1/P1G3p1P/1pNbSP1GL/2+n1S1K2/L1r3P2 w BGS3Pn2p 110");

    auto ret = dfpn.search(board);
    EXPECT_TRUE(ret);

    EXPECT_NO_THROW(
        dfpn.get_pv(board)
    );

    std::string pvUsi;
    for (size_t i = 0; i < dfpn.pv.size(); i++) {
        if (i > 0) pvUsi += ' ';
        pvUsi += Move(dfpn.pv[i]).toUSI();
    }
    EXPECT_EQ(
        "2e2g 3h2g G*3g 2g2f N*3d 2f1e 1c1d 1e2e 2a3c 2e3e 6g4e+",
        pvUsi
    );
}

TEST(TestDfPn, mate11_2) {
    using namespace ns_dfpn;

    initTable();
    Position::initZobrist();

    __DfPn dfpn;

    auto board = __Board("8+S/4S4/k6p1/1pp+bPPS1p/3p5/p1PP2PnP/3g1K3/9/L7L b RB2GSN2L4Prg2n2p 151");

    auto ret = dfpn.search(board);
    EXPECT_TRUE(ret);
}

#ifdef NDEBUG
TEST(TestDfPn, zukou001) {
    using namespace ns_dfpn;

    initTable();
    Position::initZobrist();

    __DfPn dfpn;

    auto board = __Board("1pG1B4/Gs+P6/pP7/n1ls5/3k5/nL4+r1b/1+p1p+R4/1S7/2N5K b SP2gn2l11p 1");

    auto ret = dfpn.search(board);
    EXPECT_TRUE(ret);

    EXPECT_NO_THROW(
        dfpn.get_pv(board)
    );

    // Verify PV length is odd (attacker's last move)
    EXPECT_GT(dfpn.pv.size(), 0u);
    EXPECT_EQ(dfpn.pv.size() % 2, 1u) << "PV length should be odd (attacker moves last)";

    // Verify all moves are legal and replay PV
    std::vector<StateInfo> states(dfpn.pv.size());
    for (size_t i = 0; i < dfpn.pv.size(); i++) {
        const Move move(dfpn.pv[i]);
        EXPECT_TRUE(board.pos.moveIsPseudoLegal(move))
            << "Illegal move at ply " << i << ": " << move.toUSI();
        board.pos.doMove(move, states[i]);
    }

    // After PV, the position should be checkmate (attacker just moved, so it's
    // the defender's turn and they have no legal evasion — i.e. inCheck and no legal moves)
    EXPECT_TRUE(board.pos.inCheck()) << "Final position not in check";
    // Verify no legal evasion exists
    {
        MoveList<LegalAll> ml(board.pos);
        EXPECT_EQ(ml.size(), 0u) << "Defender has legal moves at end of PV";
    }

    // Undo all moves
    for (int i = static_cast<int>(dfpn.pv.size()) - 1; i >= 0; i--) {
        board.pos.undoMove(Move(dfpn.pv[i]));
    }

    // Print PV for reference
    std::string pvUsi;
    for (size_t i = 0; i < dfpn.pv.size(); i++) {
        if (i > 0) pvUsi += ' ';
        pvUsi += Move(dfpn.pv[i]).toUSI();
    }
    printf("zukou001 PV (%zu moves): %s\n", dfpn.pv.size(), pvUsi.c_str());
    printf("zukou001 nodes: %u\n", dfpn.get_searched_node());
}
#endif

TEST(TestDfPn, no_mate) {
    using namespace ns_dfpn;

    initTable();
    Position::initZobrist();

    const std::array<const char*, 10> sfens = {
        "lns3kn1/1r7/4pgs+R1/2pp1p3/pp2P4/2P1SP3/PPSP5/2GBG4/LN1K3N+l b BG3Pl3p 49",
        "lns4n1/1r3k3/4pgsg1/2pp1p3/pp2P4/2P1SP3/PPSP5/2GBG2R1/LN1K3N+l b B3Pl3p 47",
        "7nl/5Psk1/1+P1+P1p1pp/K3g4/6p1B/1SP4P1/PsS3P1P/1N7/+r6NL w GLrb2gnl6p 1",
        "ln3+P1+PK/1rk4+B1/3p1+L1+S1/p1p2p1+B1/3+r3s1/7s1/4p1+n+pp/+p3+n2p+p/1+p3+p+p+p+p b 2GN2L2gsp 1",
        "l2+S1p2K/1B4G2/p4+N1p1/3+B3sk/5P1s1/P1G3p1p/2P1Pr1+n1/9/LNS5L b R2GL8Pnp 1",
        "+B2B1n2K/7+R1/p2p1p1ps/3g2+r1k/1p3n3/4n1P+s1/PP7/1S6p/L7L b 3GS7Pn2l2p 1",
        "l6GK/2p2+R1P1/p1nsp2+Sp/1p1p2s2/2+R2bk2/3P4P/P4+p1g1/2s6/L7L b B2GNL2n7p 1",
        "1n3G1nK/2+r2P3/p3+P1n1p/2p2Gp2/5l3/3P5/P1P3S2/6+Bpg/L1S1L3k b R2SNL5Pbg3p 1",
        "+B2B1n2K/7+R1/p2p1p1ps/3g2+r1k/1p3n3/4n1P+s1/PP7/1S7/L8 b 3GSL7Pn2l3p 1",
        "ln2g3l/2+Rskg3/p2sppL2/2pp1sP1p/2P2n3/B2P1N1p1/P1NKPP2P/1G1S1+p1P1/7+rL b B2Pg 98"
    };

    for (const auto* sfen : sfens) {
        __DfPn dfpn(15, 10000, 32767);
        auto board = __Board(sfen);
        EXPECT_FALSE(dfpn.search(board)) << sfen;
    }
}
