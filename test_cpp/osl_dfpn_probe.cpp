#include "osl/checkmate/dfpn.h"
#include "osl/checkmate/dfpnRecord.h"
#include "osl/checkmate/fixedDepthSolverExt.h"
#include "osl/checkmate/immediateCheckmate.h"
#include "osl/checkmate/libertyEstimator.h"
#include "osl/checkmate/pieceCost.h"
#include "osl/checkmate/proofNumberTable.h"
#include "osl/checkmate/proofTreeDepthDfpn.h"
#include "osl/move_generator/addEffectWithEffect.h"
#include "osl/move_generator/move_action.h"
#include "osl/hashKey.h"
#include "osl/numEffectState.h"
#include "osl/oslConfig.h"
#include "osl/csa.h"
#include "osl/pathEncoding.h"
#include "osl/usi.h"

#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdlib>

namespace {
std::string show_move(osl::Move move) {
    return move.isNormal() ? osl::usi::show(move) : "-";
}

std::string show_square(osl::Square square) {
    if (!square.isValid()) {
        return "-";
    }
    std::string result;
    result += static_cast<char>('0' + square.x());
    result += static_cast<char>('a' + square.y() - 1);
    return result;
}

template <osl::Player P>
bool is_delay_escape_node_like_dfpn(const osl::NumEffectState& state, osl::Square last_to) {
    return last_to != osl::Square()
        && state.hasEffectAt(osl::alt(P), last_to)
        && (state.hasEffectNotBy(osl::alt(P), state.kingPiece(osl::alt(P)), last_to)
            || !state.hasEffectAt(P, last_to));
}

std::string show_stand(const osl::PieceStand& stand) {
    static const osl::Ptype ptypes[] = {
        osl::PAWN, osl::LANCE, osl::KNIGHT, osl::SILVER, osl::GOLD, osl::BISHOP, osl::ROOK
    };
    std::string result;
    for (osl::Ptype ptype : ptypes) {
        const int count = stand.get(ptype);
        if (!result.empty()) {
            result += ' ';
        }
        result += osl::csa::show(ptype);
        result += '=';
        result += std::to_string(count);
    }
    return result;
}

osl::Move dfpn_move_from_usi(const std::string& move_usi, const osl::NumEffectState& state) {
    osl::checkmate::Dfpn::DfpnMoveVector moves;
    bool has_pawn_checkmate = false;
    if (state.inCheck()) {
        if (state.turn() == osl::BLACK) {
            osl::checkmate::Dfpn::generateEscape<osl::WHITE>(state, true, osl::Square(), moves);
        }
        else {
            osl::checkmate::Dfpn::generateEscape<osl::BLACK>(state, true, osl::Square(), moves);
        }
    }
    else if (state.turn() == osl::BLACK) {
        osl::checkmate::Dfpn::generateCheck<osl::BLACK>(state, moves, has_pawn_checkmate);
    }
    else {
        osl::checkmate::Dfpn::generateCheck<osl::WHITE>(state, moves, has_pawn_checkmate);
    }
    for (size_t i = 0; i < moves.size(); ++i) {
        if (osl::usi::show(moves[i]) == move_usi) {
            return moves[i];
        }
    }
    const osl::Move parsed = osl::usi::strToMove(move_usi, state);
    if (!parsed.isNormal()) {
        throw std::runtime_error("failed to parse move: " + move_usi);
    }
    return parsed;
}

void print_record_tail(const osl::checkmate::DfpnRecord& record) {
    std::cout
        << " solved=0x" << std::hex << record.solved
        << " dag=0x" << record.dag_moves
        << std::dec
        << " min_pdp=" << record.min_pdp
        << " need_full=" << static_cast<int>(record.need_full_width)
        << " false_branch=" << static_cast<int>(record.false_branch)
        << " dag_terminal=" << (record.dag_terminal ? 1 : 0);
}

template <osl::Player P>
void generate_fixed_checks(const osl::NumEffectState& state, osl::CheckMoveVector& moves, bool& pawn) {
    osl::move_action::Store store(moves);
    osl::move_generator::AddEffectWithEffect<osl::move_action::Store>::template generate<P, true>(
        state, state.kingSquare<osl::alt(P)>(), store, pawn);
}

template <osl::Player P>
bool probe_blocking_vertical_attack(const osl::NumEffectState& state, osl::Square pos) {
    osl::PieceMask effect = state.effectSetAt(pos)
        & state.effectSetAt(pos + osl::DirectionPlayerTraits<osl::U, P>::offset());
    osl::mask_t mask = effect.getMask(1);
    mask &= (state.piecesOnBoard(P).getMask(1) << 8);
    if ((mask & osl::mask_t::makeDirect(osl::PtypeFuns<osl::LANCE>::indexMask << 8)).none()) {
        mask &= osl::mask_t::makeDirect(osl::PtypeFuns<osl::ROOK>::indexMask << 8);
        while (mask.any()) {
            const int num = mask.takeOneBit() + osl::NumBitmapEffect::longToNumOffset;
            const osl::Square from = state.pieceOf(num).square();
            if (from.isU<P>(pos)) {
                goto found;
            }
        }
        return false;
    found:;
    }
    const osl::Offset offset = osl::DirectionPlayerTraits<osl::U, P>::offset();
    pos += offset;
    const osl::Player altP = osl::alt(P);
    for (int i = 0; i < 3; ++i, pos += offset) {
        const osl::Piece p = state.pieceAt(pos);
        if (p.canMoveOn<altP>()) {
            if (state.countEffect(P, pos) == 1) {
                return true;
            }
            if (!p.isEmpty()) {
                return false;
            }
        }
        else {
            return false;
        }
    }
    return false;
}

template <osl::Player P>
bool probe_blocking_diagonal_attack(const osl::NumEffectState& state, osl::Square pos,
    osl::Square target, osl::checkmate::King8Info canMoveMask) {
    const osl::Player altP = osl::alt(P);
    osl::Square to = target - osl::DirectionPlayerTraits<osl::U, P>::offset();
    if ((canMoveMask.uint64Value() & (0x10000 << osl::U)) == 0) {
        return false;
    }
    osl::PieceMask effect = state.effectSetAt(to) & state.effectSetAt(pos);
    osl::mask_t mask = effect.getMask(1);
    mask &= (state.piecesOnBoard(P).getMask(1) << 8);
    mask &= osl::mask_t::makeDirect(osl::PtypeFuns<osl::BISHOP>::indexMask << 8);
    while (mask.any()) {
        const int num = mask.takeOneBit() + osl::NumBitmapEffect::longToNumOffset;
        const osl::Square from = state.pieceOf(num).square();
        const osl::Offset offset = osl::Board_Table.getShort8OffsetUnsafe(to, from);
        if (to + offset != pos) {
            continue;
        }
        if (state.countEffect(P, to) == 1) {
            return true;
        }
        if (!state.pieceAt(to).isEmpty()) {
            return false;
        }
        const osl::Square pos1 = to - offset;
        const osl::Piece p = state.pieceAt(pos1);
        if (p.canMoveOn<altP>() && state.countEffect(P, pos1) == 1) {
            return true;
        }
    }
    return false;
}

void run_fixed_probe(osl::NumEffectState& state) {
    const osl::Player attacker = state.turn();
    const osl::Player defender = osl::alt(attacker);
    const osl::Square target = state.kingSquare(defender);
    const osl::checkmate::King8Info info = osl::checkmate::King8Info::make(attacker, state);
    const osl::checkmate::King8Info cached_info(state.Iking8Info(defender));
    std::cout << "king8 value=0x" << std::hex << info.uint64Value() << std::dec
        << " cached=0x" << std::hex << cached_info.uint64Value() << std::dec
        << " drop=" << info.dropCandidate()
        << " liberty=" << info.liberty()
        << " liberty_candidate=" << info.libertyCandidate()
        << " move_candidate2=" << info.moveCandidate2()
        << " spaces=" << info.spaces()
        << " moves=" << info.moves()
        << " liberty_count=" << info.libertyCount()
        << "\n";
    if (attacker == osl::BLACK) {
        const osl::Square uur = target - osl::DirectionPlayerTraits<osl::UUR, osl::BLACK>::offset();
        const osl::Square uul = target - osl::DirectionPlayerTraits<osl::UUL, osl::BLACK>::offset();
        std::cout << "knight_pos UUR=" << uur
            << " defender_effect_not_pinned=" << (state.hasEffectByNotPinned(defender, uur) ? 1 : 0)
            << " defender_effect=" << (state.hasEffectAt(defender, uur) ? 1 : 0)
            << " attacker_knight_effect=" << (state.effectSetAt(uur).getMask<osl::KNIGHT>()
                & state.piecesOnBoard(attacker).getMask<osl::KNIGHT>()).any()
            << " block_v=" << (probe_blocking_vertical_attack<osl::BLACK>(state, uur) ? 1 : 0)
            << " block_d=" << (probe_blocking_diagonal_attack<osl::BLACK>(state, uur, target, cached_info) ? 1 : 0)
            << "\n";
        std::cout << "knight_pos UUL=" << uul
            << " defender_effect_not_pinned=" << (state.hasEffectByNotPinned(defender, uul) ? 1 : 0)
            << " defender_effect=" << (state.hasEffectAt(defender, uul) ? 1 : 0)
            << " attacker_knight_effect=" << (state.effectSetAt(uul).getMask<osl::KNIGHT>()
                & state.piecesOnBoard(attacker).getMask<osl::KNIGHT>()).any()
            << " block_v=" << (probe_blocking_vertical_attack<osl::BLACK>(state, uul) ? 1 : 0)
            << " block_d=" << (probe_blocking_diagonal_attack<osl::BLACK>(state, uul, target, cached_info) ? 1 : 0)
            << "\n";
    }
    else {
        const osl::Square uur = target - osl::DirectionPlayerTraits<osl::UUR, osl::WHITE>::offset();
        const osl::Square uul = target - osl::DirectionPlayerTraits<osl::UUL, osl::WHITE>::offset();
        std::cout << "knight_pos UUR=" << uur
            << " defender_effect_not_pinned=" << (state.hasEffectByNotPinned(defender, uur) ? 1 : 0)
            << " defender_effect=" << (state.hasEffectAt(defender, uur) ? 1 : 0)
            << " attacker_knight_effect=" << (state.effectSetAt(uur).getMask<osl::KNIGHT>()
                & state.piecesOnBoard(attacker).getMask<osl::KNIGHT>()).any()
            << " block_v=" << (probe_blocking_vertical_attack<osl::WHITE>(state, uur) ? 1 : 0)
            << " block_d=" << (probe_blocking_diagonal_attack<osl::WHITE>(state, uur, target, cached_info) ? 1 : 0)
            << "\n";
        std::cout << "knight_pos UUL=" << uul
            << " defender_effect_not_pinned=" << (state.hasEffectByNotPinned(defender, uul) ? 1 : 0)
            << " defender_effect=" << (state.hasEffectAt(defender, uul) ? 1 : 0)
            << " attacker_knight_effect=" << (state.effectSetAt(uul).getMask<osl::KNIGHT>()
                & state.piecesOnBoard(attacker).getMask<osl::KNIGHT>()).any()
            << " block_v=" << (probe_blocking_vertical_attack<osl::WHITE>(state, uul) ? 1 : 0)
            << " block_d=" << (probe_blocking_diagonal_attack<osl::WHITE>(state, uul, target, cached_info) ? 1 : 0)
            << "\n";
    }
    osl::Move immediate;
    const bool has_immediate = attacker == osl::BLACK
        ? osl::checkmate::ImmediateCheckmate::hasCheckmateMove<osl::BLACK>(state, immediate)
        : osl::checkmate::ImmediateCheckmate::hasCheckmateMove<osl::WHITE>(state, immediate);
    std::cout << "immediate=" << (has_immediate ? 1 : 0)
        << " best=" << show_move(immediate) << "\n";
    osl::checkmate::FixedDepthSolverExt fixed(state);
    osl::Move best;
    osl::PieceStand proof;
    const osl::checkmate::ProofDisproof pdp =
        fixed.hasCheckmateMoveOfTurn(2, best, proof);
    std::cout << "fixed_result=" << pdp.proof() << ',' << pdp.disproof()
        << " best=" << show_move(best)
        << " proof=[" << show_stand(proof) << "]\n";

    osl::CheckMoveVector moves;
    bool pawn = false;
    if (state.turn() == osl::BLACK) {
        generate_fixed_checks<osl::BLACK>(state, moves, pawn);
    }
    else {
        generate_fixed_checks<osl::WHITE>(state, moves, pawn);
    }
    std::cout << "fixed_children=" << moves.size() << " pawn=" << (pawn ? 1 : 0) << "\n";
    for (size_t i = 0; i < moves.size(); ++i) {
        osl::NumEffectState child_state(state);
        child_state.makeMove(moves[i]);
        osl::checkmate::FixedDepthSolverExt child_fixed(child_state);
        osl::PieceStand child_proof;
        const osl::checkmate::ProofDisproof child = attacker == osl::BLACK
            ? child_fixed.hasEscapeMove<osl::BLACK>(moves[i], 1, child_proof)
            : child_fixed.hasEscapeMove<osl::WHITE>(moves[i], 1, child_proof);
        std::cout << "  fixed_child " << i << ' ' << osl::usi::show(moves[i])
            << " pdp=" << child.proof() << ',' << child.disproof()
            << " proof=[" << show_stand(child_proof) << "]\n";
    }
}

void run_escape_probe(osl::NumEffectState& state) {
    osl::checkmate::Dfpn::DfpnMoveVector moves;
    if (state.turn() == osl::BLACK) {
        osl::checkmate::Dfpn::generateEscape<osl::WHITE>(state, true, osl::Square(), moves);
    }
    else {
        osl::checkmate::Dfpn::generateEscape<osl::BLACK>(state, true, osl::Square(), moves);
    }
    std::cout << "escapes=" << moves.size() << "\n";
    for (size_t i = 0; i < moves.size(); ++i) {
        std::cout << "  escape " << i << ' ' << osl::usi::show(moves[i]) << "\n";
    }
}

void run_fixed_escape_probe(osl::NumEffectState& state) {
    osl::checkmate::Dfpn::DfpnMoveVector moves;
    if (state.turn() == osl::BLACK) {
        osl::checkmate::Dfpn::generateEscape<osl::WHITE>(state, true, osl::Square(), moves);
    }
    else {
        osl::checkmate::Dfpn::generateEscape<osl::BLACK>(state, true, osl::Square(), moves);
    }

    osl::checkmate::FixedDepthSolverExt fixed(state);
    std::cout << "fixed_escapes=" << moves.size() << "\n";
    for (size_t i = 0; i < moves.size(); ++i) {
        osl::Move best;
        osl::PieceStand proof;
        const osl::checkmate::ProofDisproof pdp =
            fixed.hasEscapeByMoveOfTurn(moves[i], 0, best, proof);
        std::cout << "  fixed_escape " << i << ' ' << osl::usi::show(moves[i])
            << " raw=" << moves[i].intValue()
            << " pdp=" << pdp.proof() << ',' << pdp.disproof()
            << " best=" << show_move(best)
            << " proof=[" << show_stand(proof) << "]\n";
    }
}

int probe_attack_proof_cost(osl::Player attacker, const osl::NumEffectState& state, osl::Move move) {
    int proof = 0;
    if (!move.isCapture()) {
        const osl::Square from = move.from();
        const osl::Square to = move.to();
        const int attack = state.countEffect(attacker, to) + (from.isPieceStand() ? 1 : 0);
        const int defense = state.countEffect(osl::alt(attacker), to);
        if (attack <= defense) {
            proof = osl::checkmate::PieceCost::attack_sacrifice_cost[move.ptype()];
            if (defense >= 2 && attack == defense) {
                proof /= 2;
            }
        }
    }
    return proof;
}

template <osl::Player P>
void run_attack_estimate_probe_t(osl::NumEffectState& state) {
    osl::checkmate::Dfpn::DfpnMoveVector moves;
    bool pawn = false;
    osl::checkmate::Dfpn::generateCheck<P>(state, moves, pawn);
    const osl::Square king = state.kingSquare<osl::alt(P)>();
    const osl::checkmate::King8Info info_modified =
        osl::checkmate::Edge_Table.resetEdgeFromLiberty(
            osl::alt(P), king, osl::checkmate::King8Info(state.Iking8Info(osl::alt(P))));
    std::cout << "attack_estimates=" << moves.size() << " pawn=" << (pawn ? 1 : 0) << "\n";
    for (size_t i = 0; i < moves.size(); ++i) {
        unsigned int proof = 0;
        unsigned int disproof = 0;
        osl::checkmate::LibertyEstimator::attackH(P, state, info_modified, moves[i], proof, disproof);
        std::cout << "  attack_estimate " << i << ' ' << osl::usi::show(moves[i])
            << " pdp=" << proof << ',' << disproof
            << " cost=" << probe_attack_proof_cost(P, state, moves[i]) << "\n";
    }
}

void run_attack_estimate_probe(osl::NumEffectState& state) {
    if (state.turn() == osl::BLACK) {
        run_attack_estimate_probe_t<osl::BLACK>(state);
    }
    else {
        run_attack_estimate_probe_t<osl::WHITE>(state);
    }
}
}

int main(int argc, char** argv) {
    osl::OslConfig::setUp();
    osl::OslConfig::setDfpnMaxDepth(1600);
    bool fixed_probe = false;
    bool escape_probe = false;
    bool fixed_escape_probe = false;
    bool attack_estimate_probe = false;
    bool use_path_history = false;
    bool solve_root_then_query_path = false;
    bool escape_search = false;
    bool query_only = false;
    bool dump_prefix_records = false;
    bool print_sizes = false;
    bool single_limit = false;
    std::string child_filter;
    size_t stop_limit = 0;
    int arg_base = 1;
    while (argc > arg_base) {
        const std::string option = argv[arg_base];
        if (option == "--fixed") {
            fixed_probe = true;
            ++arg_base;
        }
        else if (option == "--escapes") {
            escape_probe = true;
            ++arg_base;
        }
        else if (option == "--fixed-escapes") {
            fixed_escape_probe = true;
            ++arg_base;
        }
        else if (option == "--attack-estimates") {
            attack_estimate_probe = true;
            ++arg_base;
        }
        else if (option == "--path") {
            use_path_history = true;
            ++arg_base;
        }
        else if (option == "--solve-root-query-path") {
            solve_root_then_query_path = true;
            ++arg_base;
        }
        else if (option == "--escape-search") {
            escape_search = true;
            ++arg_base;
        }
        else if (option == "--single") {
            single_limit = true;
            ++arg_base;
        }
        else if (option == "--query-only") {
            query_only = true;
            ++arg_base;
        }
        else if (option == "--dump-prefixes") {
            dump_prefix_records = true;
            ++arg_base;
        }
        else if (option == "--dump-children") {
            ++arg_base;
        }
        else if (option == "--sizes") {
            print_sizes = true;
            ++arg_base;
        }
        else if (option == "--child") {
            if (argc <= arg_base + 1) {
                std::cerr << "usage: osl_dfpn_probe --child move <position> [query-moves...]\n";
                return 2;
            }
            child_filter = argv[arg_base + 1];
            arg_base += 2;
        }
        else if (option == "--limit" || option == "--nodes") {
            if (argc <= arg_base + 1) {
                std::cerr << "usage: osl_dfpn_probe [--fixed] [--escapes] [--limit n] <position> [query-moves...]\n";
                return 2;
            }
            stop_limit = static_cast<size_t>(std::stoull(argv[arg_base + 1]));
            arg_base += 2;
        }
        else {
            break;
        }
    }
    if (argc <= arg_base) {
        if (print_sizes) {
            const size_t unit_size =
                sizeof(osl::HashKey) + sizeof(osl::checkmate::DfpnRecord) + sizeof(void*) * 2;
            std::cout << "sizeof_hashkey=" << sizeof(osl::HashKey) << "\n";
            std::cout << "sizeof_record=" << sizeof(osl::checkmate::DfpnRecord) << "\n";
            std::cout << "sizeof_ptr2=" << sizeof(void*) * 2 << "\n";
            std::cout << "hash_unit=" << unit_size << "\n";
            std::cout << "growth64=" << (64ull * 1024ull * 1024ull) / std::max<size_t>(1, unit_size) << "\n";
            return 0;
        }
        std::cerr << "usage: osl_dfpn_probe [--fixed] <position> [query-moves...]\n";
        return 2;
    }

    std::string line = argv[arg_base];
    if (line.rfind("position ", 0) != 0) {
        line = "position " + line;
    }

    osl::NumEffectState root;
    osl::usi::parse(line, root);

    if (fixed_probe || escape_probe || fixed_escape_probe || attack_estimate_probe) {
        osl::NumEffectState query = root;
        for (int i = arg_base + 1; i < argc; ++i) {
            const osl::Move move = dfpn_move_from_usi(argv[i], query);
            query.makeMove(move);
        }
        if (fixed_probe) {
            run_fixed_probe(query);
            return 0;
        }
        if (fixed_escape_probe) {
            run_fixed_escape_probe(query);
            return 0;
        }
        if (attack_estimate_probe) {
            run_attack_estimate_probe(query);
            return 0;
        }
        run_escape_probe(query);
        return 0;
    }

    std::vector<std::string> query_move_usi;
    for (int i = arg_base + 1; i < argc; ++i) {
        query_move_usi.emplace_back(argv[i]);
    }

    osl::NumEffectState search_state = root;
    const osl::Player path_player = escape_search ? osl::alt(root.turn()) : root.turn();
    osl::PathEncoding path(path_player);
    osl::Move last_move = osl::Move::INVALID();
    if (use_path_history && !solve_root_then_query_path) {
        for (const std::string& move_usi : query_move_usi) {
            const osl::Move move = dfpn_move_from_usi(move_usi, search_state);
            path.pushMove(move);
            search_state.makeMove(move);
            last_move = move;
        }
        query_move_usi.clear();
    }

    osl::checkmate::Dfpn dfpn;
    const osl::Player table_player = escape_search ? osl::alt(root.turn()) : root.turn();
    osl::checkmate::DfpnTable table(table_player);
    osl::OslConfig::setDfpnMaxDepth(1600);
    {
        const size_t hash_bytes = 64ull * 1024ull * 1024ull;
        const size_t unit_size =
            sizeof(osl::HashKey) + sizeof(osl::checkmate::DfpnRecord) + sizeof(void*) * 2;
        table.setGrowthLimit(std::max<size_t>(1024, hash_bytes / std::max<size_t>(1, unit_size)));
    }
    dfpn.setTable(&table);
    osl::Move best;
    std::vector<osl::Move> pv;
    const osl::HashKey root_key(search_state);
    osl::checkmate::ProofDisproof result;
    const auto next_limit = [](const size_t current) {
        if (current < 4096) {
            return current * 4;
        }
        if (current < 262144) {
            return current * 2;
        }
        return current + 262144;
    };

    size_t limit = single_limit && stop_limit != 0 ? stop_limit : 256;
    size_t total_nodes = 0;
    do {
        pv.clear();
        result = escape_search
            ? dfpn.hasEscapeMove(search_state, root_key, path, limit, last_move)
            : dfpn.hasCheckmateMove(search_state, root_key, path, limit, best, last_move, &pv);
        total_nodes += dfpn.nodeCount();
        if (std::getenv("OSL_PROBE_ROOT_RESULT") != nullptr) {
            std::cerr << "osl probe root result limit=" << limit
                << " pdp=" << result.proof() << ',' << result.disproof()
                << " best=" << show_move(best)
                << " nodes=" << dfpn.nodeCount()
                << " total=" << total_nodes
                << " table=" << dfpn.currentTable().size()
                << "\n";
        }
        if (const char* dump_children = std::getenv("OSL_PROBE_ROOT_RESULT_CHILDREN")) {
            const size_t filter = std::strtoull(dump_children, nullptr, 10);
            if (filter == 0 || filter == limit) {
                osl::checkmate::Dfpn::DfpnMoveVector root_moves;
                bool has_pawn_checkmate = false;
                if (search_state.turn() == osl::BLACK) {
                    osl::checkmate::Dfpn::generateCheck<osl::BLACK>(
                        search_state, root_moves, has_pawn_checkmate);
                }
                else {
                    osl::checkmate::Dfpn::generateCheck<osl::WHITE>(
                        search_state, root_moves, has_pawn_checkmate);
                }
                std::cerr << "osl probe root table-children limit=" << limit
                    << " pdp=" << result.proof() << ',' << result.disproof()
                    << " best=" << show_move(best)
                    << " nodes=" << dfpn.nodeCount()
                    << "\n";
                for (size_t i = 0; i < root_moves.size(); ++i) {
                    osl::NumEffectState child(search_state);
                    child.makeMove(root_moves[i]);
                    const osl::HashKey child_key(child);
                    const osl::checkmate::DfpnRecord child_record =
                        dfpn.currentTable().probe(child_key, osl::PieceStand(osl::WHITE, child));
                    std::cerr << "  child " << i
                        << " move=" << show_move(root_moves[i])
                        << " pdp=" << child_record.proof() << ',' << child_record.disproof()
                        << " nodes=" << child_record.node_count
                        << " best=" << show_move(child_record.best_move)
                        << " last=" << show_move(child_record.last_move);
                    print_record_tail(child_record);
                    std::cerr << "\n";
                }
            }
        }
        if (single_limit || result.isFinal()) {
            break;
        }
        if (stop_limit != 0 && limit >= stop_limit) {
            break;
        }
        limit = next_limit(limit);
    } while (limit <= (1u << 26));

    std::cout << "result=" << result.proof() << ',' << result.disproof()
        << " best=" << show_move(best)
        << " nodes=" << dfpn.nodeCount()
        << " total_nodes=" << total_nodes << "\n";
    std::cout << "pv=";
    for (size_t i = 0; i < pv.size(); ++i) {
        if (i) {
            std::cout << ' ';
        }
        std::cout << osl::usi::show(pv[i]);
    }
    std::cout << "\n";

    if (dump_prefix_records) {
        osl::NumEffectState prefix = root;
        std::cout << "prefix_records=" << query_move_usi.size() << "\n";
        for (size_t i = 0; i <= query_move_usi.size(); ++i) {
            const osl::HashKey prefix_key(prefix);
            const osl::checkmate::DfpnRecord prefix_record =
                dfpn.currentTable().probe(prefix_key, osl::PieceStand(osl::WHITE, prefix));
            osl::checkmate::ProofTreeDepthDfpn depth_analyzer(dfpn.currentTable());
            const int prefix_depth = depth_analyzer.depth(
                prefix_key, prefix, prefix.turn() == root.turn());
            std::cout << "  prefix " << i
                << " turn=" << (prefix.turn() == osl::BLACK ? "b" : "w")
                << " sfen=" << osl::usi::show(prefix)
                << " pdp=" << prefix_record.proof() << ',' << prefix_record.disproof()
                << " best=" << show_move(prefix_record.best_move)
                << " last=" << show_move(prefix_record.last_move)
                << " nodes=" << prefix_record.node_count
                << " depth=" << prefix_depth;
            print_record_tail(prefix_record);
            std::cout << "\n";
            if (i == query_move_usi.size()) {
                break;
            }
            const osl::Move move = dfpn_move_from_usi(query_move_usi[i], prefix);
            prefix.makeMove(move);
        }
    }

    osl::NumEffectState query = solve_root_then_query_path ? root : search_state;
    for (const std::string& move_usi : query_move_usi) {
        const osl::Move move = dfpn_move_from_usi(move_usi, query);
        query.makeMove(move);
    }

    const osl::HashKey query_key(query);
    const osl::checkmate::DfpnRecord query_record =
        dfpn.currentTable().probe(query_key, osl::PieceStand(osl::WHITE, query));
    osl::checkmate::ProofTreeDepthDfpn depth_analyzer(dfpn.currentTable());
    std::cout << "query_sfen=" << osl::usi::show(query) << "\n";
    std::cout << "query_board_key=" << query_key.boardKey64() << "\n";
    std::cout << "query_sig=" << query_key.signature() << "\n";
    std::cout << "query_record=" << query_record.proof() << ',' << query_record.disproof()
        << " best=" << show_move(query_record.best_move)
        << " last=" << show_move(query_record.last_move)
        << " nodes=" << query_record.node_count;
    print_record_tail(query_record);
    std::cout << "\n";
    if (std::getenv("OSL_PROBE_SHOW_ORACLES") != nullptr) {
        const osl::checkmate::DfpnRecord oracle =
            dfpn.currentTable().findProofOracle(query_key, osl::PieceStand(osl::WHITE, query), osl::Move());
        std::cerr << "query_oracle=" << oracle.proof() << ',' << oracle.disproof()
            << " best=" << show_move(oracle.best_move)
            << " last=" << show_move(oracle.last_move)
            << " nodes=" << oracle.node_count
            << " proof=[" << show_stand(oracle.proofPieces()) << "]"
            << " black=[" << show_stand(oracle.stands[osl::BLACK]) << "]"
            << " white=[" << show_stand(oracle.stands[osl::WHITE]) << "]\n";
    }
    if (query_only) {
        return 0;
    }

    osl::checkmate::Dfpn::DfpnMoveVector moves;
    bool has_pawn_checkmate = false;
    if (query.inCheck()) {
        const osl::Square delayed_to =
            query_record.last_to != query.kingSquare(query.turn())
                ? query_record.last_to
                : osl::Square();
        const bool delay_node = query.turn() == osl::BLACK
            ? is_delay_escape_node_like_dfpn<osl::WHITE>(query, delayed_to)
            : is_delay_escape_node_like_dfpn<osl::BLACK>(query, delayed_to);
        std::cout << "query_last_to=" << show_square(query_record.last_to)
            << " query_king=" << show_square(query.kingSquare(query.turn()))
            << " query_delayed_to=" << show_square(delayed_to)
            << " delay=" << (delay_node ? 1 : 0)
            << "\n";
        if (query.turn() == osl::BLACK) {
            osl::checkmate::Dfpn::generateEscape<osl::WHITE>(query, query_record.need_full_width != 0, delayed_to, moves);
        }
        else {
            osl::checkmate::Dfpn::generateEscape<osl::BLACK>(query, query_record.need_full_width != 0, delayed_to, moves);
        }
    }
    else if (query.turn() == osl::BLACK) {
        osl::checkmate::Dfpn::generateCheck<osl::BLACK>(query, moves, has_pawn_checkmate);
    }
    else {
        osl::checkmate::Dfpn::generateCheck<osl::WHITE>(query, moves, has_pawn_checkmate);
    }
    std::cout << "children=" << moves.size() << " pawn=" << (has_pawn_checkmate ? 1 : 0) << "\n";
    for (size_t i = 0; i < moves.size(); ++i) {
        if (!child_filter.empty() && osl::usi::show(moves[i]) != child_filter) {
            continue;
        }
        const osl::HashKey child_key = query_key.newHashWithMove(moves[i]);
        const osl::PieceStand child_white_stand = (query.turn() == osl::WHITE)
            ? osl::PieceStand(osl::WHITE, query).nextStand(osl::WHITE, moves[i])
            : osl::PieceStand(osl::WHITE, query);
        osl::NumEffectState child_state(query);
        child_state.makeMove(moves[i]);
        const std::string child_sfen = osl::usi::show(child_state);
        const osl::checkmate::DfpnRecord child = dfpn.currentTable().probe(child_key, child_white_stand);
        const int child_depth = depth_analyzer.depth(child_key, child_state, query.inCheck());
        int attack_support = 0;
        int defense_support = 0;
        int proof_cost = 0;
        if (!query.inCheck()) {
            attack_support = query.countEffect(query.turn(), moves[i].to())
                + (moves[i].from().isPieceStand() ? 1 : 0);
            defense_support = query.countEffect(osl::alt(query.turn()), moves[i].to());
            if (!moves[i].isCapture() && attack_support <= defense_support) {
                proof_cost = osl::checkmate::PieceCost::attack_sacrifice_cost[moves[i].ptype()];
                if (defense_support >= 2 && attack_support == defense_support) {
                    proof_cost /= 2;
                }
            }
        }
        std::cout << "  child " << i << ' ' << osl::usi::show(moves[i])
            << " pdp=" << child.proof() << ',' << child.disproof()
            << " best=" << show_move(child.best_move)
            << " last=" << show_move(child.last_move)
            << " nodes=" << child.node_count
            << " depth=" << child_depth
            << " cost=" << proof_cost
            << " attack_support=" << attack_support
            << " defense_support=" << defense_support
            << " ptype=" << static_cast<int>(moves[i].ptype())
            << " board_key=" << child_key.boardKey64()
            << " sfen=" << child_sfen;
        print_record_tail(child);
        std::cout << "\n";
    }
}
