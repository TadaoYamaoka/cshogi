#include "pch.h"

#include "../src/cshogi.h"
#include <array>

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

void expect_mate_move_in_1(const std::string& sfen, const std::string& usi) {
    auto board = __Board(sfen);
    const int expected = board.move_from_usi(usi);
    const int actual = board.mateMoveIn1Ply();
    const Move actualMove(actual);

    ASSERT_EQ(expected, actual) << "Expected " << usi;
    ASSERT_TRUE(board.moveIsLegal(actual));
    if (!actualMove.isDrop()) {
        EXPECT_EQ(board.piece(actualMove.from()),
            colorAndPieceTypeToPiece(static_cast<Color>(board.turn()), actualMove.pieceTypeFrom()));
    }

    board.push(actual);
    EXPECT_TRUE(board.inCheck());
    EXPECT_TRUE(board.is_game_over());
}

void expect_any_mate_move_in_1(const std::string& sfen) {
    auto board = __Board(sfen);
    const int move = board.mateMoveIn1Ply();

    ASSERT_NE(Move::moveNone().value(), move);
    ASSERT_TRUE(board.moveIsLegal(move));
    if (!Move(move).isDrop()) {
        EXPECT_EQ(board.piece(Move(move).from()),
            colorAndPieceTypeToPiece(static_cast<Color>(board.turn()), Move(move).pieceTypeFrom()));
    }

    board.push(move);
    EXPECT_TRUE(board.inCheck());
    EXPECT_TRUE(board.is_game_over());
}

template <MoveType MT>
std::vector<std::string> generated_move_usi(const Position& pos) {
    std::vector<std::string> moves;
    for (MoveList<MT> moveList(pos); !moveList.end(); ++moveList) {
        moves.push_back(moveList.move().toUSI());
    }
    std::sort(moves.begin(), moves.end());
    return moves;
}

void expect_check_all_matches_legal(const std::string& sfen) {
    for (const std::string& currentSfen : { sfen, __rotate_sfen(sfen) }) {
        const __Board board(currentSfen);
        const CheckInfo ci(board.pos);
        std::array<ExtMove, MaxLegalMoves> defaultMoves;
        std::array<ExtMove, MaxLegalMoves> cachedMoves;
        ExtMove* defaultLast = generateMoves<CheckAll>(defaultMoves.data(), board.pos);
        ExtMove* cachedLast = generateCheckAllMoves(cachedMoves.data(), board.pos, ci);
        const size_t defaultCount = static_cast<size_t>(defaultLast - defaultMoves.data());
        const size_t cachedCount = static_cast<size_t>(cachedLast - cachedMoves.data());
        ASSERT_EQ(defaultCount, cachedCount) << currentSfen;
        for (size_t i = 0; i < defaultCount; ++i) {
            EXPECT_EQ(defaultMoves[i].move.value(), cachedMoves[i].move.value())
                << "index " << i << " in " << currentSfen;
        }

        std::vector<u32> expected;
        for (MoveList<LegalAll> legalMoves(board.pos); !legalMoves.end(); ++legalMoves) {
            if (board.pos.moveGivesCheck(legalMoves.move(), ci)) {
                expected.push_back(legalMoves.move().value());
            }
        }
        std::vector<u32> actual;
        for (size_t i = 0; i < defaultCount; ++i) {
            actual.push_back(defaultMoves[i].move.value());
        }
        std::sort(expected.begin(), expected.end());
        std::sort(actual.begin(), actual.end());
        EXPECT_EQ(expected, actual) << currentSfen;
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

TEST(TestBoard, check_generation_with_check_info_matches_default) {
    initTable();

    __Board board;
    for (size_t ply = 0; ply < 200; ++ply) {
        std::array<ExtMove, MaxLegalMoves> defaultMoves;
        std::array<ExtMove, MaxLegalMoves> cachedMoves;
        ExtMove* defaultLast = generateMoves<CheckAll>(defaultMoves.data(), board.pos);
        const CheckInfo ci(board.pos);
        ExtMove* cachedLast = generateCheckAllMoves(cachedMoves.data(), board.pos, ci);

        const size_t defaultCount = static_cast<size_t>(defaultLast - defaultMoves.data());
        const size_t cachedCount = static_cast<size_t>(cachedLast - cachedMoves.data());
        ASSERT_EQ(defaultCount, cachedCount) << board.toSFEN();
        for (size_t i = 0; i < defaultCount; ++i) {
            EXPECT_EQ(defaultMoves[i].move, cachedMoves[i].move)
                << "index " << i << " in " << board.toSFEN();
        }

        if (!board.pos.inCheck()) {
            std::vector<u32> expectedChecks;
            std::vector<u32> actualChecks;
            for (MoveList<LegalAll> legalMoves(board.pos); !legalMoves.end(); ++legalMoves) {
                if (board.pos.moveGivesCheck(legalMoves.move(), ci)) {
                    expectedChecks.push_back(legalMoves.move().value());
                }
            }
            for (size_t i = 0; i < defaultCount; ++i) {
                actualChecks.push_back(defaultMoves[i].move.value());
            }
            std::sort(expectedChecks.begin(), expectedChecks.end());
            std::sort(actualChecks.begin(), actualChecks.end());
            EXPECT_EQ(expectedChecks, actualChecks) << board.toSFEN();
        }

        MoveList<LegalAll> legalMoves(board.pos);
        if (legalMoves.size() == 0) {
            break;
        }
        const size_t selected = (ply * 17 + 5) % legalMoves.size();
        for (size_t i = 0; i < selected; ++i) {
            ++legalMoves;
        }
        board.push(legalMoves.move().value());
    }
}

TEST(TestBoard, pawn_drop_check_boundaries) {
    initTable();

    const std::string available = "4k4/9/9/9/9/9/9/9/4K4 b P 1";
    EXPECT_EQ(std::vector<std::string>{ "P*5b" }, generated_move_usi<CheckAll>(__Board(available).pos));
    EXPECT_EQ(std::vector<std::string>{ "P*5h" }, generated_move_usi<CheckAll>(__Board(__rotate_sfen(available)).pos));

    const std::string sameFilePawn = "4k4/9/9/9/9/9/4P4/9/4K4 b P 1";
    const std::string otherFilePawn = "4k4/9/9/9/9/9/5P3/9/4K4 b P 1";
    const std::string sameFileProPawn = "4k4/9/9/9/9/9/4+P4/9/4K4 b P 1";
    const std::string occupied = "4k4/4s4/9/9/9/9/9/9/4K4 b P 1";
    const std::string pawnDropMate = "4k4/9/3LGL3/9/9/9/9/9/4K4 b P 1";
    const std::string edgeRank = "8K/9/9/9/9/9/9/9/4k4 b P 1";

    EXPECT_EQ(std::vector<std::string>{}, generated_move_usi<CheckAll>(__Board(sameFilePawn).pos));
    EXPECT_EQ(std::vector<std::string>{ "P*5b" }, generated_move_usi<CheckAll>(__Board(otherFilePawn).pos));
    EXPECT_EQ(std::vector<std::string>{ "P*5b" }, generated_move_usi<CheckAll>(__Board(sameFileProPawn).pos));
    EXPECT_EQ(std::vector<std::string>{}, generated_move_usi<CheckAll>(__Board(occupied).pos));
    const auto pawnDropMateChecks = generated_move_usi<CheckAll>(__Board(pawnDropMate).pos);
    EXPECT_EQ(pawnDropMateChecks.end(),
        std::find(pawnDropMateChecks.begin(), pawnDropMateChecks.end(), "P*5b"));
    EXPECT_EQ(std::vector<std::string>{}, generated_move_usi<CheckAll>(__Board(edgeRank).pos));

    for (const std::string& sfen : {
        available, sameFilePawn, otherFilePawn, sameFileProPawn, occupied, pawnDropMate, edgeRank }) {
        expect_check_all_matches_legal(sfen);
    }
}

TEST(TestBoard, pawn_move_check_boundaries) {
    initTable();

    const std::string sameFile = "9/4k4/9/4P4/9/9/9/9/4K4 b - 1";
    const auto sameFileCheck = generated_move_usi<Check>(__Board(sameFile).pos);
    const auto sameFileAll = generated_move_usi<CheckAll>(__Board(sameFile).pos);
    EXPECT_NE(sameFileCheck.end(), std::find(sameFileCheck.begin(), sameFileCheck.end(), "5d5c+"));
    EXPECT_EQ(sameFileCheck.end(), std::find(sameFileCheck.begin(), sameFileCheck.end(), "5d5c"));
    EXPECT_NE(sameFileAll.end(), std::find(sameFileAll.begin(), sameFileAll.end(), "5d5c+"));
    EXPECT_NE(sameFileAll.end(), std::find(sameFileAll.begin(), sameFileAll.end(), "5d5c"));

    const std::string differentFile = "5k3/4P4/9/9/9/9/9/9/4K4 b - 1";
    const auto differentFileChecks = generated_move_usi<CheckAll>(__Board(differentFile).pos);
    EXPECT_NE(differentFileChecks.end(),
        std::find(differentFileChecks.begin(), differentFileChecks.end(), "5b5a+"));
    EXPECT_EQ(differentFileChecks.end(),
        std::find(differentFileChecks.begin(), differentFileChecks.end(), "5b5a"));

    const std::string blocked = "9/4k4/4N4/4P4/9/9/9/9/4K4 b - 1";
    const auto blockedChecks = generated_move_usi<CheckAll>(__Board(blocked).pos);
    EXPECT_EQ(blockedChecks.end(), std::find(blockedChecks.begin(), blockedChecks.end(), "5d5c+"));
    EXPECT_EQ(blockedChecks.end(), std::find(blockedChecks.begin(), blockedChecks.end(), "5d5c"));

    const std::string capture = "9/4k4/4s4/4P4/9/9/9/9/4K4 b - 1";
    const __Board captureBoard(capture);
    const Move expectedCapture(captureBoard.move_from_usi("5d5c+"));
    bool foundExpectedCapture = false;
    for (MoveList<CheckAll> checks(captureBoard.pos); !checks.end(); ++checks) {
        foundExpectedCapture |= checks.move().value() == expectedCapture.value();
    }
    EXPECT_TRUE(foundExpectedCapture);

    for (const std::string& sfen : { sameFile, differentFile, blocked, capture }) {
        expect_check_all_matches_legal(sfen);
    }
}

TEST(TestBoard, check_and_check_all_promotion_boundaries) {
    initTable();

    const std::string bishopPromotionOnly = "9/4k4/9/5B3/9/9/9/9/4K4 b - 1";
    const std::string bishopDirect = "4k4/9/9/5B3/9/9/9/9/4K4 b - 1";
    const std::string bishopLeavingPromotionZone = "9/9/6B2/4k4/9/9/9/9/4K4 b - 1";
    const std::string rookPromotionOnly = "9/4k4/9/5R3/9/9/9/9/4K4 b - 1";
    const std::string rookDirect = "4k4/9/5R3/9/9/9/9/9/4K4 b - 1";

    const auto bishopPromotionOnlyCheck = generated_move_usi<Check>(__Board(bishopPromotionOnly).pos);
    EXPECT_NE(bishopPromotionOnlyCheck.end(),
        std::find(bishopPromotionOnlyCheck.begin(), bishopPromotionOnlyCheck.end(), "4d5c+"));
    EXPECT_EQ(bishopPromotionOnlyCheck.end(),
        std::find(bishopPromotionOnlyCheck.begin(), bishopPromotionOnlyCheck.end(), "4d5c"));

    const auto bishopDirectCheck = generated_move_usi<Check>(__Board(bishopDirect).pos);
    const auto bishopDirectAll = generated_move_usi<CheckAll>(__Board(bishopDirect).pos);
    EXPECT_NE(bishopDirectCheck.end(), std::find(bishopDirectCheck.begin(), bishopDirectCheck.end(), "4d3c+"));
    EXPECT_EQ(bishopDirectCheck.end(), std::find(bishopDirectCheck.begin(), bishopDirectCheck.end(), "4d3c"));
    EXPECT_NE(bishopDirectAll.end(), std::find(bishopDirectAll.begin(), bishopDirectAll.end(), "4d3c+"));
    EXPECT_NE(bishopDirectAll.end(), std::find(bishopDirectAll.begin(), bishopDirectAll.end(), "4d3c"));

    for (const auto& [sfen, promoted, unpromoted] : {
        std::tuple{ bishopLeavingPromotionZone, "3c4d+", "3c4d" },
        std::tuple{ __rotate_sfen(bishopLeavingPromotionZone), "7g6f+", "7g6f" } }) {
        const auto checks = generated_move_usi<Check>(__Board(sfen).pos);
        const auto allChecks = generated_move_usi<CheckAll>(__Board(sfen).pos);
        EXPECT_NE(checks.end(), std::find(checks.begin(), checks.end(), promoted));
        EXPECT_EQ(checks.end(), std::find(checks.begin(), checks.end(), unpromoted));
        EXPECT_NE(allChecks.end(), std::find(allChecks.begin(), allChecks.end(), promoted));
        EXPECT_EQ(allChecks.end(), std::find(allChecks.begin(), allChecks.end(), unpromoted));
    }

    const auto rookPromotionOnlyCheck = generated_move_usi<Check>(__Board(rookPromotionOnly).pos);
    EXPECT_NE(rookPromotionOnlyCheck.end(),
        std::find(rookPromotionOnlyCheck.begin(), rookPromotionOnlyCheck.end(), "4d4c+"));
    EXPECT_EQ(rookPromotionOnlyCheck.end(),
        std::find(rookPromotionOnlyCheck.begin(), rookPromotionOnlyCheck.end(), "4d4c"));

    const auto rookDirectCheck = generated_move_usi<Check>(__Board(rookDirect).pos);
    const auto rookDirectAll = generated_move_usi<CheckAll>(__Board(rookDirect).pos);
    EXPECT_NE(rookDirectCheck.end(), std::find(rookDirectCheck.begin(), rookDirectCheck.end(), "4c5c+"));
    EXPECT_EQ(rookDirectCheck.end(), std::find(rookDirectCheck.begin(), rookDirectCheck.end(), "4c5c"));
    EXPECT_NE(rookDirectAll.end(), std::find(rookDirectAll.begin(), rookDirectAll.end(), "4c5c+"));
    EXPECT_NE(rookDirectAll.end(), std::find(rookDirectAll.begin(), rookDirectAll.end(), "4c5c"));

    for (const auto& [sfen, promoted, unpromoted] : {
        std::tuple{ bishopPromotionOnly, "6f5g+", "6f5g" },
        std::tuple{ bishopDirect, "6f7g+", "6f7g" },
        std::tuple{ rookPromotionOnly, "6f6g+", "6f6g" },
        std::tuple{ rookDirect, "6g5g+", "6g5g" } }) {
        const auto checks = generated_move_usi<Check>(__Board(__rotate_sfen(sfen)).pos);
        EXPECT_NE(checks.end(), std::find(checks.begin(), checks.end(), promoted));
        EXPECT_EQ(checks.end(), std::find(checks.begin(), checks.end(), unpromoted));
    }

    for (const std::string& sfen : {
        bishopPromotionOnly, bishopDirect, bishopLeavingPromotionZone,
        rookPromotionOnly, rookDirect }) {
        expect_check_all_matches_legal(sfen);
    }

    const std::string silverLeavingPromotionZone = "9/9/6S2/4k4/9/9/9/9/4K4 b - 1";
    for (const auto& [sfen, promoted, unpromoted] : {
        std::tuple{ silverLeavingPromotionZone, "3c4d+", "3c4d" },
        std::tuple{ __rotate_sfen(silverLeavingPromotionZone), "7g6f+", "7g6f" } }) {
        const auto checks = generated_move_usi<Check>(__Board(sfen).pos);
        EXPECT_NE(checks.end(), std::find(checks.begin(), checks.end(), promoted));
        EXPECT_EQ(checks.end(), std::find(checks.begin(), checks.end(), unpromoted));
    }
    expect_check_all_matches_legal(silverLeavingPromotionZone);
}

TEST(TestBoard, check_legality_filter_boundaries) {
    initTable();

    const std::string pinnedRook = "4rk3/9/9/9/9/9/4R4/9/4K4 b - 1";
    const auto pinnedChecks = generated_move_usi<CheckAll>(__Board(pinnedRook).pos);
    EXPECT_NE(pinnedChecks.end(), std::find(pinnedChecks.begin(), pinnedChecks.end(), "5g5a"));
    EXPECT_EQ(pinnedChecks.end(), std::find(pinnedChecks.begin(), pinnedChecks.end(), "5g4g"));

    const std::string discoveredByKing = "4k4/9/4K4/9/9/9/9/9/4R4 b - 1";
    const auto kingChecks = generated_move_usi<CheckAll>(__Board(discoveredByKing).pos);
    EXPECT_NE(kingChecks.end(), std::find(kingChecks.begin(), kingChecks.end(), "5c4c"));
    EXPECT_EQ(kingChecks.end(), std::find(kingChecks.begin(), kingChecks.end(), "5c4b"));

    const std::string dropsOnly = "4k4/9/9/9/9/9/9/9/4K4 b G 1";
    const std::string unpinnedGold = "4k4/9/4G4/9/9/9/9/9/4K4 b - 1";
    for (const std::string& sfen : { pinnedRook, discoveredByKing, dropsOnly, unpinnedGold }) {
        expect_check_all_matches_legal(sfen);
    }
}

TEST(TestBoard, optimized_check_candidate_boundaries) {
    initTable();

    const std::string lancePromotion = "9/9/4k4/9/9/5L3/9/9/4K4 b - 1";
    const auto lanceChecks = generated_move_usi<Check>(__Board(lancePromotion).pos);
    EXPECT_NE(lanceChecks.end(), std::find(lanceChecks.begin(), lanceChecks.end(), "4f4c+"));

    const std::string knightPromotion = "9/4k4/9/9/4N4/9/9/9/4K4 b - 1";
    const auto knightChecks = generated_move_usi<Check>(__Board(knightPromotion).pos);
    EXPECT_NE(knightChecks.end(), std::find(knightChecks.begin(), knightChecks.end(), "5e4c+"));
    EXPECT_NE(knightChecks.end(), std::find(knightChecks.begin(), knightChecks.end(), "5e6c+"));

    const std::string knightNonPromotion = "4k4/9/9/9/4N4/9/9/9/4K4 b - 1";
    const auto knightNonPromotionChecks = generated_move_usi<Check>(__Board(knightNonPromotion).pos);
    EXPECT_NE(knightNonPromotionChecks.end(),
        std::find(knightNonPromotionChecks.begin(), knightNonPromotionChecks.end(), "5e4c"));
    EXPECT_NE(knightNonPromotionChecks.end(),
        std::find(knightNonPromotionChecks.begin(), knightNonPromotionChecks.end(), "5e6c"));
    EXPECT_EQ(knightNonPromotionChecks.end(),
        std::find(knightNonPromotionChecks.begin(), knightNonPromotionChecks.end(), "5e4c+"));

    const std::string silverCannotPromote = "9/9/4k4/9/4S4/9/9/9/4K4 b - 1";
    const auto silverChecks = generated_move_usi<Check>(__Board(silverCannotPromote).pos);
    EXPECT_NE(silverChecks.end(), std::find(silverChecks.begin(), silverChecks.end(), "5e4d"));
    EXPECT_EQ(silverChecks.end(), std::find(silverChecks.begin(), silverChecks.end(), "5e4d+"));

    const std::string discoveredHorse = "4k4/9/4+B4/9/9/9/9/9/4R3K b - 1";
    const auto horseChecks = generated_move_usi<CheckAll>(__Board(discoveredHorse).pos);
    EXPECT_NE(horseChecks.end(), std::find(horseChecks.begin(), horseChecks.end(), "5c5b"));

    const std::string discoveredDragon = "4k4/9/6+R2/9/8B/9/9/9/K8 b - 1";
    const auto dragonChecks = generated_move_usi<CheckAll>(__Board(discoveredDragon).pos);
    EXPECT_NE(dragonChecks.end(), std::find(dragonChecks.begin(), dragonChecks.end(), "3c4b"));

    for (const std::string& sfen : {
        lancePromotion, knightPromotion, knightNonPromotion, silverCannotPromote,
        discoveredHorse, discoveredDragon }) {
        expect_check_all_matches_legal(sfen);
    }
}

TEST(TestBoard, copy_relinks_stateinfo_history) {
    initTable();

    auto board = __Board();
    board.push(board.move_from_usi("7g7f"));

    auto copied = __Board(board);
    EXPECT_FALSE(copied.inCheck());

    board.set("4k4/4r4/9/9/9/9/9/9/4K4 b - 1");
    EXPECT_TRUE(board.inCheck());

    copied.pop();

    const auto expected = __Board();
    EXPECT_EQ(expected.toSFEN(), copied.toSFEN());
    EXPECT_FALSE(copied.inCheck());
}

TEST(TestBoard, copy_assignment_relinks_stateinfo_history) {
    initTable();

    auto board = __Board();
    board.push(board.move_from_usi("7g7f"));

    auto copied = __Board();
    copied = __Board(board);

    board.set("4k4/4r4/9/9/9/9/9/9/4K4 b - 1");
    copied.pop();

    const auto expected = __Board();
    EXPECT_EQ(expected.toSFEN(), copied.toSFEN());
    EXPECT_FALSE(copied.inCheck());
}

TEST(TestBoard, copy_relinks_mixed_move_and_pass_history) {
    initTable();

    auto board = __Board();
    board.push(board.move_from_usi("7g7f"));
    board.push(board.move_from_usi("3c3d"));
    board.push_pass();

    auto copied = __Board(board);
    board.set("4k4/4r4/9/9/9/9/9/9/4K4 b - 1");

    auto expected = __Board();
    expected.push(expected.move_from_usi("7g7f"));
    expected.push(expected.move_from_usi("3c3d"));

    copied.pop_pass();
    EXPECT_EQ(expected.toSFEN(), copied.toSFEN());
    EXPECT_EQ(expected.inCheck(), copied.inCheck());

    copied.pop();
    expected.pop();
    EXPECT_EQ(expected.toSFEN(), copied.toSFEN());
    EXPECT_EQ(expected.inCheck(), copied.inCheck());

    copied.pop();
    expected.pop();
    EXPECT_EQ(expected.toSFEN(), copied.toSFEN());
    EXPECT_EQ(expected.inCheck(), copied.inCheck());
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

TEST(TestBoard, mateMoveIn1Ply_additional_preserves_gold_like_piece_type) {
    initTable();

    const std::array<const char*, 5> goldLikePieces = { "G", "+P", "+L", "+N", "+S" };
    for (const char* goldLikePiece : goldLikePieces) {
        const std::string blackSfen = "9/9/9/9/9/9/4K4/9/4k1" + std::string(goldLikePiece) + "1R b - 1";
        expect_mate_move_in_1(blackSfen, "3i3h");

        const std::string whiteSfen = __rotate_sfen(blackSfen);
        expect_any_mate_move_in_1(whiteSfen);
    }
}

TEST(TestBoard, mateMoveIn1Ply_normal_gold_like_double_check) {
    initTable();

    const std::array<const char*, 5> goldLikePieces = { "G", "+P", "+L", "+N", "+S" };
    for (const char* goldLikePiece : goldLikePieces) {
        const std::string blackSfen = "9/3p1p3/3pkp3/3p2G2/4"
            + std::string(goldLikePiece) + "4/9/4R4/9/K8 b - 1";
        auto board = __Board(blackSfen);
        const Move expected(board.move_from_usi("5e4d"));
        const Move actual = board.pos.mateMoveIn1Ply<false>();

        ASSERT_EQ(expected, actual);
        ASSERT_TRUE(board.moveIsLegal(actual.value()));
        board.push(actual.value());
        EXPECT_EQ(2, board.pos.checkersBB().popCount());
        EXPECT_TRUE(board.is_game_over());

        auto alternativeBoard = __Board(blackSfen);
        const int alternative = alternativeBoard.move_from_usi("5e5d");
        ASSERT_TRUE(alternativeBoard.moveIsLegal(alternative));
        alternativeBoard.push(alternative);
        EXPECT_TRUE(alternativeBoard.inCheck());
        EXPECT_FALSE(alternativeBoard.is_game_over());
        EXPECT_TRUE(alternativeBoard.moveIsLegal(alternativeBoard.move_from_usi("5c5b")));
    }
}

TEST(TestBoard, mateMoveIn1Ply_discovered_check_support_revealed_by_move) {
    initTable();

    const std::string blackSfen = "9/9/9/3p1pP2/3pk4/3pBp3/3BRP3/9/K8 b - 1";
    auto board = __Board(blackSfen);
    const Move expected(board.move_from_usi("5f4e"));
    const Bitboard from = setMaskBB(expected.from());
    const Bitboard occupiedAfterMove = (board.pos.occupiedBB() ^ from) | setMaskBB(expected.to());

    EXPECT_FALSE(board.pos.attackersTo(Black, expected.to()) & ~from);
    EXPECT_TRUE(board.pos.attackersTo(Black, expected.to(), occupiedAfterMove) & ~from);

    expect_mate_move_in_1(blackSfen, "5f4e");
    expect_any_mate_move_in_1(__rotate_sfen(blackSfen));
}

TEST(TestBoard, mateMoveIn1Ply_discovered_check_allows_non_pawn_interposition) {
    initTable();

    auto board = __Board("9/9/9/3p1pP2/3pk4/3pBp3/3BRP3/9/K8 b g 1");
    const int candidate = board.move_from_usi("5f4e");

    ASSERT_TRUE(board.moveIsLegal(candidate));
    EXPECT_NE(candidate, board.pos.mateMoveIn1Ply<false>().value());
    EXPECT_NE(candidate, board.mateMoveIn1Ply());

    board.push(candidate);
    EXPECT_TRUE(board.inCheck());
    EXPECT_FALSE(board.is_game_over());
    EXPECT_TRUE(board.moveIsLegal(board.move_from_usi("G*5f")));
}

TEST(TestBoard, mateMoveIn1Ply_additional_rejects_last_rank_pawn_interposition) {
    initTable();

    const std::string blackSfen = "9/9/9/9/9/9/4K4/9/4k4 b Rp 1";
    expect_mate_move_in_1(blackSfen, "R*3i");
    expect_any_mate_move_in_1(__rotate_sfen(blackSfen));
}

TEST(TestBoard, mateMoveIn1Ply_additional_pawn_capture_requires_defender_hand_pawn) {
    initTable();

    const std::string blackSfen = "9/9/9/9/9/4K4/6p2/4k1G1R/R8 b - 1";
    expect_mate_move_in_1(blackSfen, "3h3g");
    expect_mate_move_in_1(__rotate_sfen(blackSfen), "7b7c");

    auto withPawnInHand = __Board("9/9/9/9/9/4K4/6p2/4k1G1R/R8 b p 1");
    const int capture = withPawnInHand.move_from_usi("3h3g");
    ASSERT_TRUE(withPawnInHand.moveIsLegal(capture));
    EXPECT_NE(capture, withPawnInHand.mateMoveIn1Ply());
    withPawnInHand.push(capture);
    EXPECT_TRUE(withPawnInHand.inCheck());
    EXPECT_FALSE(withPawnInHand.is_game_over());
    EXPECT_TRUE(withPawnInHand.moveIsLegal(withPawnInHand.move_from_usi("P*3h")));
}

TEST(TestBoard, mateMoveIn1Ply_additional_pawn_capture_on_defender_last_rank) {
    initTable();

    const std::string blackSfen = "9/9/9/9/9/9/4K4/6p2/4k1G1R b p 1";
    expect_mate_move_in_1(blackSfen, "3i3h");
    expect_mate_move_in_1(__rotate_sfen(blackSfen), "7a7b");
}

TEST(TestBoard, mateMoveIn1Ply_silver_double_check) {
    initTable();

    const std::string blackSfen = "9/9/9/9/9/9/4K4/9/4k1S1R b - 1";
    expect_mate_move_in_1(blackSfen, "3i4h");
    expect_mate_move_in_1(__rotate_sfen(blackSfen), "7a6b");
}

TEST(TestBoard, mateMoveIn1Ply_knight_double_check) {
    initTable();

    const std::string nonPromotionSfen = "9/9/2G1k1G2/9/2GG2G2/9/4N4/9/K3R4 b - 1";
    expect_mate_move_in_1(nonPromotionSfen, "5g4e");
    expect_mate_move_in_1(__rotate_sfen(nonPromotionSfen), "5c6e");

    const std::string promotionSfen = "2K6/4k4/6G2/2G1N4/9/9/9/9/4R4 b - 1";
    expect_mate_move_in_1(promotionSfen, "5d4b+");
    expect_mate_move_in_1(__rotate_sfen(promotionSfen), "5f6h+");
}

#ifdef CSHOGI_ENABLE_EXHAUSTIVE_MATE_TESTS
TEST(TestBoard, AllMate5Positions) {
    // 詰将棋500万問の5手詰め局面集
    // https://yaneuraou.yaneu.com/2020/12/25/christmas-present/
    constexpr const char* Mate5Path = R"(E:\game\shogi\mate3_5_7_9_11\mate5.sfen)";
    constexpr size_t ExpectedPositionCount = 998824;

    std::ifstream input(Mate5Path);
    ASSERT_TRUE(input) << Mate5Path;

    initTable();
    Position::initZobrist();

    __Board board;
    std::string sfen;
    size_t positionCount = 0;
    size_t failureCount = 0;
    std::string firstFailure;
    std::chrono::steady_clock::duration totalMateSearchTime{};

    while (std::getline(input, sfen)) {
        if (sfen.empty())
            continue;

        ++positionCount;
        board.set(sfen);
        const auto start = std::chrono::steady_clock::now();
        const auto move = board.mateMove(5);
        totalMateSearchTime += std::chrono::steady_clock::now() - start;
        if (move == 0) {
            ++failureCount;
            if (firstFailure.empty())
                firstFailure = sfen;
        }
    }

    std::cout << "mateMove(5) total: "
        << std::chrono::duration_cast<std::chrono::milliseconds>(totalMateSearchTime).count()
        << " ms" << std::endl;
    EXPECT_EQ(ExpectedPositionCount, positionCount);
    EXPECT_EQ(0u, failureCount) << "first failed position: " << firstFailure;
}

TEST(TestBoard, AllNoMate5Positions) {
    // 詰将棋500万問の7手詰め局面集
    // https://yaneuraou.yaneu.com/2020/12/25/christmas-present/
    // 12局面は5手で詰む局面のため除外
    constexpr const char* Mate5Path = R"(E:\game\shogi\mate3_5_7_9_11\mate7_filtered.sfen)";
    constexpr size_t ExpectedPositionCount = 999059;

    std::ifstream input(Mate5Path);
    ASSERT_TRUE(input) << Mate5Path;

    initTable();
    Position::initZobrist();

    __Board board;
    std::string sfen;
    size_t positionCount = 0;
    size_t failureCount = 0;
    std::string firstFailure;
    std::chrono::steady_clock::duration totalMateSearchTime{};

    while (std::getline(input, sfen)) {
        if (sfen.empty())
            continue;

        ++positionCount;
        board.set(sfen);
        const auto start = std::chrono::steady_clock::now();
        const auto move = board.mateMove(5);
        totalMateSearchTime += std::chrono::steady_clock::now() - start;
        if (move != 0) {
            ++failureCount;
            if (firstFailure.empty())
                firstFailure = sfen;
        }
    }

    std::cout << "mateMove(5) total: "
        << std::chrono::duration_cast<std::chrono::milliseconds>(totalMateSearchTime).count()
        << " ms" << std::endl;
    EXPECT_EQ(ExpectedPositionCount, positionCount);
    EXPECT_EQ(0u, failureCount) << "first failed position: " << firstFailure;
}
#endif

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

    __OslDfPn dfpn;

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

TEST(TestDfPn, mate3) {
    using namespace ns_dfpn;

    initTable();
    Position::initZobrist();

    __OslDfPn dfpn;

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

    __OslDfPn dfpn;

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

    __OslDfPn dfpn;

    auto board = __Board("ln6l/4g1G2/2s1pk3/3p1s2p/p1P4b1/2pPPbp1P/PP1S3K1/3g2RP1/4+r2NL w N2Pgsnl3p 98");

    auto ret = dfpn.search(board);
    EXPECT_TRUE(ret);

    EXPECT_NO_THROW(
        dfpn.get_pv(board)
    );

    expect_checkmate_pv(board, dfpn.pv);
}

TEST(TestDfPn, mate9_2) {
    using namespace ns_dfpn;

    initTable();
    Position::initZobrist();

    __OslDfPn dfpn;

    auto board = __Board("ln5kl/2r6/p1ps2+N1p/4p1pg1/3l3p1/P1P1PPP1P/1P1P1G3/2GS2+n2/+b2K1s2L b RGS2Pbn2p 101");

    auto ret = dfpn.search(board);
    EXPECT_TRUE(ret);

    expect_checkmate_pv(board, dfpn.pv);
}

TEST(TestDfPn, mate11) {
    using namespace ns_dfpn;

    initTable();
    Position::initZobrist();

    __OslDfPn dfpn;

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

TEST(TestDfPn, no_king) {
    using namespace ns_dfpn;

    initTable();
    Position::initZobrist();

    __DfPn dfpn;

    auto board = __Board("4k4/9/3P1P3/9/9/9/9/9/9 b GS 1");

    auto ret = dfpn.search(board);
    EXPECT_TRUE(ret);

    expect_checkmate_pv(board, dfpn.pv);
}

TEST(TestDfPn, osl_no_king) {
    using namespace ns_dfpn;

    initTable();
    Position::initZobrist();

    __OslDfPn dfpn;

    auto board = __Board("4k4/9/3P1P3/9/9/9/9/9/9 b GS 1");

    auto ret = dfpn.search(board);
    EXPECT_TRUE(ret);

    expect_checkmate_pv(board, dfpn.pv);
}

#ifdef NDEBUG
TEST(TestDfPn, zukou001) {
    using namespace ns_dfpn;

    initTable();
    Position::initZobrist();

    __OslDfPn dfpn;

    auto board = __Board("1pG1B4/Gs+P6/pP7/n1ls5/3k5/nL4+r1b/1+p1p+R4/1S7/2N5K b SP2gn2l11p 1");

    auto ret = dfpn.search(board);
    EXPECT_TRUE(ret);

    EXPECT_NO_THROW(
        dfpn.get_pv(board)
    );

    expect_checkmate_pv(board, dfpn.pv);

    EXPECT_EQ(3143631u, dfpn.get_searched_node());
    EXPECT_EQ(
        "S*5d 6e7e 7i8g 7e8f 5g6f 3f6f 5a9e+ 8f7f P*7g 6f7g 9e7g 7f8e R*1e R*2e 1e2e 1f2e 7g9e 8e7f R*2f R*3f 2f3f 2e3f 9e7g 7f8e R*3e R*4e 3e4e 3f4e 7g9e 8e7f R*4f L*5f 9e7g 7f8e 4f4e L*5e B*7f 7d7f 7g9e 8e7d 9e9f B*8e N*6f 7d8c 9b8b 8a8b 4e4c P*7c 4c7c+ 6d7c 7b7c 8c7c 9f9e L*8d P*7d 7c6b 9e8d 6b7a S*7b 7a7b 7d7c+ 7b8a 7c8b 8a8b L*8c 8b9b S*8a 9b9a 8d7c P*8b 8c8b+",
        join_pv_usi(dfpn.pv)
    );

}

TEST(TestDfPn, zukou001_no_king) {
    using namespace ns_dfpn;

    initTable();
    Position::initZobrist();

    __OslDfPn dfpn;

    auto board = __Board("1pG1B4/Gs+P6/pP7/n1ls5/3k5/nL4+r1b/1+p1p+R4/1S7/2N6 b SP2gn2l11p 1");

    auto ret = dfpn.search(board);
    EXPECT_TRUE(ret);

    EXPECT_NO_THROW(
        dfpn.get_pv(board)
    );

    expect_checkmate_pv(board, dfpn.pv);

    EXPECT_EQ(3713302u, dfpn.get_searched_node());
    EXPECT_EQ(
        "S*5d 6e7e 7i8g 7e8f 5g6f 3f6f 5a9e+ 8f7f P*7g 6f7g 9e7g 7f8e R*1e R*2e 1e2e 1f2e 7g9e 8e7f R*2f R*3f 2f3f 2e3f 9e7g 7f8e R*3e R*4e 3e4e 3f4e 7g9e 8e7f R*4f L*5f 9e7g 7f8e 4f4e L*5e B*7f 7d7f 7g9e 8e7d 9e9f B*8e N*6f 7d8c 9b8b 8a8b 4e4c P*7c 7b7c 6d7c 4c7c+ 8c7c 9f9e L*8d P*7d 7c6b 9e8d 6b7a S*7b 7a7b 7d7c+ 7b8a 7c8b 8a8b L*8c 8b9b S*8a 9b9a 8d7c P*8b 8c8b+",
        join_pv_usi(dfpn.pv)
    );

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
        __OslDfPn dfpn(15, 10000, 32767);
        auto board = __Board(sfen);
        EXPECT_FALSE(dfpn.search(board)) << sfen;
    }
}
