#include "osl/checkmate/dfpn.h"
#include "osl/checkmate/dfpnRecord.h"
#include "osl/checkmate/fixedDepthSolverExt.h"
#include "osl/checkmate/fixedDepthSearcher.h"
#include "osl/checkmate/proofNumberTable.h"
#include "osl/move_generator/addEffectWithEffect.tcc"
#include "osl/move_generator/move_action.h"
#include "osl/numEffectState.h"
#include "osl/usi.h"

#include <iostream>
#include <sstream>
#include <string>

int main(int argc, char** argv) {
    osl::checkmate::Proof_Number_Table.init();
    osl::checkmate::Edge_Table.init();

    if (argc < 2) {
        std::cerr << "usage: osl_gen_probe [--raw|--escapes|--escapes-full|--fixed-escapes|--attack-est-zero|--fixed depth|--solve-seq max_limit|--solve-path max_limit [--dump-prefixes]] <position>\n";
        return 2;
    }

    int arg_index = 1;
    bool raw = false;
    bool escapes = false;
    bool escapes_full = false;
    bool fixed_escapes = false;
    bool attack_est_zero = false;
    bool fixed = false;
    bool solve_seq = false;
    bool solve_path = false;
    bool dump_prefixes = false;
    int fixed_depth = 0;
    std::size_t solve_max_limit = 0;
    if (std::string(argv[arg_index]) == "--raw") {
        raw = true;
        ++arg_index;
    }
    else if (std::string(argv[arg_index]) == "--escapes") {
        escapes = true;
        ++arg_index;
    }
    else if (std::string(argv[arg_index]) == "--escapes-full") {
        escapes_full = true;
        ++arg_index;
    }
    else if (std::string(argv[arg_index]) == "--fixed-escapes") {
        fixed_escapes = true;
        ++arg_index;
    }
    else if (std::string(argv[arg_index]) == "--attack-est-zero") {
        attack_est_zero = true;
        ++arg_index;
    }
    else if (std::string(argv[arg_index]) == "--fixed") {
        if (argc <= arg_index + 2) {
            std::cerr << "usage: osl_gen_probe --fixed depth <position>\n";
            return 2;
        }
        fixed = true;
        fixed_depth = std::stoi(argv[arg_index + 1]);
        arg_index += 2;
    }
    else if (std::string(argv[arg_index]) == "--solve-seq") {
        if (argc <= arg_index + 2) {
            std::cerr << "usage: osl_gen_probe --solve-seq max_limit <position>\n";
            return 2;
        }
        solve_seq = true;
        solve_max_limit = static_cast<std::size_t>(std::stoull(argv[arg_index + 1]));
        arg_index += 2;
    }
    else if (std::string(argv[arg_index]) == "--solve-path") {
        if (argc <= arg_index + 2) {
            std::cerr << "usage: osl_gen_probe --solve-path max_limit <position> moves <move...>\n";
            return 2;
        }
        solve_path = true;
        solve_max_limit = static_cast<std::size_t>(std::stoull(argv[arg_index + 1]));
        arg_index += 2;
        if (argc > arg_index && std::string(argv[arg_index]) == "--dump-prefixes") {
            dump_prefixes = true;
            ++arg_index;
        }
    }
    if (argc <= arg_index) {
        std::cerr << "usage: osl_gen_probe [--raw|--escapes|--escapes-full|--fixed-escapes|--attack-est-zero|--fixed depth|--solve-seq max_limit|--solve-path max_limit [--dump-prefixes]] <position>\n";
        return 2;
    }

    std::string line;
    for (int i = arg_index; i < argc; ++i) {
        if (i > arg_index) {
            line += ' ';
        }
        line += argv[i];
    }
    if (line.rfind("position ", 0) != 0) {
        line = "position " + line;
    }

    osl::NumEffectState state;
    osl::usi::parse(line, state);

    if (attack_est_zero) {
        const osl::Player attack = state.turn();
        const osl::Player defense = osl::alt(attack);
        const osl::Square king = state.kingSquare(defense);
        const osl::checkmate::King8Info raw(state.Iking8Info(defense));
        const osl::checkmate::King8Info info =
            osl::checkmate::Edge_Table.resetEdgeFromLiberty(defense, king, raw);
        const int move_mask = attack == osl::BLACK
            ? info.moveCandidateMask<osl::BLACK>(state)
            : info.moveCandidateMask<osl::WHITE>(state);
        const int drop_proof =
            osl::checkmate::Proof_Number_Table.libertyAfterAllDrop(state, attack, info);
        const int move_proof = drop_proof >= 2
            ? osl::checkmate::Proof_Number_Table.libertyAfterAllMove(state, attack, info, king)
            : drop_proof;
        const int disproof =
            osl::checkmate::Proof_Number_Table.disproofAfterAllCheck(state, attack, info);
        const osl::checkmate::ProofDisproof pdp =
            osl::checkmate::Proof_Number_Table.attackEstimation(state, attack, info, king);
        std::cout << osl::usi::show(state) << "\n";
        std::cout << "attack_est_zero"
            << " raw=" << raw
            << " raw64=0x" << std::hex << raw.uint64Value() << std::dec
            << " edge=" << info
            << " edge64=0x" << std::hex << info.uint64Value() << std::dec
            << " liberty=0x" << std::hex << info.liberty()
            << " count=" << std::dec << info.libertyCount()
            << " drop=0x" << std::hex << info.dropCandidate()
            << " move_mask=0x" << move_mask << std::dec
            << " drop_proof=" << drop_proof
            << " move_proof=" << move_proof
            << " disproof=" << disproof
            << " pdp=" << pdp.proof() << ',' << pdp.disproof()
            << "\n";
        return 0;
    }

    if (solve_path) {
        osl::NumEffectState root_state;
        std::vector<osl::Move> path_moves;
        const std::string moves_marker = " moves ";
        const std::size_t moves_pos = line.find(moves_marker);
        if (moves_pos == std::string::npos) {
            osl::usi::parse(line, root_state, path_moves);
        }
        else {
            const std::string root_line = line.substr(0, moves_pos);
            osl::usi::parse(root_line, root_state);
            osl::NumEffectState parse_state = root_state;
            std::istringstream moves_stream(line.substr(moves_pos + moves_marker.size()));
            std::string move_text;
            while (moves_stream >> move_text) {
                const osl::Move move = osl::usi::strToMove(move_text, parse_state);
                path_moves.push_back(move);
                parse_state.makeMove(move);
            }
        }
        osl::NumEffectState query_state = root_state;
        for (const osl::Move move : path_moves) {
            query_state.makeMove(move);
        }

        osl::checkmate::Dfpn searcher;
        osl::checkmate::DfpnTable table(root_state.turn());
        searcher.setTable(&table);
        const osl::HashKey root_key(root_state);
        const osl::PathEncoding root_path(root_state.turn());
        osl::Move best_move;
        std::vector<osl::Move> pv;
        const osl::checkmate::ProofDisproof pdp =
            searcher.hasCheckmateMove(root_state, root_key, root_path, solve_max_limit, best_move, osl::Move::INVALID(), &pv);
        std::cout << "root_pdp=" << pdp.proof() << ',' << pdp.disproof()
            << " best=" << (best_move.isNormal() ? osl::usi::show(best_move) : "-")
            << " nodes=" << searcher.nodeCount() << "\n";

        if (dump_prefixes) {
            osl::NumEffectState prefix_state = root_state;
            std::cout << "prefix_records=" << path_moves.size() << "\n";
            for (size_t i = 0; i <= path_moves.size(); ++i) {
                const osl::HashKey prefix_key(prefix_state);
                const osl::PieceStand prefix_white(osl::WHITE, prefix_state);
                const osl::checkmate::DfpnRecord prefix_record = table.probe(prefix_key, prefix_white);
                std::cout << "  prefix " << i
                    << " turn=" << (prefix_state.turn() == osl::BLACK ? "b" : "w")
                    << " sfen=" << osl::usi::show(prefix_state)
                    << " pdp=" << prefix_record.proof() << ',' << prefix_record.disproof()
                    << " best=" << (prefix_record.best_move.isNormal() ? osl::usi::show(prefix_record.best_move) : "-")
                    << " last=" << (prefix_record.last_move.isNormal() ? osl::usi::show(prefix_record.last_move) : "-")
                    << " nodes=" << prefix_record.node_count
                    << " solved=0x" << std::hex << prefix_record.solved
                    << " dag=0x" << prefix_record.dag_moves << std::dec
                    << " full=" << static_cast<int>(prefix_record.need_full_width)
                    << " false=" << (prefix_record.false_branch ? 1 : 0)
                    << "\n";
                if (i == path_moves.size()) {
                    break;
                }
                prefix_state.makeMove(path_moves[i]);
            }
        }

        const osl::HashKey query_key(query_state);
        const osl::PieceStand query_white(osl::WHITE, query_state);
        const osl::checkmate::DfpnRecord query_record = table.probe(query_key, query_white);
        std::cout << "query_sfen=" << osl::usi::show(query_state) << "\n";
        std::cout << "query_key=" << query_key.boardKey64()
            << " sig=" << query_key.signature() << "\n";
        std::cout << "query_pdp=" << query_record.proof() << ',' << query_record.disproof()
            << " best=" << (query_record.best_move.isNormal() ? osl::usi::show(query_record.best_move) : "-")
            << " last=" << (query_record.last_move.isNormal() ? osl::usi::show(query_record.last_move) : "-")
            << " last_to=" << (query_record.last_to.isOnBoard() ? osl::psn::show(query_record.last_to) : "-")
            << " nodes=" << query_record.node_count
            << " solved=0x" << std::hex << query_record.solved
            << " dag=0x" << query_record.dag_moves << std::dec
            << " full=" << static_cast<int>(query_record.need_full_width)
            << " false=" << (query_record.false_branch ? 1 : 0)
            << "\n";

        osl::checkmate::Dfpn::DfpnMoveVector query_moves;
        bool has_pawn_checkmate = false;
        if (query_state.turn() == root_state.turn()) {
            if (root_state.turn() == osl::BLACK) {
                osl::checkmate::Dfpn::generateCheck<osl::BLACK>(query_state, query_moves, has_pawn_checkmate);
            }
            else {
                osl::checkmate::Dfpn::generateCheck<osl::WHITE>(query_state, query_moves, has_pawn_checkmate);
            }
        }
        else {
            const osl::Square delayed_to =
                query_record.last_to != query_state.kingSquare(query_state.turn())
                ? query_record.last_to
                : osl::Square();
            if (root_state.turn() == osl::BLACK) {
                osl::checkmate::Dfpn::generateEscape<osl::BLACK>(
                    query_state, query_record.need_full_width, delayed_to, query_moves);
            }
            else {
                osl::checkmate::Dfpn::generateEscape<osl::WHITE>(
                    query_state, query_record.need_full_width, delayed_to, query_moves);
            }
        }
        std::cout << "query_children=" << query_moves.size() << "\n";
        for (size_t i = 0; i < query_moves.size(); ++i) {
            const osl::Move move = query_moves[i];
            const osl::HashKey child_key = query_key.newHashWithMove(move);
            const osl::PieceStand child_white = query_white.nextStand(query_state.turn(), move);
            const osl::checkmate::DfpnRecord child = table.probe(child_key, child_white);
            std::cout << "  child " << i << ' ' << osl::usi::show(move)
                << " pdp=" << child.proof() << ',' << child.disproof()
                << " best=" << (child.best_move.isNormal() ? osl::usi::show(child.best_move) : "-")
                << " last=" << (child.last_move.isNormal() ? osl::usi::show(child.last_move) : "-")
                << " nodes=" << child.node_count
                << " solved=0x" << std::hex << child.solved
                << " dag=0x" << child.dag_moves << std::dec
                << " full=" << static_cast<int>(child.need_full_width)
                << " false=" << (child.false_branch ? 1 : 0)
                << "\n";
        }
        return 0;
    }

    if (escapes || escapes_full) {
        osl::checkmate::Dfpn::DfpnMoveVector moves;
        if (state.turn() == osl::WHITE) {
            osl::checkmate::Dfpn::generateEscape<osl::BLACK>(state, escapes_full, osl::Square(), moves);
        }
        else {
            osl::checkmate::Dfpn::generateEscape<osl::WHITE>(state, escapes_full, osl::Square(), moves);
        }
        std::cout << osl::usi::show(state) << "\n";
        std::cout << (escapes_full ? "escapes_full=" : "escapes=") << moves.size() << "\n";
        for (size_t i = 0; i < moves.size(); ++i) {
            std::cout << i << ' ' << osl::usi::show(moves[i]) << "\n";
        }
        return 0;
    }

    if (fixed_escapes) {
        osl::checkmate::Dfpn::DfpnMoveVector moves;
        if (state.turn() == osl::WHITE) {
            osl::checkmate::Dfpn::generateEscape<osl::BLACK>(state, false, osl::Square(), moves);
        }
        else {
            osl::checkmate::Dfpn::generateEscape<osl::WHITE>(state, false, osl::Square(), moves);
        }
        std::cout << osl::usi::show(state) << "\n";
        std::cout << "fixed_escapes=" << moves.size() << "\n";
        for (size_t i = 0; i < moves.size(); ++i) {
            osl::checkmate::FixedDepthSolverExt searcher(state);
            osl::Move check_move;
            osl::PieceStand proof_pieces;
            const osl::checkmate::ProofDisproof pdp =
                state.turn() == osl::WHITE
                ? searcher.hasEscapeByMove<osl::BLACK>(moves[i], 0, check_move, proof_pieces)
                : searcher.hasEscapeByMove<osl::WHITE>(moves[i], 0, check_move, proof_pieces);
            std::cout << "  fixed_escape " << i << ' ' << osl::usi::show(moves[i])
                << " pdp=" << pdp.proof() << ',' << pdp.disproof()
                << " best=" << (check_move.isNormal() ? osl::usi::show(check_move) : "-")
                << "\n";
        }
        return 0;
    }

    if (solve_seq) {
        osl::checkmate::Dfpn searcher;
        osl::checkmate::DfpnTable table(state.turn());
        searcher.setTable(&table);
        const osl::HashKey key(state);
        const osl::PathEncoding path(state.turn());
        osl::Move best_move;
        std::vector<osl::Move> pv;
        std::size_t limit = 256;
        const auto next_limit = [](const std::size_t current) {
            if (current < 4096) {
                return current * 4;
            }
            if (current < 262144) {
                return current * 2;
            }
            return current + 262144;
        };
        std::cout << osl::usi::show(state) << "\n";
        while (limit <= solve_max_limit) {
            pv.clear();
            const osl::checkmate::ProofDisproof pdp =
                searcher.hasCheckmateMove(state, key, path, limit, best_move, osl::Move::INVALID(), &pv);
            std::cout << "limit=" << limit
                << " pdp=" << pdp.proof() << ',' << pdp.disproof()
                << " best=" << (best_move.isNormal() ? osl::usi::show(best_move) : "-")
                << " nodes=" << searcher.nodeCount()
                << " pv=" << pv.size();
            for (const osl::Move move : pv) {
                std::cout << ' ' << osl::usi::show(move);
            }
            std::cout << "\n";
            const osl::checkmate::DfpnRecord root_record =
                table.probe(key, osl::PieceStand(osl::WHITE, state));
            std::cout << "  root_record pdp=" << root_record.proof() << ',' << root_record.disproof()
                << " best=" << (root_record.best_move.isNormal() ? osl::usi::show(root_record.best_move) : "-")
                << " last=" << (root_record.last_move.isNormal() ? osl::usi::show(root_record.last_move) : "-")
                << " nodes=" << root_record.node_count
                << " solved=0x" << std::hex << root_record.solved
                << " dag=0x" << root_record.dag_moves << std::dec
                << " full=" << static_cast<int>(root_record.need_full_width)
                << " false=" << (root_record.false_branch ? 1 : 0)
                << "\n";
            if (limit == 256) {
                osl::checkmate::Dfpn::DfpnMoveVector root_moves;
                bool child_has_pawn_checkmate = false;
                if (state.turn() == osl::BLACK) {
                    osl::checkmate::Dfpn::generateCheck<osl::BLACK>(state, root_moves, child_has_pawn_checkmate);
                }
                else {
                    osl::checkmate::Dfpn::generateCheck<osl::WHITE>(state, root_moves, child_has_pawn_checkmate);
                }
                const osl::PieceStand root_white(osl::WHITE, state);
                for (size_t i = 0; i < root_moves.size(); ++i) {
                    const osl::Move move = root_moves[i];
                    const osl::HashKey child_key = key.newHashWithMove(move);
                    const osl::PieceStand child_white = root_white.nextStand(state.turn(), move);
                    const osl::checkmate::DfpnRecord child = table.probe(child_key, child_white);
                    std::cout << "  child " << i << ' ' << osl::usi::show(move)
                        << " pdp=" << child.proof() << ',' << child.disproof()
                        << " best=" << (child.best_move.isNormal() ? osl::usi::show(child.best_move) : "-")
                        << " last=" << (child.last_move.isNormal() ? osl::usi::show(child.last_move) : "-")
                        << " nodes=" << child.node_count
                        << " solved=0x" << std::hex << child.solved
                        << " dag=0x" << child.dag_moves << std::dec
                        << " full=" << static_cast<int>(child.need_full_width)
                        << " false=" << (child.false_branch ? 1 : 0)
                        << "\n";
                }
            }
            if (pdp.isFinal()) {
                break;
            }
            const std::size_t next = next_limit(limit);
            if (next <= limit) {
                break;
            }
            limit = next;
        }
        return 0;
    }

    if (fixed) {
        osl::checkmate::FixedDepthSearcher searcher(state);
        osl::Move best_move;
        const osl::checkmate::ProofDisproof pdp = searcher.hasCheckmateMoveOfTurn(fixed_depth, best_move);
        std::cout << osl::usi::show(state) << "\n";
        std::cout << "fixed_depth=" << fixed_depth
            << " pdp=" << pdp.proof() << ',' << pdp.disproof()
            << " best=" << (best_move.isNormal() ? osl::usi::show(best_move) : "-")
            << " nodes=" << searcher.getCount() << "\n";
        return 0;
    }

    osl::checkmate::Dfpn::DfpnMoveVector moves;
    bool has_pawn_checkmate = false;
    if (raw && state.turn() == osl::BLACK) {
        osl::move_action::Store store(moves);
        osl::move_generator::AddEffectWithEffect<osl::move_action::Store>::generate<osl::BLACK, true>(
            state, state.kingPiece(osl::WHITE).square(), store, has_pawn_checkmate);
    }
    else if (raw && state.turn() == osl::WHITE) {
        osl::move_action::Store store(moves);
        osl::move_generator::AddEffectWithEffect<osl::move_action::Store>::generate<osl::WHITE, true>(
            state, state.kingPiece(osl::BLACK).square(), store, has_pawn_checkmate);
    }
    else if (state.turn() == osl::BLACK) {
        osl::checkmate::Dfpn::generateCheck<osl::BLACK>(state, moves, has_pawn_checkmate);
    }
    else {
        osl::checkmate::Dfpn::generateCheck<osl::WHITE>(state, moves, has_pawn_checkmate);
    }

    std::cout << osl::usi::show(state) << "\n";
    std::cout << "has_pawn_checkmate=" << (has_pawn_checkmate ? 1 : 0) << "\n";
    for (size_t i = 0; i < moves.size(); ++i) {
        std::cout << i << ' ' << osl::usi::show(moves[i]) << "\n";
    }
    return 0;
}
