#include "pch.h"

#include "../src/generateMoves.hpp"
#include "../src/position.hpp"
#include <algorithm>
#include <array>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct CheckCase {
    const char* name;
    const char* viewpoint;
    const char* sfen;
    const char* expected_usi;
    // 生成する派生ケース。空白区切りで O, M, R, RM を指定する。
    // O=基準、M=左右反転、R=180度回転・先後反転、RM=両方。例: "O R"。
    const char* variants;
};

// Base cases from all_checks.json. The other orientations are generated below.
constexpr CheckCase all_check_cases[] = {
#include "all_check_cases.inc"
};

// Base cases from check_evasions.json.
constexpr CheckCase check_evasion_cases[] = {
#include "check_evasion_cases.inc"
};

static_assert(std::size(all_check_cases) == 860);
static_assert(std::size(check_evasion_cases) == 6);

char swap_piece_color(const char piece) {
    if (piece >= 'A' && piece <= 'Z') return piece - 'A' + 'a';
    if (piece >= 'a' && piece <= 'z') return piece - 'a' + 'A';
    return piece;
}

std::string transformed_sfen(const char* original, const bool mirror, const bool rotate) {
    if (!mirror && !rotate) return original;

    std::istringstream input(original);
    std::string board, turn, hand, ply;
    input >> board >> turn >> hand >> ply;

    std::array<std::array<std::string, 9>, 9> squares{};
    size_t rank = 0;
    size_t file = 0;
    for (size_t i = 0; i < board.size(); ++i) {
        const char c = board[i];
        if (c == '/') {
            ++rank;
            file = 0;
        }
        else if (c >= '1' && c <= '9') {
            file += c - '0';
        }
        else {
            std::string piece;
            if (c == '+') piece += board[++i];
            else piece += c;
            if (rotate) piece.back() = swap_piece_color(piece.back());
            squares[rotate ? 8 - rank : rank][mirror != rotate ? 8 - file : file] =
                (c == '+' ? "+" : "") + piece;
            ++file;
        }
    }

    std::string transformed_board;
    for (const auto& row : squares) {
        if (!transformed_board.empty()) transformed_board += '/';
        unsigned empty = 0;
        for (const std::string& piece : row) {
            if (piece.empty()) {
                ++empty;
            }
            else {
                if (empty) transformed_board += std::to_string(empty);
                empty = 0;
                transformed_board += piece;
            }
        }
        if (empty) transformed_board += std::to_string(empty);
    }

    if (rotate && hand != "-") {
        std::string black_hand, white_hand;
        for (size_t i = 0; i < hand.size();) {
            std::string token;
            while (i < hand.size() && hand[i] >= '0' && hand[i] <= '9') {
                token += hand[i++];
            }
            const char piece = swap_piece_color(hand[i++]);
            token += piece;
            (piece >= 'A' && piece <= 'Z' ? black_hand : white_hand) += token;
        }
        hand = black_hand + white_hand;
    }

    if (rotate) turn = turn == "b" ? "w" : "b";
    return transformed_board + " " + turn + " " + hand + " " + ply;
}

std::string transformed_usi(std::string usi, const bool mirror, const bool rotate) {
    const auto transform_square = [&](const size_t index) {
        if (mirror != rotate) usi[index] = '0' + (10 - (usi[index] - '0'));
        if (rotate) usi[index + 1] = 'a' + ('i' - usi[index + 1]);
    };
    if (usi.size() > 1 && usi[1] == '*') transform_square(2);
    else {
        transform_square(0);
        transform_square(2);
    }
    return usi;
}

enum class CheckVariant {
    Original,
    Mirror,
    Rotate,
    RotateMirror,
};

CheckVariant parse_variant(const std::string& variant) {
    if (variant == "O") return CheckVariant::Original;
    if (variant == "M") return CheckVariant::Mirror;
    if (variant == "R") return CheckVariant::Rotate;
    if (variant == "RM") return CheckVariant::RotateMirror;
    throw std::runtime_error("unknown check test variant: " + variant);
}

std::string case_name(const CheckCase& test_case, const CheckVariant variant) {
    switch (variant) {
    case CheckVariant::Original: return std::string(test_case.name) + "-O";
    case CheckVariant::Mirror: return std::string(test_case.name) + "-M";
    case CheckVariant::Rotate: return std::string(test_case.name) + "-R";
    case CheckVariant::RotateMirror: return std::string(test_case.name) + "-RM";
    }
    throw std::runtime_error("unknown check test variant");
}

std::string case_viewpoint(const CheckCase& test_case, const CheckVariant variant) {
    const std::string base(test_case.viewpoint);
    const bool white = std::string(test_case.sfen).find(" w ") != std::string::npos;
    switch (variant) {
    case CheckVariant::Original:
        return base + (white ? "（後手番・基準局面）" : "（先手番・基準局面）");
    case CheckVariant::Mirror:
        return base + (white ? "（後手番・左右反転）" : "（先手番・左右反転）");
    case CheckVariant::Rotate:
        return base + "（後手番・180度回転・先後反転）";
    case CheckVariant::RotateMirror:
        return base + "（後手番・180度回転・先後反転・左右反転）";
    }
    throw std::runtime_error("unknown check test variant");
}

std::vector<std::string> expected_moves(const CheckCase& test_case,
    const bool mirror, const bool rotate) {
    std::istringstream input(test_case.expected_usi);
    std::vector<std::string> result;
    for (std::string usi; input >> usi;) {
        result.push_back(transformed_usi(usi, mirror, rotate));
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::string> generated_moves(const Position& pos,
    const ExtMove* first, const ExtMove* last, const bool require_evasion) {
    std::vector<std::string> result;
    for (const ExtMove* move = first; move != last; ++move) {
        // CheckMovePicker applies this filter after generating checks in an in-check position.
        if (!require_evasion || pos.checkMoveIsEvasion(move->move)) {
            result.push_back(move->move.toUSI());
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

void check_cases(const CheckCase* first, const CheckCase* last,
    const bool expect_in_check) {
    size_t case_count = 0;
    for (const CheckCase* test_case = first; test_case != last; ++test_case) {
        std::istringstream variants(test_case->variants);
        for (std::string variant_name; variants >> variant_name;) {
            const CheckVariant variant = parse_variant(variant_name);
            ++case_count;
            const bool mirror = variant == CheckVariant::Mirror
                || variant == CheckVariant::RotateMirror;
            const bool rotate = variant == CheckVariant::Rotate
                || variant == CheckVariant::RotateMirror;
            const std::string sfen = transformed_sfen(test_case->sfen, mirror, rotate);
            SCOPED_TRACE(case_name(*test_case, variant) + " | "
                + case_viewpoint(*test_case, variant) + " | " + sfen);

            const Position pos(sfen);
            ASSERT_EQ(expect_in_check, pos.inCheck());
            const auto expected = expected_moves(*test_case, mirror, rotate);

            std::array<ExtMove, MaxLegalMoves> direct_buffer;
            const ExtMove* direct_last = generateMoves<CheckAll>(direct_buffer.data(), pos);
            {
                SCOPED_TRACE("generateMoves<CheckAll>");
                EXPECT_EQ(expected, generated_moves(pos, direct_buffer.data(), direct_last,
                    expect_in_check));
            }

            const CheckInfo info(pos);
            std::array<ExtMove, MaxLegalMoves> cached_buffer;
            const ExtMove* cached_last =
                generateCheckAllMoves(cached_buffer.data(), pos, info);
            SCOPED_TRACE("generateCheckAllMoves(CheckInfo)");
            EXPECT_EQ(expected, generated_moves(pos, cached_buffer.data(), cached_last,
                expect_in_check));
        }
    }
    EXPECT_EQ(expect_in_check ? 24u : 1990u, case_count);
}

} // namespace

TEST(TestCheckGeneration, AllChecks) {
    initTable();
    check_cases(std::begin(all_check_cases), std::end(all_check_cases), false);
}

TEST(TestCheckGeneration, CheckEvasions) {
    initTable();
    check_cases(std::begin(check_evasion_cases), std::end(check_evasion_cases), true);
}
