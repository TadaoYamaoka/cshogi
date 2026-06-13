#include "osl/checkmate/fixedDepthSolverExt.h"
#include "osl/checkmate/proofNumberTable.h"
#include "osl/additionalEffect.h"
#include "osl/hashKey.h"
#include "osl/oslConfig.h"
#include "osl/usi.h"

#include <bitset>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

template <osl::Player P>
osl::Square dir_square(osl::Square target, int dir) {
    switch (dir) {
    case osl::UL:
        return target - osl::DirectionPlayerTraits<osl::UL, P>::offset();
    case osl::U:
        return target - osl::DirectionPlayerTraits<osl::U, P>::offset();
    case osl::UR:
        return target - osl::DirectionPlayerTraits<osl::UR, P>::offset();
    case osl::L:
        return target - osl::DirectionPlayerTraits<osl::L, P>::offset();
    case osl::R:
        return target - osl::DirectionPlayerTraits<osl::R, P>::offset();
    case osl::DL:
        return target - osl::DirectionPlayerTraits<osl::DL, P>::offset();
    case osl::D:
        return target - osl::DirectionPlayerTraits<osl::D, P>::offset();
    case osl::DR:
        return target - osl::DirectionPlayerTraits<osl::DR, P>::offset();
    default:
        return osl::Square::STAND();
    }
}

osl::Square dir_square(osl::Square target, osl::Player attack, int dir) {
    return attack == osl::BLACK ? dir_square<osl::BLACK>(target, dir) : dir_square<osl::WHITE>(target, dir);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: osl_fixed_probe <depth|escape-black-depth> <sfen...>\n";
        return 2;
    }

    osl::OslConfig::setUp();

    const std::string mode = argv[1];
    const bool escape_black = mode.rfind("escape-black-", 0) == 0;
    const int depth = std::atoi(escape_black ? mode.c_str() + 13 : mode.c_str());
    std::string line;
    for (int i = 2; i < argc; ++i) {
        if (!line.empty()) {
            line += ' ';
        }
        line += argv[i];
    }
    if (line.rfind("position ", 0) != 0 && line.rfind("sfen ", 0) == 0) {
        line = "position " + line;
    }

    osl::NumEffectState state;
    osl::usi::parse(line, state);

    const osl::Player attack = state.turn();
    const osl::Player defense = osl::alt(attack);
    const osl::Square target_king = state.kingSquare(defense);
    const osl::checkmate::King8Info raw_info(state.Iking8Info(defense));
    const osl::checkmate::King8Info edge_info =
        osl::checkmate::Edge_Table.resetEdgeFromLiberty(defense, target_king, raw_info);
    const int move_candidate_mask =
        attack == osl::BLACK
            ? edge_info.moveCandidateMask<osl::BLACK>(state)
            : edge_info.moveCandidateMask<osl::WHITE>(state);
    const int move_candidate_count =
        attack == osl::BLACK
            ? edge_info.countMoveCandidate<osl::BLACK>(state)
            : edge_info.countMoveCandidate<osl::WHITE>(state);
    const int drop_scale =
        state.hasPieceOnStand<osl::GOLD>(attack) + state.hasPieceOnStand<osl::SILVER>(attack);
    const osl::checkmate::ProofDisproof est =
        osl::checkmate::Proof_Number_Table.attackEstimation(state, attack, edge_info, target_king);

    osl::checkmate::FixedDepthSolverExt solver(state);
    const osl::HashKey key(state);
    osl::Move best_move;
    osl::PieceStand proof_pieces;
    const osl::checkmate::ProofDisproof pdp =
        escape_black
            ? solver.hasEscapeMove<osl::BLACK>(osl::Move(), depth, proof_pieces)
            : (state.turn() == osl::BLACK
                ? solver.hasCheckmateMove<osl::BLACK>(depth, best_move, proof_pieces)
                : solver.hasCheckmateMove<osl::WHITE>(depth, best_move, proof_pieces));

    std::cout << "pdp=" << pdp.proof() << "," << pdp.disproof()
              << " best=" << (best_move.isValid() ? osl::usi::show(best_move) : "-")
              << " key=" << key.boardKey64()
              << " sfen=" << osl::usi::show(state)
              << "\n";
    std::cout << "raw value=" << raw_info.uint64Value()
              << " move2=" << std::bitset<8>(raw_info.moveCandidate2())
              << " liberty_candidate=" << std::bitset<8>(raw_info.libertyCandidate())
              << " liberty=" << std::bitset<8>(raw_info.liberty())
              << " drop=" << std::bitset<8>(raw_info.dropCandidate())
              << " spaces=" << std::bitset<8>(raw_info.spaces())
              << " moves=" << std::bitset<8>(raw_info.moves())
              << " liberty_count=" << raw_info.libertyCount()
              << "\n";
    std::cout << "edge value=" << edge_info.uint64Value()
              << " move2=" << std::bitset<8>(edge_info.moveCandidate2())
              << " liberty_candidate=" << std::bitset<8>(edge_info.libertyCandidate())
              << " liberty=" << std::bitset<8>(edge_info.liberty())
              << " drop=" << std::bitset<8>(edge_info.dropCandidate())
              << " spaces=" << std::bitset<8>(edge_info.spaces())
              << " moves=" << std::bitset<8>(edge_info.moves())
              << " liberty_count=" << edge_info.libertyCount()
              << " move_mask=" << std::bitset<8>(move_candidate_mask)
              << " move_count=" << move_candidate_count
              << " drop_scale=" << drop_scale
              << " est=" << est.proof() << "," << est.disproof()
              << "\n";
    static const char* dir_names[] = { "UL", "U", "UR", "L", "R", "DL", "D", "DR" };
    for (int dir = osl::UL; dir <= osl::DR; ++dir) {
        const int bit = 1 << dir;
        std::cout << "dir " << dir_names[dir]
                  << " move2=" << ((edge_info.moveCandidate2() & bit) ? 1 : 0)
                  << " counted=" << ((move_candidate_mask & bit) ? 1 : 0)
                  << " sq=";
        const osl::Square sq = dir_square(target_king, attack, dir);
        if (sq.isOnBoard()) {
            const osl::Piece piece = state.pieceAt(sq);
            osl::PieceMask defenders = state.effectSetAt(sq) & state.piecesOnBoard(defense);
            defenders.reset(osl::KingTraits<osl::WHITE>::index);
            defenders.reset(osl::KingTraits<osl::BLACK>::index);
            const osl::PieceMask pinned = state.pin(defense);
            std::cout << sq.x() << sq.y()
                      << " piece=" << osl::usi::show(piece)
                      << " has_effect=" << (state.hasEffectAt(attack, sq) ? 1 : 0)
                      << " effects=" << state.countEffect(attack, sq)
                      << " additional=" << (osl::effect_util::AdditionalEffect::hasEffect(state, sq, attack) ? 1 : 0);
            while (defenders.any()) {
                const int num = defenders.takeOneBit();
                const osl::Piece defender = state.pieceOf(num);
                const bool is_pinned = pinned.test(num);
                int same = 0;
                if (is_pinned) {
                    const osl::Direction d =
                        defense == osl::BLACK
                            ? osl::Board_Table.getShort8<osl::BLACK>(target_king, defender.square())
                            : osl::Board_Table.getShort8<osl::WHITE>(target_king, defender.square());
                    same = d == dir ? 1 : 0;
                }
                std::cout << " def=" << osl::usi::show(defender)
                          << " pinned=" << (is_pinned ? 1 : 0)
                          << " same=" << same;
            }
        } else {
            std::cout << "-";
        }
        std::cout
                  << "\n";
    }
    return 0;
}
