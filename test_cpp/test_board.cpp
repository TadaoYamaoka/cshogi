#include "pch.h"

#include "../src/cshogi.h"
#include "../src/generateMoves.hpp"
#include <array>
#include <chrono>

namespace {
std::string join_pv_usi(const std::vector<u32>& pv) {
    std::string pv_usi;
    for (size_t i = 0; i < pv.size(); ++i) {
        if (i > 0) {
            pv_usi += ' ';
        }
        pv_usi += Move(pv[i]).toUSI();
    }
    return pv_usi;
}

std::string join_moves_usi(const std::vector<Move>& moves) {
    std::string out;
    for (size_t i = 0; i < moves.size(); ++i) {
        if (i > 0) {
            out += ' ';
        }
        out += moves[i].toUSI();
    }
    return out;
}

int dfpn_effect_count_for_test(const Position& pos, const Color color, const Square sq) {
    return pos.attackersTo(color, sq).popCount();
}

int dfpn_osl_sort_ptype_for_test(const PieceType piece_type) {
    switch (piece_type) {
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

int dfpn_osl_square_index_for_test(const Square square) {
    if (square == SquareNum) {
        return 0;
    }
    const int x = static_cast<int>(makeFile(square)) + 1;
    const int y = static_cast<int>(makeRank(square)) + 1;
    return x * 16 + y + 1;
}

std::tuple<bool, int, bool> dfpn_move_sort_key_for_test(const Position& pos, const Color turn, const Move move) {
    const int attack_support = dfpn_effect_count_for_test(pos, turn, move.to()) + (move.isDrop() ? 1 : 0);
    const int defense_support = dfpn_effect_count_for_test(pos, oppositeColor(turn), move.to());
    const int move_sort_turn_sign = turn == Black ? 1 : -1;
    const int file = static_cast<int>(makeFile(move.to())) + 1;
    const int to_y = move_sort_turn_sign * (static_cast<int>(makeRank(move.to())) + 1);
    const int to_x = (5 - std::abs(5 - file)) * 2 + (file > 5 ? 1 : 0);
    int from_to = (to_y * 16 + to_x) * 256;
    if (move.isDrop()) {
        from_to += dfpn_osl_sort_ptype_for_test(move.pieceTypeDropped());
    }
    else {
        from_to += dfpn_osl_square_index_for_test(move.from());
    }
    return std::make_tuple(attack_support > defense_support, from_to, move.isPromotion());
}

std::vector<Move> sort_like_dfpn_for_test(const Position& pos, std::vector<Move> moves) {
    size_t last_sorted = 0;
    size_t cur = 0;
    PieceType last_piece_type = Occupied;
    for (; cur < moves.size(); ++cur) {
        const PieceType piece_type = moves[cur].isDrop()
            ? Occupied
            : moves[cur].pieceTypeFrom();
        if (moves[cur].isDrop() || piece_type == last_piece_type) {
            continue;
        }
        std::sort(moves.begin() + static_cast<std::ptrdiff_t>(last_sorted), moves.begin() + static_cast<std::ptrdiff_t>(cur),
            [&](const Move lhs, const Move rhs) {
                return dfpn_move_sort_key_for_test(pos, pos.turn(), lhs) > dfpn_move_sort_key_for_test(pos, pos.turn(), rhs);
            });
        last_sorted = cur;
        last_piece_type = piece_type;
    }
    std::sort(moves.begin() + static_cast<std::ptrdiff_t>(last_sorted), moves.begin() + static_cast<std::ptrdiff_t>(cur),
        [&](const Move lhs, const Move rhs) {
            return dfpn_move_sort_key_for_test(pos, pos.turn(), lhs) > dfpn_move_sort_key_for_test(pos, pos.turn(), rhs);
        });
    return moves;
}

std::string format_dfpn_sort_key_for_test(const Position& pos, const Move move) {
    const auto key = dfpn_move_sort_key_for_test(pos, pos.turn(), move);
    std::ostringstream out;
    out << std::get<0>(key) << ',' << std::get<1>(key) << ',' << std::get<2>(key);
    return out.str();
}

template <MoveType MoveTypeValue>
std::vector<Move> generate_check_moves_for_test(const Position& pos) {
    CheckInfo ci(pos);
    std::array<ExtMove, MaxLegalMoves> buffer{};
    ExtMove* last = generateMoves<MoveTypeValue>(buffer.data(), pos);

    std::vector<Move> moves;
    moves.reserve(static_cast<size_t>(last - buffer.data()));
    for (ExtMove* it = buffer.data(); it != last; ++it) {
        if (pos.moveIsLegal(it->move) && pos.moveGivesCheck(it->move, ci)) {
            moves.push_back(it->move);
        }
    }
    return moves;
}

std::vector<Move> generate_oslmate_escape_moves_for_test(const Position& pos, const bool cheap_only, const bool sort_moves = true) {
    std::array<ExtMove, MaxLegalMoves> buffer{};
    ExtMove* last = generateOslmateEscapeMoves(buffer.data(), pos, cheap_only, sort_moves);

    std::vector<Move> moves;
    moves.reserve(static_cast<size_t>(last - buffer.data()));
    for (ExtMove* it = buffer.data(); it != last; ++it) {
        moves.push_back(it->move);
    }
    return moves;
}

std::vector<Move> generate_legal_moves_for_test(const Position& pos) {
    MoveList<LegalAll> moves(pos);
    std::vector<Move> out;
    out.reserve(moves.size());
    for (ExtMove* it = moves.begin(); it != moves.begin() + static_cast<std::ptrdiff_t>(moves.size()); ++it) {
        out.push_back(it->move);
    }
    return out;
}

void expect_checkmate_pv(__Board& board, const std::vector<u32>& pv) {
    ASSERT_FALSE(pv.empty());
    EXPECT_EQ(pv.size() % 2, 1u) << "PV length should be odd (attacker moves last)";

    for (size_t i = 0; i < pv.size(); ++i) {
        const int move = static_cast<int>(pv[i]);
        ASSERT_TRUE(board.moveIsLegal(move))
            << "Illegal move at ply " << i << ": " << Move(move).toUSI();
        board.push(move);
    }

    EXPECT_TRUE(board.inCheck()) << "Final position not in check";
    EXPECT_TRUE(board.is_game_over()) << "Defender still has legal moves at end of PV";

    for (size_t i = 0; i < pv.size(); ++i) {
        board.pop();
    }
}
}


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

    EXPECT_FALSE(dfpn.pv.empty());
    expect_checkmate_pv(board, dfpn.pv);

    std::string pvUsi;
    for (size_t i = 0; i < dfpn.pv.size(); i++) {
        if (i > 0) pvUsi += ' ';
        pvUsi += Move(dfpn.pv[i]).toUSI();
    }
    EXPECT_EQ(
        "7f5d 5c5d 5b4a+ P*7b 8b7b+ B*5b 7b5b 3b2a B*4c 2b3b 4c3b+ 1b3b 5b3b 2a3b R*3a 3b2b B*3c 2b1c 3a1a+ R*1b 1a1b 1c1b R*1a",
        pvUsi
    );
}

TEST(TestDfPn, DISABLED_issue56_incheck_positions) {
    __Board after_7f5d("8l/1R2S1kgr/3pp2p1/p2n+Bpp1p/5ns2/3Pl3P/1PNK1PP2/1GGS2S2/5G1NL w BL3P3p 2");
    EXPECT_TRUE(after_7f5d.inCheck());
    EXPECT_GT(dfpn_effect_count_for_test(after_7f5d.pos, Black, after_7f5d.pos.kingSquare(White)), 0);
    EXPECT_GT(after_7f5d.pos.attackersToExceptKing(Black, after_7f5d.pos.kingSquare(White)).popCount(), 0);

    __Board after_5b4ap("5+S2l/1R4kgr/3p3p1/p2nppp1p/5ns2/3Pl3P/1PNK1PP2/1GGS2S2/5G1NL w BL3Pb3p 4");
    EXPECT_TRUE(after_5b4ap.inCheck());
    EXPECT_GT(dfpn_effect_count_for_test(after_5b4ap.pos, Black, after_5b4ap.pos.kingSquare(White)), 0);
    EXPECT_GT(after_5b4ap.pos.attackersToExceptKing(Black, after_5b4ap.pos.kingSquare(White)).popCount(), 0);
}

TEST(TestDfPn, DISABLED_dump_issue56_root_pv_node_caps) {
    using namespace ns_dfpn;

    initTable();
    Position::initZobrist();

    constexpr const char* kIssue56Sfen = "8l/1R2S1kgr/3pp2p1/p2nlpp1p/5ns2/2+BPl3P/1PNK1PP2/1GGS2S2/5G1NL b B3P3p 1";
    for (const uint32_t nodes : { 4096u, 16384u, 1048576u }) {
        __DfPn dfpn(31, nodes, 32767);
        __Board board(kIssue56Sfen);

        const auto start = std::chrono::steady_clock::now();
        const bool ret = dfpn.search(board);
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

        std::cout << "nodes=" << nodes
                  << " ret=" << ret
                  << " searched=" << dfpn.get_searched_node()
                  << " elapsed_ms=" << elapsed_ms
                  << " pv=" << join_pv_usi(dfpn.pv)
                  << std::endl;
    }
}

TEST(TestDfPn, DISABLED_trace_issue56_root_16384) {
    using namespace ns_dfpn;

    initTable();
    Position::initZobrist();

    __DfPn dfpn(31, 16384, 32767);
    __Board board("8l/1R2S1kgr/3pp2p1/p2nlpp1p/5ns2/2+BPl3P/1PNK1PP2/1GGS2S2/5G1NL b B3P3p 1");
    const bool ret = dfpn.search(board);

    std::cout << "ret=" << ret
              << " searched=" << dfpn.get_searched_node()
              << " pv=" << join_pv_usi(dfpn.pv)
              << std::endl;
}

TEST(TestDfPn, DISABLED_probe_issue56_child_branches_4096) {
    initTable();
    Position::initZobrist();

    constexpr const char* kIssue56Sfen = "8l/1R2S1kgr/3pp2p1/p2nlpp1p/5ns2/2+BPl3P/1PNK1PP2/1GGS2S2/5G1NL b B3P3p 1";
    const std::array<const char*, 3> branches = {
        "5b4c",
        "5b4c+",
        "5b6c+",
    };

    for (const char* usi : branches) {
        __Board board(kIssue56Sfen);
        const Move move = usiToMove(board.pos, usi);
        ASSERT_TRUE(move) << usi;
        board.push(move.value());

        __DfPn dfpn(31, 4096, 32767);
        const bool ret = dfpn.search_andnode(board);

        std::cout << "branch=" << usi
                  << " ret=" << ret
                  << " searched=" << dfpn.get_searched_node()
                  << std::endl;
    }
}

TEST(TestDfPn, DISABLED_probe_issue56_root_branches_iterative_4096) {
    initTable();
    Position::initZobrist();

    constexpr const char* kIssue56Sfen = "8l/1R2S1kgr/3pp2p1/p2nlpp1p/5ns2/2+BPl3P/1PNK1PP2/1GGS2S2/5G1NL b B3P3p 1";
    const std::array<const char*, 5> branches = {{
        "5b6c+",
        "5b4c+",
        "5b4c",
        "5b4a+",
        "5b5a",
    }};

    for (const char* usi : branches) {
        __Board root(kIssue56Sfen);
        const Move move = usiToMove(root.pos, usi);
        ASSERT_TRUE(move) << usi;
        const std::vector<Move> history{ move };

        __DfPn dfpn(2000, 4096, 32767);
        const bool ret = dfpn.search_with_history(root, history);

        std::cout << "branch=" << usi
                  << " ret=" << ret
                  << " searched=" << dfpn.get_searched_node()
                  << std::endl;
    }
}

TEST(TestDfPn, DISABLED_probe_issue56_root_branch_5b6c_iterative_4096) {
    initTable();
    Position::initZobrist();

    constexpr const char* kIssue56Sfen = "8l/1R2S1kgr/3pp2p1/p2nlpp1p/5ns2/2+BPl3P/1PNK1PP2/1GGS2S2/5G1NL b B3P3p 1";

    __Board root(kIssue56Sfen);
    const Move move = usiToMove(root.pos, "5b6c+");
    ASSERT_TRUE(move);

    const std::vector<Move> history{ move };
    __DfPn dfpn(2000, 4096, 32767);
    const bool ret = dfpn.search_with_history(root, history);

    std::cout << "branch=5b6c+ ret=" << ret
              << " searched=" << dfpn.get_searched_node()
              << std::endl;
}

TEST(TestDfPn, DISABLED_probe_issue56_root_branch_5b4ap_iterative_4096) {
    initTable();
    Position::initZobrist();

    constexpr const char* kIssue56Sfen = "8l/1R2S1kgr/3pp2p1/p2nlpp1p/5ns2/2+BPl3P/1PNK1PP2/1GGS2S2/5G1NL b B3P3p 1";

    __Board root(kIssue56Sfen);
    const Move move = usiToMove(root.pos, "5b4a+");
    ASSERT_TRUE(move);

    const std::vector<Move> history{ move };
    __DfPn dfpn(2000, 4096, 32767);
    const bool ret = dfpn.search_with_history(root, history);

    std::cout << "branch=5b4a+ ret=" << ret
              << " searched=" << dfpn.get_searched_node()
              << std::endl;
}

TEST(TestDfPn, DISABLED_dump_issue56_d2_false_positive_sfens) {
    initTable();
    Position::initZobrist();

    constexpr const char* kIssue56Sfen = "8l/1R2S1kgr/3pp2p1/p2nlpp1p/5ns2/2+BPl3P/1PNK1PP2/1GGS2S2/5G1NL b B3P3p 1";
    const std::array<std::pair<const char*, const char*>, 2> lines = {{
        { "5b6c+", "3b4c" },
        { "5b6c+", "3b3c" },
    }};

    for (const auto& [attack_usi, defense_usi] : lines) {
        __Board board(kIssue56Sfen);
        const Move attack = usiToMove(board.pos, attack_usi);
        ASSERT_TRUE(attack) << attack_usi;
        board.push(attack.value());

        const Move defense = usiToMove(board.pos, defense_usi);
        ASSERT_TRUE(defense) << defense_usi;
        board.push(defense.value());

        std::cout << attack_usi << ' ' << defense_usi << ' ' << board.toSFEN() << std::endl;
    }
}

TEST(TestDfPn, DISABLED_dump_issue56_root_5b4ap_3b4c_8b4bp_child) {
    initTable();
    Position::initZobrist();

    constexpr const char* kIssue56Sfen = "8l/1R2S1kgr/3pp2p1/p2nlpp1p/5ns2/2+BPl3P/1PNK1PP2/1GGS2S2/5G1NL b B3P3p 1";

    __Board board(kIssue56Sfen);
    for (const char* usi : { "5b4a+", "3b4c" }) {
        const Move move = usiToMove(board.pos, usi);
        ASSERT_TRUE(move) << usi;
        board.push(move.value());
    }

    std::cout << "after_5b4ap_3b4c sfen=" << board.toSFEN() << std::endl;
    std::cout << "checks=" << join_moves_usi(generate_check_moves_for_test<CheckAllOslmate>(board.pos)) << std::endl;

    const Move mate1 = usiToMove(board.pos, "8b4b+");
    ASSERT_TRUE(mate1);
    EXPECT_TRUE(board.pos.moveGivesCheck(mate1));
    board.push(mate1.value());

    const std::vector<Move> cheap = generate_oslmate_escape_moves_for_test(board.pos, true, false);
    const std::vector<Move> full = generate_oslmate_escape_moves_for_test(board.pos, false, false);
    const std::vector<Move> sorted = generate_oslmate_escape_moves_for_test(board.pos, false, true);
    const std::vector<Move> legal = generate_legal_moves_for_test(board.pos);

    std::cout << "after_8b4bp sfen=" << board.toSFEN() << std::endl;
    std::cout << "cheap=" << join_moves_usi(cheap) << std::endl;
    std::cout << "full=" << join_moves_usi(full) << std::endl;
    std::cout << "sorted=" << join_moves_usi(sorted) << std::endl;
    std::cout << "legal=" << join_moves_usi(legal) << std::endl;
}

TEST(TestDfPn, DISABLED_dump_issue56_root_attack_sfens) {
    initTable();
    Position::initZobrist();

    constexpr const char* kIssue56Sfen = "8l/1R2S1kgr/3pp2p1/p2nlpp1p/5ns2/2+BPl3P/1PNK1PP2/1GGS2S2/5G1NL b B3P3p 1";
    const std::array<const char*, 6> attacks = {{
        "5b6c+",
        "5b4c+",
        "5b4c",
        "5b4a+",
        "5b4a",
        "5b5a",
    }};

    for (const char* attack_usi : attacks) {
        __Board board(kIssue56Sfen);
        const Move attack = usiToMove(board.pos, attack_usi);
        ASSERT_TRUE(attack) << attack_usi;
        board.push(attack.value());
        std::cout << attack_usi << ' ' << board.toSFEN() << std::endl;
    }
}

TEST(TestDfPn, DISABLED_dump_issue56_check_generation_subtree) {
    initTable();
    Position::initZobrist();

    const __Board root_board("8l/1R2S1kgr/3pp2p1/p2nlpp1p/5ns2/2+BPl3P/1PNK1PP2/1GGS2S2/5G1NL b B3P3p 1");
    const std::vector<Move> root_legacy_moves = generate_check_moves_for_test<CheckAll>(root_board.pos);
    const std::vector<Move> root_osl_moves = generate_check_moves_for_test<CheckAllOslmate>(root_board.pos);

    const __Board board("8l/1R3Bkgr/3Sp2p1/p2nlpp1p/5ns2/2+BPl3P/1PNK1PP2/1GGS2S2/5G1NL b 4P3p 5");

    const std::vector<Move> legacy_moves = generate_check_moves_for_test<CheckAll>(board.pos);
    const std::vector<Move> osl_moves = generate_check_moves_for_test<CheckAllOslmate>(board.pos);

    std::cout << "root legacy=" << join_moves_usi(root_legacy_moves) << std::endl;
    std::cout << "root osl=" << join_moves_usi(root_osl_moves) << std::endl;
    std::cout << "legacy=" << join_moves_usi(legacy_moves) << std::endl;
    std::cout << "osl=" << join_moves_usi(osl_moves) << std::endl;
}

TEST(TestDfPn, DISABLED_dump_issue56_deep_attack_check_generation) {
    initTable();
    Position::initZobrist();

    const __Board board("5+S2l/6k2/3p3p1/p2nppp1p/5ns2/3Pl3P/1PNK1PP2/1GGS2S2/5G1NL b RBGL4Prb2p 15");

    const std::vector<Move> legacy_moves = generate_check_moves_for_test<CheckAll>(board.pos);
    const std::vector<Move> osl_moves = generate_check_moves_for_test<CheckAllOslmate>(board.pos);
    const std::vector<Move> dfpn_sorted_osl_moves = sort_like_dfpn_for_test(board.pos, osl_moves);

    std::cout << "deep legacy=" << join_moves_usi(legacy_moves) << std::endl;
    std::cout << "deep osl=" << join_moves_usi(osl_moves) << std::endl;
    std::cout << "deep dfpn-sort(osl)=" << join_moves_usi(dfpn_sorted_osl_moves) << std::endl;

    for (const Move move : dfpn_sorted_osl_moves) {
        if (move.toUSI() == "R*3a" || move.toUSI() == "R*4b") {
            std::cout << move.toUSI() << " key=" << format_dfpn_sort_key_for_test(board.pos, move) << std::endl;
        }
    }
}

TEST(TestDfPn, DISABLED_trace_issue56_check_generation_direct_roots) {
    using namespace ns_dfpn;

    initTable();
    Position::initZobrist();

    const std::array<const char*, 4> sfens = {{
        "6k1l/1R5gr/3pp+S1p1/p2nlpp1p/5ns2/2+BPl3P/1PNK1PP2/1GGS2S2/5G1NL b B3P3p 3",
        "7kl/1R5gr/3pp+S1p1/p2nlpp1p/5ns2/2+BPl3P/1PNK1PP2/1GGS2S2/5G1NL b B3P3p 3",
        "6k1l/1R5gr/3p1+S1p1/p2nppp1p/5ns2/3Pl3P/1PNK1PP2/1GGS2S2/5G1NL b BL3Pb3p 1",
        "8l/1R5gr/3p1k1p1/p2nppp1p/5ns2/3Pl3P/1PNK1PP2/1GGS2S2/5G1NL b BL3Pbs3p 5",
    }};

    for (const char* sfen : sfens) {
        __Board board(sfen);
        __DfPn dfpn(31, 128, 32767);
        const bool ret = dfpn.search(board);
        std::cout << "sfen=" << sfen
                  << " ret=" << ret
                  << " searched=" << dfpn.get_searched_node()
                  << " pv=" << join_pv_usi(dfpn.pv)
                  << std::endl;
    }
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

    expect_checkmate_pv(board, dfpn.pv);
}

TEST(TestDfPn, mate9_sub_2e1f_2g1f) {
    using namespace ns_dfpn;

    initTable();
    Position::initZobrist();

    __DfPn dfpn;

    auto board = __Board("ln6l/4g1G2/2s1pk3/3p1s2p/p1P4b1/2pPPbp1P/PP1S3K1/3g2RP1/4+r2NL w N2Pgsnl3p 98");
    board.push(board.move_from_usi("2e1f"));
    board.push(board.move_from_usi("2g1f"));

    auto ret = dfpn.search(board);
    EXPECT_TRUE(ret);

    EXPECT_NO_THROW(
        dfpn.get_pv(board)
    );

    expect_checkmate_pv(board, dfpn.pv);
}

TEST(TestDfPn, mate9_sub_2e1f_2g1f_G1e_1f2g) {
    using namespace ns_dfpn;

    initTable();
    Position::initZobrist();

    __DfPn dfpn;

    auto board = __Board("ln6l/4g1G2/2s1pk3/3p1s2p/p1P4b1/2pPPbp1P/PP1S3K1/3g2RP1/4+r2NL w N2Pgsnl3p 98");
    board.push(board.move_from_usi("2e1f"));
    board.push(board.move_from_usi("2g1f"));
    board.push(board.move_from_usi("G*1e"));
    board.push(board.move_from_usi("1f2g"));

    auto ret = dfpn.search(board);
    EXPECT_TRUE(ret);
    EXPECT_EQ("P*2f", Move(dfpn.get_move(board)).toUSI());

    EXPECT_NO_THROW(
        dfpn.get_pv(board)
    );

    expect_checkmate_pv(board, dfpn.pv);
}

TEST(TestDfPn, mate9_sub_2e1f_2g1f_G1e_1f2g_L2e_2g1h) {
    using namespace ns_dfpn;

    initTable();
    Position::initZobrist();

    __DfPn dfpn;

    auto board = __Board("ln6l/4g1G2/2s1pk3/3p1s2p/p1P4b1/2pPPbp1P/PP1S3K1/3g2RP1/4+r2NL w N2Pgsnl3p 98");
    board.push(board.move_from_usi("2e1f"));
    board.push(board.move_from_usi("2g1f"));
    board.push(board.move_from_usi("G*1e"));
    board.push(board.move_from_usi("1f2g"));
    board.push(board.move_from_usi("L*2e"));
    board.push(board.move_from_usi("2g1h"));

    auto ret = dfpn.search(board);
    EXPECT_TRUE(ret);

    EXPECT_NO_THROW(
        dfpn.get_pv(board)
    );

    expect_checkmate_pv(board, dfpn.pv);
}

TEST(TestDfPn, mate9_sub_2e1f_2g1f_G1e_1f2g_S2f_2g1h_L1f_P1g_2f1g_2i1g_N2f_1h2g) {
    using namespace ns_dfpn;

    initTable();
    Position::initZobrist();

    __DfPn dfpn;

    auto board = __Board("ln6l/4g1G2/2s1pk3/3p1s2p/p1P5g/2pPPbpnl/PP1S3KN/3g2RP1/4+r3L w BSNP5p 110");

    auto ret = dfpn.search(board);
    EXPECT_TRUE(ret);

    EXPECT_NO_THROW(
        dfpn.get_pv(board)
    );

    EXPECT_EQ("5i5g", Move(dfpn.get_move(board)).toUSI());
    expect_checkmate_pv(board, dfpn.pv);
}

TEST(TestDfPn, mate9_sub_2e1f_2g1f_G1e_1f2g_P2f_2g1h) {
    using namespace ns_dfpn;

    initTable();
    Position::initZobrist();

    __DfPn dfpn;

    auto board = __Board("ln6l/4g1G2/2s1pk3/3p1s2p/p1P5g/2pPPbpp1/PP1S5/3g2RPK/4+r2NL w BN2Psnl3p 104");

    auto ret = dfpn.search(board);
    EXPECT_TRUE(ret);

    EXPECT_NO_THROW(
        dfpn.get_pv(board)
    );

    EXPECT_EQ("L*1f", Move(dfpn.get_move(board)).toUSI());
    expect_checkmate_pv(board, dfpn.pv);
}

TEST(TestDfPn, mate9_sub_2e1f_2g1f_G1e_1f2g_S2f_2g1h_L1f_P1g) {
    using namespace ns_dfpn;

    initTable();
    Position::initZobrist();

    __DfPn dfpn;

    auto board = __Board("ln6l/4g1G2/2s1pk3/3p1s2p/p1P5g/2pPPbpsl/PP1S4P/3g2RPK/4+r2NL w BNPn4p 106");

    auto ret = dfpn.search(board);
    EXPECT_TRUE(ret);

    EXPECT_NO_THROW(
        dfpn.get_pv(board)
    );

    EXPECT_EQ("2f1g+", Move(dfpn.get_move(board)).toUSI());
    expect_checkmate_pv(board, dfpn.pv);
}

TEST(TestDfPn, mate9_2) {
    using namespace ns_dfpn;

    initTable();
    Position::initZobrist();

    __DfPn dfpn;

    auto board = __Board("ln5kl/2r6/p1ps2+N1p/4p1pg1/3l3p1/P1P1PPP1P/1P1P1G3/2GS2+n2/+b2K1s2L b RGS2Pbn2p 101");

    auto ret = dfpn.search(board);
    EXPECT_TRUE(ret);

    expect_checkmate_pv(board, dfpn.pv);
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
        "2e2g 3h2g G*3g 2g2f N*3d 2f3e 6g4e+ 3e2e 2a3c 2e1e 1c1d",
        pvUsi
    );
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

    expect_checkmate_pv(board, dfpn.pv);

    // Print PV for reference
    std::string pvUsi = join_pv_usi(dfpn.pv);
    printf("zukou001 PV (%zu moves): %s\n", dfpn.pv.size(), pvUsi.c_str());
    printf("zukou001 nodes: %u\n", dfpn.get_searched_node());
}
#endif

TEST(TestDfPn, no_mate) {
    using namespace ns_dfpn;

    initTable();
    Position::initZobrist();

    const std::array<const char*, 1> sfens = {
        "lns3kn1/1r7/4pgs+R1/2pp1p3/pp2P4/2P1SP3/PPSP5/2GBG4/LN1K3N+l b BG3Pl3p 49",
        //"lns4n1/1r3k3/4pgsg1/2pp1p3/pp2P4/2P1SP3/PPSP5/2GBG2R1/LN1K3N+l b B3Pl3p 47",
        //"7nl/5Psk1/1+P1+P1p1pp/K3g4/6p1B/1SP4P1/PsS3P1P/1N7/+r6NL w GLrb2gnl6p 1",
        //"ln3+P1+PK/1rk4+B1/3p1+L1+S1/p1p2p1+B1/3+r3s1/7s1/4p1+n+pp/+p3+n2p+p/1+p3+p+p+p+p b 2GN2L2gsp 1",
        //"l2+S1p2K/1B4G2/p4+N1p1/3+B3sk/5P1s1/P1G3p1p/2P1Pr1+n1/9/LNS5L b R2GL8Pnp 1",
        //"+B2B1n2K/7+R1/p2p1p1ps/3g2+r1k/1p3n3/4n1P+s1/PP7/1S6p/L7L b 3GS7Pn2l2p 1",
        //"l6GK/2p2+R1P1/p1nsp2+Sp/1p1p2s2/2+R2bk2/3P4P/P4+p1g1/2s6/L7L b B2GNL2n7p 1",
        //"1n3G1nK/2+r2P3/p3+P1n1p/2p2Gp2/5l3/3P5/P1P3S2/6+Bpg/L1S1L3k b R2SNL5Pbg3p 1",
        //"+B2B1n2K/7+R1/p2p1p1ps/3g2+r1k/1p3n3/4n1P+s1/PP7/1S7/L8 b 3GSL7Pn2l3p 1",
        //"ln2g3l/2+Rskg3/p2sppL2/2pp1sP1p/2P2n3/B2P1N1p1/P1NKPP2P/1G1S1+p1P1/7+rL b B2Pg 98"
    };

    for (const auto* sfen : sfens) {
        __DfPn dfpn(15, 10000, 32767);
        auto board = __Board(sfen);
        EXPECT_FALSE(dfpn.search(board)) << sfen;
    }
}
