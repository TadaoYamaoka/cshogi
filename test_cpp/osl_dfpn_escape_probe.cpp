#include "osl/checkmate/dfpn.h"
#include "osl/checkmate/dfpnRecord.h"
#include "osl/hashKey.h"
#include "osl/numEffectState.h"
#include "osl/oslConfig.h"
#include "osl/pathEncoding.h"
#include "osl/usi.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: osl_dfpn_escape_probe <sfen> [last_move] [limit]\n";
        return 1;
    }

    try {
        int arg_index = 1;
        bool attack_mode = false;
        bool iterative_mode = false;
        if (std::string(argv[arg_index]) == "--attack") {
            attack_mode = true;
            ++arg_index;
        }
        if (argc > arg_index && std::string(argv[arg_index]) == "--iter") {
            iterative_mode = true;
            ++arg_index;
        }
        if (argc <= arg_index) {
            std::cerr << "usage: osl_dfpn_escape_probe [--attack] <sfen> [last_move] [limit]\n";
            return 1;
        }

        osl::OslConfig::setUp();
        osl::NumEffectState state;
        osl::usi::parse(std::string("position ") + argv[arg_index], state);

        osl::Move last_move = osl::Move::INVALID();
        if (argc > arg_index + 1 && std::string(argv[arg_index + 1]) != "-") {
            osl::NumEffectState before = state;
            // last_move is only used for pawn-checkmate normalization and record metadata.
            // Parse it against the current state when possible; otherwise keep INVALID.
            try {
                last_move = osl::usi::strToMove(argv[arg_index + 1], before);
            }
            catch (...) {
                last_move = osl::Move::INVALID();
            }
        }

        const std::size_t limit = argc > arg_index + 2
            ? static_cast<std::size_t>(std::strtoull(argv[arg_index + 2], nullptr, 10))
            : static_cast<std::size_t>(100000000);
        const osl::Player attack = attack_mode ? state.turn() : osl::alt(state.turn());

        osl::OslConfig::setDfpnMaxDepth(1600);
        osl::checkmate::DfpnTable table(attack);
        const std::size_t hash_bytes = 64ull * 1024ull * 1024ull;
        const std::size_t unit_size =
            sizeof(osl::HashKey) + sizeof(osl::checkmate::DfpnRecord) + sizeof(void*) * 2;
        table.setGrowthLimit(std::max<std::size_t>(1024, hash_bytes / std::max<std::size_t>(1, unit_size)));
        osl::checkmate::Dfpn searcher;
        searcher.setTable(&table);

        const osl::HashKey key(state);
        const osl::PathEncoding path(attack);
        osl::Move best_move = osl::Move::INVALID();
        osl::checkmate::ProofDisproof result;
        std::size_t total_nodes = 0;
        if (attack_mode && iterative_mode) {
            std::size_t current_limit = std::min<std::size_t>(256, limit);
            while (true) {
                result = searcher.hasCheckmateMove(state, key, path, current_limit, best_move, last_move, nullptr);
                total_nodes += searcher.nodeCount();
                std::cerr << "oslprobe root result limit=" << current_limit
                          << " pdp=" << result
                          << " best=" << (best_move.isNormal() ? osl::usi::show(best_move) : "-")
                          << " nodes=" << searcher.nodeCount()
                          << " total=" << total_nodes
                          << "\n";
                if (result.isFinal() || current_limit >= limit) {
                    break;
                }
                if (current_limit < 4096) {
                    current_limit *= 4;
                }
                else if (current_limit < 262144) {
                    current_limit *= 2;
                }
                else {
                    current_limit += 262144;
                }
                current_limit = std::min<std::size_t>(current_limit, limit);
            }
        }
        else {
            result = attack_mode
                ? searcher.hasCheckmateMove(state, key, path, limit, best_move, last_move, nullptr)
                : searcher.hasEscapeMove(state, key, path, limit, last_move);
            total_nodes = searcher.nodeCount();
        }

        const osl::checkmate::DfpnRecord record = table.probe(key, osl::PieceStand(osl::WHITE, state));
        std::cout << "sfen=" << osl::usi::show(state) << "\n";
        std::cout << "key64=" << key.boardKey64()
                  << " sig=" << key.signature()
                  << " black=" << key.blackStand().getFlags()
                  << " white=" << osl::PieceStand(osl::WHITE, state).getFlags()
                  << "\n";
        std::cout << "result=" << result << " nodes=" << total_nodes << "\n";
        std::cout << "record=" << record.proof_disproof
                  << " best=" << (record.best_move.isNormal() ? osl::usi::show(record.best_move) : "-")
                  << " last=" << (record.last_move.isNormal() ? osl::usi::show(record.last_move) : "-")
                  << " nodes=" << record.node_count
                  << " min_pdp=" << record.min_pdp
                  << " need_full=" << static_cast<int>(record.need_full_width)
                  << " false_branch=" << static_cast<int>(record.false_branch)
                  << " solved=0x" << std::hex << record.solved
                  << " dag=0x" << record.dag_moves << std::dec
                  << "\n";
        if (attack_mode || state.inCheck()) {
            bool has_pawn_checkmate = false;
            osl::checkmate::Dfpn::DfpnMoveVector moves;
            if (state.inCheck()) {
                if (attack == osl::BLACK) {
                    osl::checkmate::Dfpn::generateEscape<osl::BLACK>(
                        state, record.need_full_width, osl::Square(), moves);
                }
                else {
                    osl::checkmate::Dfpn::generateEscape<osl::WHITE>(
                        state, record.need_full_width, osl::Square(), moves);
                }
            }
            else {
                if (state.turn() == osl::BLACK) {
                    osl::checkmate::Dfpn::generateCheck<osl::BLACK>(state, moves, has_pawn_checkmate);
                }
                else {
                    osl::checkmate::Dfpn::generateCheck<osl::WHITE>(state, moves, has_pawn_checkmate);
                }
            }
            std::cout << "children=" << moves.size() << "\n";
            for (std::size_t i = 0; i < moves.size(); ++i) {
                osl::NumEffectState child_state(state);
                child_state.makeMove(moves[i]);
                const osl::HashKey child_key(child_state);
                const osl::checkmate::DfpnRecord child =
                    table.probe(child_key, osl::PieceStand(osl::WHITE, child_state));
                std::cout << "  child " << i << " " << osl::usi::show(moves[i])
                          << " pdp=" << child.proof_disproof
                          << " best=" << (child.best_move.isNormal() ? osl::usi::show(child.best_move) : "-")
                          << " last=" << (child.last_move.isNormal() ? osl::usi::show(child.last_move) : "-")
                          << " nodes=" << child.node_count
                          << " min_pdp=" << child.min_pdp
                          << " need_full=" << static_cast<int>(child.need_full_width)
                          << " false_branch=" << static_cast<int>(child.false_branch)
                          << " solved=0x" << std::hex << child.solved
                          << " dag=0x" << child.dag_moves << std::dec
                          << "\n";
            }
        }
        const int query_start = arg_index + 3;
        if (argc > query_start) {
            osl::NumEffectState query_state(state);
            for (int i = query_start; i < argc; ++i) {
                const osl::Move move = osl::usi::strToMove(argv[i], query_state);
                query_state.makeMove(move);
            }
            const osl::HashKey query_key(query_state);
            const osl::checkmate::DfpnRecord query =
                table.probe(query_key, osl::PieceStand(osl::WHITE, query_state));
            std::cout << "query_sfen=" << osl::usi::show(query_state) << "\n";
            std::cout << "query_record=" << query.proof_disproof
                      << " best=" << (query.best_move.isNormal() ? osl::usi::show(query.best_move) : "-")
                      << " last=" << (query.last_move.isNormal() ? osl::usi::show(query.last_move) : "-")
                      << " nodes=" << query.node_count
                      << " min_pdp=" << query.min_pdp
                      << " need_full=" << static_cast<int>(query.need_full_width)
                      << " false_branch=" << static_cast<int>(query.false_branch)
                      << " solved=0x" << std::hex << query.solved
                      << " dag=0x" << query.dag_moves << std::dec
                      << "\n";
            {
                bool query_has_pawn_checkmate = false;
                osl::checkmate::Dfpn::DfpnMoveVector query_moves;
                if (query_state.inCheck()) {
                    if (attack == osl::BLACK) {
                        osl::checkmate::Dfpn::generateEscape<osl::BLACK>(
                            query_state, query.need_full_width, osl::Square(), query_moves);
                    }
                    else {
                        osl::checkmate::Dfpn::generateEscape<osl::WHITE>(
                            query_state, query.need_full_width, osl::Square(), query_moves);
                    }
                }
                else {
                    if (query_state.turn() == osl::BLACK) {
                        osl::checkmate::Dfpn::generateCheck<osl::BLACK>(query_state, query_moves, query_has_pawn_checkmate);
                    }
                    else {
                        osl::checkmate::Dfpn::generateCheck<osl::WHITE>(query_state, query_moves, query_has_pawn_checkmate);
                    }
                }
                std::cout << "query_children=" << query_moves.size() << "\n";
                for (std::size_t i = 0; i < query_moves.size(); ++i) {
                    osl::NumEffectState child_state(query_state);
                    child_state.makeMove(query_moves[i]);
                    const osl::HashKey child_key(child_state);
                    const osl::checkmate::DfpnRecord child =
                        table.probe(child_key, osl::PieceStand(osl::WHITE, child_state));
                    std::cout << "  qchild " << i << " " << osl::usi::show(query_moves[i])
                              << " pdp=" << child.proof_disproof
                              << " best=" << (child.best_move.isNormal() ? osl::usi::show(child.best_move) : "-")
                              << " last=" << (child.last_move.isNormal() ? osl::usi::show(child.last_move) : "-")
                              << " nodes=" << child.node_count
                              << " min_pdp=" << child.min_pdp
                              << " need_full=" << static_cast<int>(child.need_full_width)
                              << " false_branch=" << static_cast<int>(child.false_branch)
                              << " solved=0x" << std::hex << child.solved
                              << " dag=0x" << child.dag_moves << std::dec
                              << "\n";
                }
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
