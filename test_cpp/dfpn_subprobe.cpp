#include "../src/init.hpp"
#include "../src/cshogi.h"
#include "../src/generateMoves.hpp"

#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <chrono>
#include <algorithm>
#include <tuple>
#include <vector>

namespace {
std::string show_hand(const Hand& hand) {
    static constexpr std::array<std::pair<HandPiece, const char*>, 7> pieces = { {
        { HPawn, "P" }, { HLance, "L" }, { HKnight, "N" }, { HSilver, "S" },
        { HGold, "G" }, { HBishop, "B" }, { HRook, "R" }
    } };
    std::string result;
    for (const auto& [piece, label] : pieces) {
        if (!result.empty()) {
            result += ' ';
        }
        result += label;
        result += '=';
        result += std::to_string(hand.numOf(piece));
    }
    return result;
}

int osl_sort_ptype(const PieceType pt) {
    switch (pt) {
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

int osl_square_index(const Square square) {
    return (static_cast<int>(makeFile(square)) + 1) * 16
        + (static_cast<int>(makeRank(square)) + 1) + 1;
}

auto osl_dfpn_move_sort_key(const Position& pos, const Color turn, const Move move) {
    const int attack_support = pos.attackersTo(turn, move.to()).popCount() + (move.isDrop() ? 1 : 0);
    const int defense_support = pos.attackersTo(oppositeColor(turn), move.to()).popCount();
    const int turn_sign = turn == Black ? 1 : -1;
    const int file = static_cast<int>(makeFile(move.to())) + 1;
    const int to_y = turn_sign * (static_cast<int>(makeRank(move.to())) + 1);
    const int to_x = (5 - std::abs(5 - file)) * 2 + (file > 5 ? 1 : 0);
    int from_to = (to_y * 16 + to_x) * 256;
    if (move.isDrop()) {
        from_to += osl_sort_ptype(move.pieceTypeDropped());
    }
    else {
        from_to += osl_square_index(move.from());
    }
    return std::make_tuple(attack_support > defense_support, from_to, move.isPromotion());
}

void sort_like_osl_dfpn(const Position& pos, std::vector<ExtMove>& moves) {
    size_t last_sorted = 0;
    size_t cur = 0;
    PieceType last_piece_type = Occupied;
    const auto sort_segment = [&](const size_t begin, const size_t end) {
        std::sort(moves.begin() + static_cast<std::ptrdiff_t>(begin),
            moves.begin() + static_cast<std::ptrdiff_t>(end),
            [&](const ExtMove& lhs, const ExtMove& rhs) {
                return osl_dfpn_move_sort_key(pos, pos.turn(), lhs.move)
                    > osl_dfpn_move_sort_key(pos, pos.turn(), rhs.move);
            });
    };
    for (; cur < moves.size(); ++cur) {
        const PieceType piece_type = moves[cur].move.isDrop()
            ? Occupied
            : moves[cur].move.pieceTypeFrom();
        if (moves[cur].move.isDrop() || piece_type == last_piece_type) {
            continue;
        }
        sort_segment(last_sorted, cur);
        last_sorted = cur;
        last_piece_type = piece_type;
    }
    sort_segment(last_sorted, cur);
}

bool has_ignored_unpromote_escape_like_osl(const Move move, const Color defender) {
    if (move.isDrop() || !move.isPromotion()) {
        return false;
    }
    switch (move.pieceTypeFrom()) {
    case Pawn:
        return (defender == Black && makeRank(move.to()) != Rank1)
            || (defender == White && makeRank(move.to()) != Rank9);
    case Lance:
        return makeRank(move.to()) == (defender == Black ? Rank2 : Rank8);
    case Bishop:
    case Rook:
        return true;
    default:
        return false;
    }
}

bool is_delay_escape_node_like_osl(const Position& pos, const Square last_to) {
    if (last_to == SquareNum) {
        return false;
    }
    const Color defense = pos.turn();
    const Color attack = oppositeColor(defense);
    const Bitboard defense_attackers_except_king =
        pos.attackersTo(defense, last_to) & ~pos.bbOf(King, defense);
    return pos.attackersToIsAny(defense, last_to)
        && (defense_attackers_except_king.isAny()
            || !pos.attackersToIsAny(attack, last_to));
}

std::vector<ExtMove> generate_like_osl_dfpn_escape(
    const Position& pos,
    const bool need_full_width,
    const Square last_to = SquareNum) {
    ExtMove cheap_buffer[MaxLegalMoves];
    ExtMove* cheap_last = generateOslmateEscapeMoves(cheap_buffer, pos, true, false);
    std::vector<ExtMove> result;
    if (is_delay_escape_node_like_osl(pos, last_to)) {
        for (ExtMove* it = cheap_buffer; it != cheap_last; ++it) {
            if (it->move.to() == last_to) {
                result.push_back(*it);
            }
        }
    }
    else {
        result.assign(cheap_buffer, cheap_last);
    }
    sort_like_osl_dfpn(pos, result);
    if (!need_full_width) {
        return result;
    }

    ExtMove full_buffer[MaxLegalMoves];
    ExtMove* full_last = generateOslmateEscapeMoves(full_buffer, pos, false, false);
    std::vector<ExtMove> full(full_buffer, full_last);
    sort_like_osl_dfpn(pos, full);

    const size_t original_size = result.size();
    for (const ExtMove& candidate : full) {
        const bool exists = std::find_if(
            result.begin(),
            result.begin() + static_cast<std::ptrdiff_t>(original_size),
            [&](const ExtMove& move) { return move.move == candidate.move; }) != result.begin() + static_cast<std::ptrdiff_t>(original_size);
        if (!exists) {
            result.push_back(candidate);
        }
    }

    const size_t full_width_size = result.size();
    for (size_t i = 0; i < full_width_size; ++i) {
        const Move move = result[i].move;
        if (has_ignored_unpromote_escape_like_osl(move, pos.turn())) {
            result.push_back(ExtMove{ Move(static_cast<u32>(move.value() & ~Move::PromoteFlag)) });
        }
    }
    return result;
}

std::vector<ExtMove> generate_like_dfpn_node_children(DfPn& dfpn, Position& pos,
    const ns_dfpn::ProbeRecord& record, const bool attack_node) {
    if (attack_node) {
        const std::vector<Move> moves = dfpn.debug_check_moves(pos);
        std::vector<ExtMove> result;
        result.reserve(moves.size());
        for (const Move move : moves) {
            result.push_back(ExtMove{ move });
        }
        return result;
    }
    const Square delayed_to = record.last_to != pos.kingSquare(pos.turn())
        ? record.last_to
        : SquareNum;
    return generate_like_osl_dfpn_escape(pos, record.need_full_width != 0, delayed_to);
}

Move exact_usi_to_move(const Position& pos, const std::string& usi) {
    for (MoveList<LegalAll> ml(pos); !ml.end(); ++ml) {
        if (ml.move().toUSI() == usi) {
            return ml.move();
        }
    }
    {
        ExtMove buffer[MaxLegalMoves];
        ExtMove* last = generateMoves<CheckAllOslmate>(buffer, pos);
        for (ExtMove* it = buffer; it != last; ++it) {
            if (it->move.toUSI() == usi) {
                return it->move;
            }
        }
    }
    {
        ExtMove buffer[MaxLegalMoves];
        ExtMove* last = generateOslmateEscapeMoves(buffer, const_cast<Position&>(pos), false, false);
        for (ExtMove* it = buffer; it != last; ++it) {
            if (it->move.toUSI() == usi) {
                return it->move;
            }
        }
    }
    return Move::moveNone();
}
}

int main(int argc, char** argv) {
    using namespace ns_dfpn;

    try {
        initTable();
        Position::initZobrist();

        if (argc < 2) {
            std::cerr << "usage: dfpn_subprobe [--andnode] [--path] [--threshold proof disproof] [--nodes max_nodes] [--hash hash_mb] <sfen-or-position> [moves...]\n";
            return 1;
        }

        int arg_index = 1;
        bool andnode = false;
        bool use_path_history = false;
        bool print_checks = false;
        bool print_dfpn_checks = false;
        bool print_checks_sorted = false;
        bool print_fixed = false;
        bool print_fixed_checks = false;
        bool print_escapes = false;
        bool print_fixed_escapes = false;
        bool print_attack_estimates = false;
        bool dump_children_after_search = false;
        bool dump_bucket_after_search = false;
        bool dump_prefix_records = false;
        bool dump_prefix_depth = true;
        bool solve_root_then_query_path = false;
        bool query_only = false;
        std::optional<std::string> child_filter;
        std::optional<ProofDisproof> threshold;
        std::optional<uint32_t> max_nodes;
        std::optional<uint64_t> hash_mb;
        std::optional<std::string> solve_root_position;
        while (argc > arg_index) {
            const std::string option = argv[arg_index];
            if (option == "--andnode") {
                andnode = true;
                ++arg_index;
            }
            else if (option == "--checks") {
                print_checks = true;
                ++arg_index;
            }
            else if (option == "--dfpn-checks") {
                print_dfpn_checks = true;
                ++arg_index;
            }
            else if (option == "--checks-sorted") {
                print_checks = true;
                print_checks_sorted = true;
                ++arg_index;
            }
            else if (option == "--fixed-checks") {
                print_fixed_checks = true;
                ++arg_index;
            }
            else if (option == "--fixed") {
                print_fixed = true;
                ++arg_index;
            }
            else if (option == "--escapes") {
                print_escapes = true;
                ++arg_index;
            }
            else if (option == "--fixed-escapes") {
                print_fixed_escapes = true;
                ++arg_index;
            }
            else if (option == "--attack-estimates") {
                print_attack_estimates = true;
                ++arg_index;
            }
            else if (option == "--dump-children") {
                dump_children_after_search = true;
                ++arg_index;
            }
            else if (option == "--dump-bucket") {
                dump_bucket_after_search = true;
                ++arg_index;
            }
            else if (option == "--dump-prefixes") {
                dump_prefix_records = true;
                ++arg_index;
            }
            else if (option == "--dump-prefixes-fast") {
                dump_prefix_records = true;
                dump_prefix_depth = false;
                ++arg_index;
            }
            else if (option == "--path") {
                use_path_history = true;
                ++arg_index;
            }
            else if (option == "--solve-root-query-path") {
                solve_root_then_query_path = true;
                ++arg_index;
            }
            else if (option == "--solve-root") {
                if (argc <= arg_index + 1) {
                    std::cerr << "usage: dfpn_subprobe --solve-root <root-sfen-or-position> <query-sfen-or-position>\n";
                    return 1;
                }
                solve_root_position = argv[arg_index + 1];
                solve_root_then_query_path = true;
                arg_index += 2;
            }
            else if (option == "--query-only") {
                query_only = true;
                ++arg_index;
            }
            else if (option == "--child") {
                if (argc <= arg_index + 1) {
                    std::cerr << "usage: dfpn_subprobe --child move <sfen-or-position> [moves...]\n";
                    return 1;
                }
                child_filter = argv[arg_index + 1];
                arg_index += 2;
            }
            else if (option == "--threshold") {
                if (argc <= arg_index + 2) {
                    std::cerr << "usage: dfpn_subprobe [--andnode] [--path] [--threshold proof disproof] [--nodes max_nodes] <sfen-or-position> [moves...]\n";
                    return 1;
                }
                threshold = ProofDisproof{
                    static_cast<uint32_t>(std::stoul(argv[arg_index + 1])),
                    static_cast<uint32_t>(std::stoul(argv[arg_index + 2]))
                };
                arg_index += 3;
            }
            else if (option == "--nodes") {
                if (argc <= arg_index + 1) {
                    std::cerr << "usage: dfpn_subprobe [--andnode] [--path] [--threshold proof disproof] [--nodes max_nodes] <sfen-or-position> [moves...]\n";
                    return 1;
                }
                max_nodes = static_cast<uint32_t>(std::stoul(argv[arg_index + 1]));
                arg_index += 2;
            }
            else if (option == "--hash") {
                if (argc <= arg_index + 1) {
                    std::cerr << "usage: dfpn_subprobe [--andnode] [--path] [--threshold proof disproof] [--nodes max_nodes] [--hash hash_mb] <sfen-or-position> [moves...]\n";
                    return 1;
                }
                hash_mb = static_cast<uint64_t>(std::stoull(argv[arg_index + 1]));
                arg_index += 2;
            }
            else {
                break;
            }
        }
        if (argc <= arg_index) {
            std::cerr << "usage: dfpn_subprobe [--andnode] [--path] [--threshold proof disproof] [--nodes max_nodes] <sfen-or-position> [moves...]\n";
            return 1;
        }

        __Board board;
        __Board root_board;
        std::string position_or_sfen = argv[arg_index];
        int history_begin = arg_index + 1;
        if (position_or_sfen.find('/') != std::string::npos && argc >= arg_index + 4) {
            const std::string side = argv[arg_index + 1];
            if ((side == "b" || side == "w") && argv[arg_index + 3][0] >= '0' && argv[arg_index + 3][0] <= '9') {
                position_or_sfen += " ";
                position_or_sfen += argv[arg_index + 1];
                position_or_sfen += " ";
                position_or_sfen += argv[arg_index + 2];
                position_or_sfen += " ";
                position_or_sfen += argv[arg_index + 3];
                history_begin = arg_index + 4;
            }
        }
        if (position_or_sfen.rfind("sfen ", 0) == 0 || position_or_sfen.rfind("startpos", 0) == 0) {
            board.set_position(position_or_sfen);
            root_board.set_position(position_or_sfen);
        }
        else {
            board.set(position_or_sfen);
            root_board.set(position_or_sfen);
        }
        if (solve_root_position) {
            if (solve_root_position->rfind("sfen ", 0) == 0 || solve_root_position->rfind("startpos", 0) == 0) {
                root_board.set_position(*solve_root_position);
            }
            else {
                root_board.set(*solve_root_position);
            }
        }

        std::vector<Move> history;
        for (int i = history_begin; i < argc; ++i) {
            const Move move = exact_usi_to_move(board.pos, argv[i]);
            if (!move) {
                std::cerr << "illegal move: " << argv[i] << "\n";
                return 2;
            }
            board.push(move.value());
            history.emplace_back(move);
        }

        std::cout << "sfen=" << board.toSFEN() << "\n";
        std::cout << "board_key=" << static_cast<unsigned long long>(board.pos.getBoardKey())
            << " key=" << static_cast<unsigned long long>(board.pos.getKey()) << "\n";
        {
            DfPn key_dfpn;
            const ns_dfpn::ProbeRecord key_record = key_dfpn.dfpn_probe_record(board.pos);
            std::cout << "dfpn_board_index=" << key_record.board_key
                << " dfpn_board_secondary=" << key_record.board_secondary << "\n";
        }
        if (print_dfpn_checks) {
            DfPn dfpn;
            const std::vector<Move> moves = dfpn.debug_check_moves(board.pos);
            std::cout << "dfpn_checks=" << moves.size() << "\n";
            for (size_t i = 0; i < moves.size(); ++i) {
                const Move move = moves[i];
                std::cout << "  dfpn_check " << i << ' ' << move.toUSI()
                    << " key=("
                    << (std::get<0>(osl_dfpn_move_sort_key(board.pos, board.pos.turn(), move)) ? 1 : 0)
                    << ',' << std::get<1>(osl_dfpn_move_sort_key(board.pos, board.pos.turn(), move))
                    << ',' << (std::get<2>(osl_dfpn_move_sort_key(board.pos, board.pos.turn(), move)) ? 1 : 0)
                    << ')'
                    << " from=" << static_cast<int>(move.from())
                    << " to=" << static_cast<int>(move.to())
                    << " ptfrom=" << static_cast<int>(move.pieceTypeFrom())
                    << " ptto=" << static_cast<int>(move.pieceTypeTo())
                    << " ptdropped=" << static_cast<int>(move.isDrop() ? move.pieceTypeDropped() : -1)
                    << " promote=" << static_cast<int>(move.isPromotion())
                    << " atk=" << board.pos.attackersTo(board.pos.turn(), move.to()).popCount()
                    << " def=" << board.pos.attackersTo(oppositeColor(board.pos.turn()), move.to()).popCount()
                    << "\n";
            }
            return 0;
        }
        if (print_checks || print_fixed_checks) {
            ExtMove buffer[MaxLegalMoves];
            ExtMove* last = print_fixed_checks
                ? generateMoves<CheckAllOslmateFixedRaw>(buffer, board.pos)
                : generateMoves<CheckAllOslmate>(buffer, board.pos);
            std::vector<ExtMove> checks(buffer, last);
            if (print_checks_sorted) {
                sort_like_osl_dfpn(board.pos, checks);
            }
            std::cout << "checks=" << checks.size() << "\n";
            for (size_t i = 0; i < checks.size(); ++i) {
                const Move move = checks[i].move;
                std::cout << "  check " << i << ' ' << move.toUSI()
                    << " key=("
                    << (std::get<0>(osl_dfpn_move_sort_key(board.pos, board.pos.turn(), move)) ? 1 : 0)
                    << ',' << std::get<1>(osl_dfpn_move_sort_key(board.pos, board.pos.turn(), move))
                    << ',' << (std::get<2>(osl_dfpn_move_sort_key(board.pos, board.pos.turn(), move)) ? 1 : 0)
                    << ')'
                    << " from=" << static_cast<int>(move.from())
                    << " to=" << static_cast<int>(move.to())
                    << " ptfrom=" << static_cast<int>(move.pieceTypeFrom())
                    << " ptdropped=" << static_cast<int>(move.pieceTypeDropped())
                    << " promote=" << static_cast<int>(move.isPromotion())
                    << " atk=" << board.pos.attackersTo(board.pos.turn(), move.to()).popCount()
                    << " def=" << board.pos.attackersTo(oppositeColor(board.pos.turn()), move.to()).popCount()
                    << "\n";
            }
            return 0;
        }
        if (print_escapes) {
            const auto print_list = [&](const char* label, const bool cheap_only, const bool sort_moves) {
                ExtMove buffer[MaxLegalMoves];
                ExtMove* last = generateOslmateEscapeMoves(buffer, board.pos, cheap_only, sort_moves);
                std::cout << label << '=' << (last - buffer) << "\n";
                for (ExtMove* it = buffer; it != last; ++it) {
                    std::cout << "  " << label << ' ' << (it - buffer) << ' ' << it->move.toUSI()
                        << " from=" << static_cast<int>(it->move.from())
                        << " to=" << static_cast<int>(it->move.to())
                        << " ptfrom=" << static_cast<int>(it->move.pieceTypeFrom())
                        << " ptdropped=" << static_cast<int>(it->move.pieceTypeDropped())
                        << " promote=" << static_cast<int>(it->move.isPromotion())
                        << "\n";
                }
            };
            print_list("cheap_raw", true, false);
            print_list("cheap_sorted", true, true);
            print_list("full_raw", false, false);
            print_list("full_sorted", false, true);
            ExtMove nonblock[MaxLegalMoves];
            ExtMove* nonblock_raw_last = generateOslmateEscapeNonblockMoves(nonblock, board.pos, false);
            std::cout << "nonblock_raw=" << (nonblock_raw_last - nonblock) << "\n";
            for (ExtMove* it = nonblock; it != nonblock_raw_last; ++it) {
                std::cout << "  nonblock_raw " << (it - nonblock) << ' ' << it->move.toUSI() << "\n";
            }
            ExtMove* nonblock_last = generateOslmateEscapeNonblockMoves(nonblock, board.pos, true);
            std::cout << "nonblock_sorted=" << (nonblock_last - nonblock) << "\n";
            for (ExtMove* it = nonblock; it != nonblock_last; ++it) {
                std::cout << "  nonblock_sorted " << (it - nonblock) << ' ' << it->move.toUSI() << "\n";
            }
            return 0;
        }
        if (print_fixed) {
            DfPn dfpn;
            const ns_dfpn::FixedCheckDebugRecord record = dfpn.debug_fixed_check(board.pos);
            std::cout << "fixed_result=" << record.proof_disproof.proof << ',' << record.proof_disproof.disproof
                << " best=" << (record.best_move ? record.best_move.toUSI() : "-")
                << " proof=[" << show_hand(record.proof_pieces) << "]\n";
            std::cout << "fixed_children=" << record.children.size() << "\n";
            for (size_t i = 0; i < record.children.size(); ++i) {
                const auto& child = record.children[i];
                std::cout << "  fixed_child " << i << ' ' << child.check_move.toUSI()
                    << " pdp=" << child.proof_disproof.proof << ',' << child.proof_disproof.disproof
                    << " best=" << (child.best_move ? child.best_move.toUSI() : "-")
                    << " proof=[" << show_hand(child.proof_pieces) << "]\n";
            }
            return 0;
        }
        if (print_fixed_escapes) {
            DfPn dfpn;
            const std::vector<ns_dfpn::FixedEscapeDebugRecord> records = dfpn.debug_fixed_escapes(board.pos);
            std::cout << "fixed_escapes=" << records.size() << "\n";
            for (size_t i = 0; i < records.size(); ++i) {
                const auto& record = records[i];
                std::cout << "  fixed_escape " << i << ' ' << record.escape_move.toUSI()
                    << " pdp=" << record.proof_disproof.proof << ',' << record.proof_disproof.disproof
                    << " best=" << (record.best_move ? record.best_move.toUSI() : "-")
                    << " searched=" << (record.searched_attack_node ? 1 : 0)
                    << " proof=[" << show_hand(record.proof_pieces) << "]\n";
            }
            return 0;
        }
        if (print_attack_estimates) {
            DfPn dfpn;
            const std::vector<ns_dfpn::AttackEstimateDebugRecord> records = dfpn.debug_attack_estimates(board.pos);
            std::cout << "attack_estimates=" << records.size() << "\n";
            for (size_t i = 0; i < records.size(); ++i) {
                const auto& record = records[i];
                std::cout << "  attack_estimate " << i << ' ' << record.move.toUSI()
                    << " pdp=" << record.proof_disproof.proof << ',' << record.proof_disproof.disproof
                    << " cost=" << record.proof_cost
                    << " attack_support=" << record.attack_support
                    << " defense_support=" << record.defense_support
                    << " ptype=" << static_cast<int>(record.ptype)
                    << "\n";
            }
            return 0;
        }
        if (threshold) {
            std::cout << "threshold=" << threshold->proof << ',' << threshold->disproof << "\n";
        }

        DfPn dfpn;
        if (max_nodes) {
            dfpn.set_max_search_node(*max_nodes);
        }
        if (hash_mb) {
            dfpn.set_hash(*hash_mb);
        }
        if (solve_root_then_query_path) {
            __Board query_board = board;
            board = root_board;
            DfPn root_dfpn;
            if (max_nodes) {
                root_dfpn.set_max_search_node(*max_nodes);
            }
            if (hash_mb) {
                root_dfpn.set_hash(*hash_mb);
            }
            const bool root_result = andnode
                ? root_dfpn.dfpn_andnode(board.pos)
                : root_dfpn.dfpn(board.pos);
            std::cout << "root_result=" << root_result << " root_nodes=" << root_dfpn.searchedNode << "\n";
            if (dump_prefix_records) {
                __Board prefix_board = root_board;
                std::vector<StateInfo> prefix_states(history.size());
                std::cout << "prefix_records=" << history.size() << "\n";
                for (size_t i = 0; i <= history.size(); ++i) {
                    const ns_dfpn::ProbeRecord record = root_dfpn.dfpn_probe_record(prefix_board.pos);
                    const ns_dfpn::ProbeRecord exact_record = root_dfpn.dfpn_probe_exact_record(prefix_board.pos);
                    const int prefix_depth = dump_prefix_depth
                        ? root_dfpn.pv_depth(prefix_board.pos, prefix_board.pos.turn() == root_board.pos.turn())
                        : -1;
                    std::cout << "  prefix " << i
                        << " turn=" << (prefix_board.pos.turn() == Black ? "b" : "w")
                        << " sfen=" << prefix_board.toSFEN()
                        << " pdp=" << record.proof_disproof.proof << ',' << record.proof_disproof.disproof
                        << " best=" << (record.best_move ? record.best_move.toUSI() : "-")
                        << " last=" << (record.last_move ? record.last_move.toUSI() : "-")
                        << " last_to=" << (record.last_to == SquareNum ? "-" : squareToStringUSI(record.last_to))
                        << " nodes=" << record.node_count
                        << " depth=" << prefix_depth
                        << " solved=0x" << std::hex << record.solved
                        << " dag=0x" << record.dag_moves
                        << std::dec
                        << " min_pdp=" << record.min_pdp
                        << " need_full=" << static_cast<unsigned>(record.need_full_width)
                        << " false_branch=" << (record.false_branch ? 1 : 0)
                        << " exact_pdp=" << exact_record.proof_disproof.proof << ',' << exact_record.proof_disproof.disproof
                        << " exact_best=" << (exact_record.best_move ? exact_record.best_move.toUSI() : "-")
                        << " exact_last=" << (exact_record.last_move ? exact_record.last_move.toUSI() : "-")
                        << " exact_nodes=" << exact_record.node_count
                        << " exact_solved=0x" << std::hex << exact_record.solved
                        << " exact_dag=0x" << exact_record.dag_moves
                        << std::dec
                        << " exact_min_pdp=" << exact_record.min_pdp
                        << " exact_need_full=" << static_cast<unsigned>(exact_record.need_full_width)
                        << "\n";
                    if (i == history.size()) {
                        break;
                    }
                    prefix_board.pos.doMove(history[i], prefix_states[i]);
                }
            }
            std::cout << "query_sfen=" << query_board.toSFEN() << "\n";
            const ns_dfpn::ProbeRecord query_record = root_dfpn.dfpn_probe_record(query_board.pos);
            Move query_record_best = query_record.best_move;
            const ProofDisproof query_pdp = query_record.proof_disproof;
            std::cout << "query_best=" << (query_record_best ? query_record_best.toUSI() : "-") << "\n";
                std::cout << "query_pdp=" << query_pdp.proof << ',' << query_pdp.disproof
                << " record_best=" << (query_record_best ? query_record_best.toUSI() : "-")
                << " last=" << (query_record.last_move ? query_record.last_move.toUSI() : "-")
                << " last_to=" << (query_record.last_to == SquareNum ? "-" : squareToStringUSI(query_record.last_to))
                << " nodes=" << query_record.node_count
                << " board_index=" << query_record.board_key
                << " board_secondary=" << query_record.board_secondary
                << " proof_set=" << query_record.proof_pieces_set
                << " proof=[" << show_hand(query_record.proof_pieces) << "]"
                << " black=[" << show_hand(query_record.black_stand) << "]"
                << " white=[" << show_hand(query_record.white_stand) << "]"
                << " solved=0x" << std::hex << query_record.solved
                << " dag=0x" << query_record.dag_moves
                << std::dec
                << " min_pdp=" << query_record.min_pdp
                << " need_full=" << static_cast<unsigned>(query_record.need_full_width)
                << " false_branch=" << (query_record.false_branch ? 1 : 0)
                << " dag_terminal=" << (query_record.dag_terminal ? 1 : 0)
                << "\n";
            if (query_only) {
                return 0;
            }
            const bool query_attack_node = (history.size() % 2) == 0;
            const Square query_delayed_to = !query_attack_node && query_record.last_to != query_board.pos.kingSquare(query_board.pos.turn())
                ? query_record.last_to
                : SquareNum;
            if (!query_attack_node) {
                std::cout << "query_delayed_to="
                    << (query_delayed_to == SquareNum ? "-" : squareToStringUSI(query_delayed_to))
                    << " delay=" << (is_delay_escape_node_like_osl(query_board.pos, query_delayed_to) ? 1 : 0)
                    << "\n";
            }
            std::vector<ExtMove> query_children =
                generate_like_dfpn_node_children(root_dfpn, query_board.pos, query_record, query_attack_node);
            std::cout << "query_children=" << query_children.size() << "\n";
            for (size_t child_index = 0; child_index < query_children.size(); ++child_index) {
                const Move move = query_children[child_index].move;
                if (child_filter && move.toUSI() != *child_filter) {
                    continue;
                }
                StateInfo st;
                query_board.pos.doMove(move, st);
                const auto child_board_key = static_cast<unsigned long long>(query_board.pos.getBoardKey());
                const auto child_key = static_cast<unsigned long long>(query_board.pos.getKey());
                const ns_dfpn::ProbeRecord child_record = root_dfpn.dfpn_probe_record(query_board.pos);
                const ns_dfpn::ProbeRecord child_exact_record = root_dfpn.dfpn_probe_exact_record(query_board.pos);
                Move child_best = child_record.best_move;
                const ProofDisproof child_pdp = child_record.proof_disproof;
                const int child_depth = root_dfpn.pv_depth(
                    query_board.pos,
                    query_board.pos.turn() == root_board.pos.turn());
                query_board.pos.undoMove(move);
                std::cout << "  child " << child_index << ' ' << move.toUSI()
                    << " pdp=" << child_pdp.proof << ',' << child_pdp.disproof
                    << " best=" << (child_best ? child_best.toUSI() : "-")
                    << " last=" << (child_record.last_move ? child_record.last_move.toUSI() : "-")
                    << " last_to=" << (child_record.last_to == SquareNum ? "-" : squareToStringUSI(child_record.last_to))
                    << " nodes=" << child_record.node_count
                    << " proof_set=" << child_record.proof_pieces_set
                    << " proof=[" << show_hand(child_record.proof_pieces) << "]"
                    << " black=[" << show_hand(child_record.black_stand) << "]"
                    << " white=[" << show_hand(child_record.white_stand) << "]"
                    << " solved=0x" << std::hex << child_record.solved
                    << " dag=0x" << child_record.dag_moves
                    << std::dec
                    << " min_pdp=" << child_record.min_pdp
                    << " need_full=" << static_cast<unsigned>(child_record.need_full_width)
                    << " false_branch=" << (child_record.false_branch ? 1 : 0)
                    << " dag_terminal=" << (child_record.dag_terminal ? 1 : 0)
                    << " depth=" << child_depth
                    << " board_index=" << child_record.board_key
                    << " board_secondary=" << child_record.board_secondary
                    << " board_key=" << child_board_key
                    << " key=" << child_key
                    << " exact_pdp=" << child_exact_record.proof_disproof.proof << ',' << child_exact_record.proof_disproof.disproof
                    << " exact_best=" << (child_exact_record.best_move ? child_exact_record.best_move.toUSI() : "-")
                    << " exact_last=" << (child_exact_record.last_move ? child_exact_record.last_move.toUSI() : "-")
                    << " exact_last_to=" << (child_exact_record.last_to == SquareNum ? "-" : squareToStringUSI(child_exact_record.last_to))
                    << " exact_nodes=" << child_exact_record.node_count
                    << " exact_solved=0x" << std::hex << child_exact_record.solved
                    << " exact_dag=0x" << child_exact_record.dag_moves
                    << std::dec
                    << " exact_min_pdp=" << child_exact_record.min_pdp
                    << " exact_need_full=" << static_cast<unsigned>(child_exact_record.need_full_width)
                    << "\n";
            }
            if (dump_bucket_after_search) {
                const std::vector<ns_dfpn::ProbeRecord> bucket = root_dfpn.debug_bucket_records(query_board.pos);
                std::cout << "query_bucket=" << bucket.size() << "\n";
                for (size_t i = 0; i < bucket.size(); ++i) {
                    const auto& record = bucket[i];
                    std::cout << "  bucket " << i
                        << " pdp=" << record.proof_disproof.proof << ',' << record.proof_disproof.disproof
                        << " best=" << (record.best_move ? record.best_move.toUSI() : "-")
                        << " last=" << (record.last_move ? record.last_move.toUSI() : "-")
                        << " nodes=" << record.node_count
                        << " proof_set=" << record.proof_pieces_set
                        << " proof=[" << show_hand(record.proof_pieces) << "]"
                        << " black=[" << show_hand(record.black_stand) << "]"
                        << " white=[" << show_hand(record.white_stand) << "]"
                        << " solved=0x" << std::hex << record.solved
                        << " dag=0x" << record.dag_moves
                        << std::dec
                        << " min_pdp=" << record.min_pdp
                        << " need_full=" << static_cast<unsigned>(record.need_full_width)
                        << " false_branch=" << (record.false_branch ? 1 : 0)
                        << " dag_terminal=" << (record.dag_terminal ? 1 : 0)
                        << "\n";
                }
            }
            if (root_result && !andnode) {
                std::vector<u32> pv;
                root_dfpn.get_pv(query_board.pos, query_board.pos.turn() == root_board.pos.turn(), pv);
                std::cout << "query_pv=";
                for (size_t i = 0; i < pv.size(); ++i) {
                    if (i) {
                        std::cout << ' ';
                    }
                    std::cout << Move(pv[i]).toUSI();
                }
                std::cout << "\n";
            }
            return 0;
        }

        const auto search_start = std::chrono::steady_clock::now();
        const bool result = andnode
            ? dfpn.dfpn_andnode(board.pos)
            : (use_path_history
                ? (threshold
                    ? dfpn.dfpn_with_history(root_board.pos, history, *threshold)
                    : dfpn.dfpn_with_history(root_board.pos, history))
                : dfpn.dfpn(board.pos));
        const auto search_end = std::chrono::steady_clock::now();
        std::cout << "result=" << result << " nodes=" << dfpn.searchedNode << "\n";
        std::cout << "search_ms="
            << std::chrono::duration_cast<std::chrono::milliseconds>(search_end - search_start).count()
            << "\n";
        const Move best = dfpn.dfpn_move(board.pos);
        std::cout << "best=" << (best ? best.toUSI() : "-") << "\n";
        Move record_best;
        const ProofDisproof record_pdp = dfpn.dfpn_probe(board.pos, &record_best);
        std::cout << "pdp=" << record_pdp.proof << ',' << record_pdp.disproof
            << " record_best=" << (record_best ? record_best.toUSI() : "-") << "\n";
        const ns_dfpn::ProbeRecord root_record_after_search = dfpn.dfpn_probe_record(board.pos);
        std::cout << "record_full=" << root_record_after_search.proof_disproof.proof << ','
            << root_record_after_search.proof_disproof.disproof
            << " best=" << (root_record_after_search.best_move ? root_record_after_search.best_move.toUSI() : "-")
            << " last=" << (root_record_after_search.last_move ? root_record_after_search.last_move.toUSI() : "-")
            << " nodes=" << root_record_after_search.node_count
            << " solved=0x" << std::hex << root_record_after_search.solved
            << " dag=0x" << root_record_after_search.dag_moves
            << std::dec
            << " min_pdp=" << root_record_after_search.min_pdp
            << " need_full=" << static_cast<unsigned>(root_record_after_search.need_full_width)
            << " false_branch=" << (root_record_after_search.false_branch ? 1 : 0)
            << " dag_terminal=" << (root_record_after_search.dag_terminal ? 1 : 0)
            << "\n";
        if (dump_children_after_search) {
            const ns_dfpn::ProbeRecord root_record = root_record_after_search;
            const bool root_attack_node = (history.size() % 2) == 0;
            std::vector<ExtMove> query_children =
                generate_like_dfpn_node_children(dfpn, board.pos, root_record, root_attack_node);
            std::cout << "children=" << query_children.size() << "\n";
            for (size_t child_index = 0; child_index < query_children.size(); ++child_index) {
                const Move move = query_children[child_index].move;
                StateInfo st;
                board.pos.doMove(move, st);
                const ns_dfpn::ProbeRecord child_record = dfpn.dfpn_probe_record(board.pos);
                const int child_depth = dfpn.pv_depth(board.pos, board.pos.turn() == root_board.pos.turn());
                board.pos.undoMove(move);
                std::cout << "  child " << child_index << ' ' << move.toUSI()
                    << " pdp=" << child_record.proof_disproof.proof << ',' << child_record.proof_disproof.disproof
                    << " best=" << (child_record.best_move ? child_record.best_move.toUSI() : "-")
                    << " last=" << (child_record.last_move ? child_record.last_move.toUSI() : "-")
                    << " nodes=" << child_record.node_count
                    << " solved=0x" << std::hex << child_record.solved
                    << " dag=0x" << child_record.dag_moves
                    << std::dec
                    << " min_pdp=" << child_record.min_pdp
                    << " need_full=" << static_cast<unsigned>(child_record.need_full_width)
                    << " false_branch=" << (child_record.false_branch ? 1 : 0)
                    << " dag_terminal=" << (child_record.dag_terminal ? 1 : 0)
                    << " depth=" << child_depth
                    << "\n";
            }
        }
        if (result && !andnode) {
            std::vector<u32> pv;
            const auto pv_start = std::chrono::steady_clock::now();
            dfpn.get_pv(board.pos, pv);
            const auto pv_end = std::chrono::steady_clock::now();
            std::cout << "pv_ms="
                << std::chrono::duration_cast<std::chrono::milliseconds>(pv_end - pv_start).count()
                << "\n";
            std::cout << "pv=";
            for (size_t i = 0; i < pv.size(); ++i) {
                if (i) {
                    std::cout << ' ';
                }
                std::cout << Move(pv[i]).toUSI();
            }
            std::cout << "\n";
        }
        return 0;
    }
    catch (const std::exception& ex) {
        std::cerr << "exception: " << ex.what() << "\n";
        return 1;
    }
}
