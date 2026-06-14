#include "dfpn.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <climits>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <forward_list>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif

#include "generateMoves.hpp"
#include "mt64bit.hpp"
#include "usi.hpp"

using ns_dfpn::ProofDisproof;

#ifndef CSHOGI_ENABLE_DFPN_PROFILE_CODE
#define CSHOGI_ENABLE_DFPN_PROFILE_CODE 0
#endif

namespace {
    constexpr size_t kMoveBufferSize = MaxLegalMoves;
    constexpr size_t kCheckOrEscapeMoveCapacity = 150;
    constexpr int kRepetitionWindow = 256;
    constexpr uint32_t kProofSimulationTolerance = 1024;
    constexpr uint32_t kOracleNodeLimit = 100000;
    constexpr uint32_t kRootSearchInitialNodeLimit = 256;
    constexpr uint32_t kRootSearchGeometricLimit = 4096;
    constexpr uint32_t kRootSearchArithmeticStep = 262144;
    constexpr int kUnknownDepth = std::numeric_limits<int>::max() / 4;
    constexpr uint32_t kInitialDominanceProofMax = 35;
    constexpr uint32_t kInitialDominanceDisproofMax = 110;
    constexpr uint32_t kDagFindThreshold = 64;
    constexpr uint32_t kDagFindThreshold2 = 256;
    constexpr size_t kMaxDagTraceDepth = 1600;
    constexpr uint32_t kNoPromoteIgnoreProofThreshold = 100;
    constexpr uint32_t kNoPromoteIgnoreDisproofThreshold = 200;
    constexpr int kEnableGCDepth = 512;
    constexpr uint32_t kIgnoreUpwardProofThreshold = 100;
    constexpr uint32_t kIgnoreUpwardDisproofThreshold = 100;
    constexpr uint32_t kRootProofTolerance = 65536u * 1024u;
    constexpr uint32_t kRootDisproofTolerance = 65536u * 1024u;
    constexpr uint32_t kAdHocSumScale = 128;
    constexpr uint32_t kSacrificeBlockCount = 0;
    constexpr uint32_t kLongDropCount = 1;
    // OSL computes the growth limit from sizeof(HashKey)+sizeof(DfpnRecord)+2 pointers.
    // Keep the OSL layout size here; the ported record has different padding.
    constexpr uint64_t kOslmateHashGrowthUnitSize = 104;
    constexpr bool kEnableNagaiDagTest = true;
    constexpr bool kEnableFixedDepthShortcut = true;
    constexpr bool kEnableProofOracleShortcut = true;
    // OSL defines CHECKMATE_A3_SIMULLATION before the CHECKMATE_D2 branch.
    constexpr bool kEnableProofOracleAttackFixedDepthShortcut = true;
    constexpr bool kEnableKishimotoWidenThreshold = true;
    constexpr bool kEnableBlockingVerify = true;
    constexpr bool kEnableBlockingSimulation = true;
    constexpr bool kEnableGrandParentSimulation = true;
    constexpr bool kEnableReleaseZukouTrace = false;

    template <size_t Capacity>
    class FixedMoveVector {
    public:
        using iterator = Move*;
        using const_iterator = const Move*;

        void clear() { size_ = 0; }
        bool empty() const { return size_ == 0; }
        size_t size() const { return size_; }

        iterator begin() { return moves_.data(); }
        iterator end() { return moves_.data() + size_; }
        const_iterator begin() const { return moves_.data(); }
        const_iterator end() const { return moves_.data() + size_; }

        Move& operator[](const size_t index) { return moves_[index]; }
        const Move& operator[](const size_t index) const { return moves_[index]; }

        void push_back(const Move move) {
            assert(size_ < Capacity);
            moves_[size_++] = move;
        }

    private:
        std::array<Move, Capacity> moves_;
        size_t size_ = 0;
    };

    double osl_memory_use_ratio();

    uint32_t next_root_search_limit(const uint32_t current, const uint32_t max_search_node) {
        if (current >= max_search_node) {
            return max_search_node;
        }
        uint64_t next = 0;
        if (current < kRootSearchGeometricLimit) {
            next = static_cast<uint64_t>(current) * 4;
        }
        else if (current < kRootSearchArithmeticStep) {
            next = static_cast<uint64_t>(current) * 2;
        }
        else {
            next = static_cast<uint64_t>(current) + kRootSearchArithmeticStep;
        }
        return static_cast<uint32_t>(std::min<uint64_t>(next, max_search_node));
    }

    struct OslHashKey128Layout {
        uint64_t board64;
        uint32_t board32;
        uint32_t piece_stand;
    };

    const std::array<std::array<OslHashKey128Layout, 32>, 256>& osl_hash_gen_table() {
        static const std::array<std::array<OslHashKey128Layout, 32>, 256> table = { {
#include "../oslmate/osl/osl/core/osl/bits/hash.txt"
        } };
        return table;
    }

    int osl_square_index_for_hash(const Square square) {
        if (!isInSquare(square)) {
            return 0; // Square::STAND()
        }
        return (static_cast<int>(makeFile(square)) + 1) * 16
            + (static_cast<int>(makeRank(square)) + 1) + 1;
    }

    int osl_ptype_index_for_hash(const PieceType piece_type) {
        switch (piece_type) {
        case Pawn: return 10;
        case Lance: return 11;
        case Knight: return 12;
        case Silver: return 13;
        case Bishop: return 14;
        case Rook: return 15;
        case Gold: return 9;
        case King: return 8;
        case ProPawn: return 2;
        case ProLance: return 3;
        case ProKnight: return 4;
        case ProSilver: return 5;
        case Horse: return 6;
        case Dragon: return 7;
        default: return 0;
        }
    }

    int osl_ptypeo_index_for_hash(const Color color, const PieceType piece_type) {
        const int ptype = osl_ptype_index_for_hash(piece_type);
        // OSL PtypeO index is ptypeO - PTYPEO_MIN.  BLACK ptypeO is ptype,
        // WHITE ptypeO is ptype - 16, and PTYPEO_MIN is -16.
        return color == Black ? ptype + 16 : ptype;
    }

    const OslHashKey128Layout& osl_hash_seed(const Square square, const Color color, const PieceType piece_type) {
        return osl_hash_gen_table()[static_cast<size_t>(osl_square_index_for_hash(square))]
            [static_cast<size_t>(osl_ptypeo_index_for_hash(color, piece_type))];
    }

    const OslHashKey128Layout& osl_hash_seed_direct(const int square_index, const int ptypeo_index) {
        return osl_hash_gen_table()[static_cast<size_t>(square_index)][static_cast<size_t>(ptypeo_index)];
    }

    inline void osl_add_hash(uint64_t& board64, uint32_t& board32,
        const Square square, const Color color, const PieceType piece_type) {
        const OslHashKey128Layout& key = osl_hash_seed(square, color, piece_type);
        board64 += key.board64;
        board32 += key.board32;
    }

    inline void osl_sub_hash(uint64_t& board64, uint32_t& board32,
        const Square square, const Color color, const PieceType piece_type) {
        const OslHashKey128Layout& key = osl_hash_seed(square, color, piece_type);
        board64 -= key.board64;
        board32 -= key.board32;
    }

    inline void osl_add_hash_direct(uint64_t& board64, uint32_t& board32,
        const int square_index, const int ptypeo_index) {
        const OslHashKey128Layout& key = osl_hash_seed_direct(square_index, ptypeo_index);
        board64 += key.board64;
        board32 += key.board32;
    }

    inline void osl_sub_hash_direct(uint64_t& board64, uint32_t& board32,
        const int square_index, const int ptypeo_index) {
        const OslHashKey128Layout& key = osl_hash_seed_direct(square_index, ptypeo_index);
        board64 -= key.board64;
        board32 -= key.board32;
    }

    PieceType osl_captured_ptype(const PieceType captured) {
        switch (captured) {
        case ProPawn: return Pawn;
        case ProLance: return Lance;
        case ProKnight: return Knight;
        case ProSilver: return Silver;
        case Horse: return Bishop;
        case Dragon: return Rook;
        default: return captured;
        }
    }

    template <typename Callback>
    void for_each_osl_stand_piece(const Hand& hand, Callback&& callback) {
        static constexpr std::array<std::pair<HandPiece, PieceType>, 7> pieces = { {
            { HPawn, Pawn }, { HLance, Lance }, { HKnight, Knight }, { HSilver, Silver },
            { HGold, Gold }, { HBishop, Bishop }, { HRook, Rook }
        } };
        for (const auto& [hand_piece, piece_type] : pieces) {
            const int count = static_cast<int>(hand.numOf(hand_piece));
            for (int i = 0; i < count; ++i) {
                callback(piece_type);
            }
        }
    }

    uint64_t osl_board64_key(const Position& pos) {
        uint64_t board64 = 0;
        uint32_t board32 = 0;
        for (int sq_value = 0; sq_value < static_cast<int>(SquareNum); ++sq_value) {
            const Square square = static_cast<Square>(sq_value);
            const Piece piece = pos.piece(square);
            if (piece == Empty) {
                continue;
            }
            osl_add_hash(board64, board32, square, pieceToColor(piece), pieceToPieceType(piece));
        }
        for_each_osl_stand_piece(pos.hand(Black), [&](const PieceType piece_type) {
            osl_add_hash(board64, board32, SquareNum, Black, piece_type);
        });
        for_each_osl_stand_piece(pos.hand(White), [&](const PieceType piece_type) {
            osl_add_hash(board64, board32, SquareNum, White, piece_type);
        });
        board64 &= ~uint64_t{ 1 };
        board64 |= (pos.turn() == Black ? 0ull : 1ull);
        return board64;
    }

    uint32_t osl_board32_key(const Position& pos) {
        uint64_t board64 = 0;
        uint32_t board32 = 0;
        for (int sq_value = 0; sq_value < static_cast<int>(SquareNum); ++sq_value) {
            const Square square = static_cast<Square>(sq_value);
            const Piece piece = pos.piece(square);
            if (piece == Empty) {
                continue;
            }
            osl_add_hash(board64, board32, square, pieceToColor(piece), pieceToPieceType(piece));
        }
        for_each_osl_stand_piece(pos.hand(Black), [&](const PieceType piece_type) {
            osl_add_hash(board64, board32, SquareNum, Black, piece_type);
        });
        for_each_osl_stand_piece(pos.hand(White), [&](const PieceType piece_type) {
            osl_add_hash(board64, board32, SquareNum, White, piece_type);
        });
        return board32;
    }

    inline Key board_index_key(const Position& pos) {
        return static_cast<Key>(osl_board64_key(pos));
    }

    uint64_t splitmix64_value(uint64_t x) {
        x += 0x9e3779b97f4a7c15ull;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
        return x ^ (x >> 31);
    }

    const std::array<std::array<std::array<uint64_t, ColorNum>, SquareNum>, PieceTypeNum>& secondary_piece_keys() {
        static const auto keys = []() {
            std::array<std::array<std::array<uint64_t, ColorNum>, SquareNum>, PieceTypeNum> result{};
            for (int pt = 0; pt < static_cast<int>(PieceTypeNum); ++pt) {
                for (int sq = 0; sq < static_cast<int>(SquareNum); ++sq) {
                    for (int color = 0; color < static_cast<int>(ColorNum); ++color) {
                        result[pt][sq][color] = splitmix64_value((static_cast<uint64_t>(pt) << 16)
                            ^ (static_cast<uint64_t>(sq) << 1)
                            ^ static_cast<uint64_t>(color)
                            ^ 0x6a09e667f3bcc909ull);
                    }
                }
            }
            return result;
        }();
        return keys;
    }

    inline uint64_t secondary_piece_key(const PieceType pt, const Square sq, const Color color) {
        return secondary_piece_keys()[static_cast<int>(pt)][static_cast<int>(sq)][static_cast<int>(color)];
    }

    inline uint64_t secondary_turn_key() {
        return 0xbb67ae8584caa73bull;
    }

    uint64_t secondary_board_key(const Position& pos) {
        return static_cast<uint64_t>(osl_board32_key(pos));
    }

    uint64_t secondary_board_key_after_move(uint64_t key, const Color mover, const Move move) {
        uint64_t board64 = 0;
        uint32_t board32 = static_cast<uint32_t>(key);
        if (!move) {
            // OSL's default Move() is INVALID_VALUE=(1<<8), not pass.  In release
            // newHashWithMove applies that bit pattern with asserts disabled.
            constexpr int kOslInvalidFromSquare = 1;
            constexpr int kOslInvalidToSquare = 0;
            constexpr int kOslBlackEmptyPtypeOIndex = 16; // PTYPEO_EMPTY - PTYPEO_MIN
            osl_sub_hash_direct(board64, board32, kOslInvalidFromSquare, kOslBlackEmptyPtypeOIndex);
            osl_add_hash_direct(board64, board32, kOslInvalidToSquare, kOslBlackEmptyPtypeOIndex);
            return static_cast<uint64_t>(board32);
        }
        if (!move.isDrop()) {
            const PieceType old_type = move.pieceTypeFrom();
            const PieceType new_type = move.pieceTypeTo();
            if (move.isCapture()) {
                const PieceType captured = move.cap();
                osl_sub_hash(board64, board32, move.to(), oppositeColor(mover), captured);
                osl_add_hash(board64, board32, SquareNum, mover, osl_captured_ptype(captured));
            }
            osl_sub_hash(board64, board32, move.from(), mover, old_type);
            osl_add_hash(board64, board32, move.to(), mover, new_type);
        }
        else {
            const PieceType dropped = move.pieceTypeDropped();
            osl_sub_hash(board64, board32, SquareNum, mover, dropped);
            osl_add_hash(board64, board32, move.to(), mover, dropped);
        }
        return static_cast<uint64_t>(board32);
    }

    uint64_t board_index_key_after_move(uint64_t key, const Color mover, const Move move) {
        uint64_t board64 = key;
        uint32_t board32 = 0;
        if (!move) {
            constexpr int kOslInvalidFromSquare = 1;
            constexpr int kOslInvalidToSquare = 0;
            constexpr int kOslBlackEmptyPtypeOIndex = 16;
            osl_sub_hash_direct(board64, board32, kOslInvalidFromSquare, kOslBlackEmptyPtypeOIndex);
            osl_add_hash_direct(board64, board32, kOslInvalidToSquare, kOslBlackEmptyPtypeOIndex);
            board64 ^= uint64_t{ 1 };
            return board64;
        }
        if (!move.isDrop()) {
            const PieceType old_type = move.pieceTypeFrom();
            const PieceType new_type = move.pieceTypeTo();
            if (move.isCapture()) {
                const PieceType captured = move.cap();
                osl_sub_hash(board64, board32, move.to(), oppositeColor(mover), captured);
                osl_add_hash(board64, board32, SquareNum, mover, osl_captured_ptype(captured));
            }
            osl_sub_hash(board64, board32, move.from(), mover, old_type);
            osl_add_hash(board64, board32, move.to(), mover, new_type);
        }
        else {
            const PieceType dropped = move.pieceTypeDropped();
            osl_sub_hash(board64, board32, SquareNum, mover, dropped);
            osl_add_hash(board64, board32, move.to(), mover, dropped);
        }
        board64 ^= uint64_t{ 1 };
        return board64;
    }

    struct OslBoardKeyParts {
        Key board_index = 0;
        uint64_t board_secondary = 0;
    };

    OslBoardKeyParts board_keys_after_move(
        const uint64_t board_index_key,
        const uint64_t board_secondary_key,
        const Color mover,
        const Move move) {
        uint64_t board64 = board_index_key;
        uint32_t board32 = static_cast<uint32_t>(board_secondary_key);
        if (!move) {
            constexpr int kOslInvalidFromSquare = 1;
            constexpr int kOslInvalidToSquare = 0;
            constexpr int kOslBlackEmptyPtypeOIndex = 16;
            osl_sub_hash_direct(board64, board32, kOslInvalidFromSquare, kOslBlackEmptyPtypeOIndex);
            osl_add_hash_direct(board64, board32, kOslInvalidToSquare, kOslBlackEmptyPtypeOIndex);
            board64 ^= uint64_t{ 1 };
            return { static_cast<Key>(board64), static_cast<uint64_t>(board32) };
        }
        if (!move.isDrop()) {
            const PieceType old_type = move.pieceTypeFrom();
            const PieceType new_type = move.pieceTypeTo();
            if (move.isCapture()) {
                const PieceType captured = move.cap();
                osl_sub_hash(board64, board32, move.to(), oppositeColor(mover), captured);
                osl_add_hash(board64, board32, SquareNum, mover, osl_captured_ptype(captured));
            }
            osl_sub_hash(board64, board32, move.from(), mover, old_type);
            osl_add_hash(board64, board32, move.to(), mover, new_type);
        }
        else {
            const PieceType dropped = move.pieceTypeDropped();
            osl_sub_hash(board64, board32, SquareNum, mover, dropped);
            osl_add_hash(board64, board32, move.to(), mover, dropped);
        }
        board64 ^= uint64_t{ 1 };
        return { static_cast<Key>(board64), static_cast<uint64_t>(board32) };
    }

    uint64_t board_index_key_before_move(uint64_t key, const Color mover, const Move move) {
        uint64_t board64 = key;
        uint32_t board32 = 0;
        if (!move.isDrop()) {
            const PieceType old_type = move.pieceTypeFrom();
            const PieceType new_type = move.pieceTypeTo();
            if (move.isCapture()) {
                const PieceType captured = move.cap();
                osl_add_hash(board64, board32, move.to(), oppositeColor(mover), captured);
                osl_sub_hash(board64, board32, SquareNum, mover, osl_captured_ptype(captured));
            }
            osl_add_hash(board64, board32, move.from(), mover, old_type);
            osl_sub_hash(board64, board32, move.to(), mover, new_type);
        }
        else {
            const PieceType dropped = move.pieceTypeDropped();
            osl_add_hash(board64, board32, SquareNum, mover, dropped);
            osl_sub_hash(board64, board32, move.to(), mover, dropped);
        }
        board64 ^= uint64_t{ 1 };
        return board64;
    }

    uint64_t secondary_board_key_before_move(uint64_t key, const Color mover, const Move move) {
        uint64_t board64 = 0;
        uint32_t board32 = static_cast<uint32_t>(key);
        if (!move.isDrop()) {
            const PieceType old_type = move.pieceTypeFrom();
            const PieceType new_type = move.pieceTypeTo();
            if (move.isCapture()) {
                const PieceType captured = move.cap();
                osl_add_hash(board64, board32, move.to(), oppositeColor(mover), captured);
                osl_sub_hash(board64, board32, SquareNum, mover, osl_captured_ptype(captured));
            }
            osl_add_hash(board64, board32, move.from(), mover, old_type);
            osl_sub_hash(board64, board32, move.to(), mover, new_type);
        }
        else {
            const PieceType dropped = move.pieceTypeDropped();
            osl_add_hash(board64, board32, SquareNum, mover, dropped);
            osl_sub_hash(board64, board32, move.to(), mover, dropped);
        }
        return static_cast<uint64_t>(board32);
    }
    struct OslmatePositionKey {
        Key board_key = 0;
        uint64_t board_secondary = 0;
        Hand black_stand = Hand(0);

        bool operator==(const OslmatePositionKey& other) const {
            return board_key == other.board_key
                && board_secondary == other.board_secondary
                && black_stand == other.black_stand;
        }

        bool operator!=(const OslmatePositionKey& other) const {
            return !(*this == other);
        }
    };

    OslmatePositionKey oslmate_position_key(const Position& pos) {
        return { board_index_key(pos), secondary_board_key(pos), pos.hand(Black) };
    }

    struct OslmateBoardKey {
        Key board_key = 0;
        uint64_t board_secondary = 0;

        bool operator==(const OslmateBoardKey& other) const {
            // OSL HashKey::boardKey() is BoardKey96: (board64, board32).
            return board_key == other.board_key
                && board_secondary == other.board_secondary;
        }
    };

    struct OslmateBoardKeyHash {
        size_t operator()(const OslmateBoardKey& key) const {
            // OSL std::hash<BoardKey> returns BoardKey96::signature(), i.e. board32.
            return static_cast<size_t>(static_cast<uint32_t>(key.board_secondary));
        }
    };

    inline OslmateBoardKey make_oslmate_board_key(const Position& pos) {
        return { board_index_key(pos), secondary_board_key(pos) };
    }

    inline OslmateBoardKey make_oslmate_board_key(const Key board_index, const uint64_t board_secondary) {
        return { board_index, board_secondary };
    }

    struct Threshold {
        uint32_t proof;
        uint32_t disproof;
    };

    enum class ProofPiecesType : int8_t {
        Unset = 0,
        Proof,
        Disproof,
    };

    struct PathRecord;
    class OslPieceNumberState;

    struct ChildState {
        ChildState() noexcept {}

        void reset_for_move(const Move move_) {
            move = move_;
            path_record = nullptr;
            proof_cost = 0;
        }

        Move move;
        ProofDisproof pdp;
        Move best_reply;
        Move last_move;
        Hand proof_pieces;
        ProofPiecesType proof_pieces_type;
        uint32_t node_count;
        uint32_t tried_oracle;
        uint32_t min_pdp;
        uint32_t working_threads;
        uint16_t remaining_depth;
        uint64_t solved;
        uint64_t dag_moves;
        Hand proof_pieces_candidate;
        Square last_to;
        bool false_branch;
        bool dag_terminal;
        bool exact;
        int8_t proof_cost;
        uint8_t need_full_width;
        Key board_index;
        uint64_t board_secondary;
        std::array<Hand, ColorNum> stands;
        const PathRecord* path_record;
    };

    struct LibertyInfo {
        uint8_t mask = 0;
        uint8_t count = 0;
    };

    struct King8RuntimeInfo {
        uint8_t drop_candidate = 0;
        uint8_t liberty = 0;
        uint8_t liberty_candidate = 0;
        uint8_t move_candidate2 = 0;
        uint8_t spaces = 0;
        uint8_t moves = 0;
        uint8_t liberty_count = 0;

        uint16_t liberty_drop_mask() const {
            return static_cast<uint16_t>(drop_candidate) | (static_cast<uint16_t>(liberty) << 8);
        }
    };

    struct LibertyEstimate {
        uint8_t liberty = 0;
        bool has_effect = false;
    };

    constexpr std::array<PieceType, 7> kStandPieceTypes = {
        Gold, Pawn, Lance, Knight, Silver, Bishop, Rook
    };

    // OSL ProofNumberTable::init() enumerates PTYPE_BASIC_MIN..PTYPE_MAX:
    // KING, GOLD, PAWN, LANCE, KNIGHT, SILVER, BISHOP, ROOK.
    constexpr std::array<PieceType, 7> kMoveLibertyPieceTypes = {
        Gold, Pawn, Lance, Knight, Silver, Bishop, Rook
    };

    struct ProofNumberRuntimeTables {
        std::array<std::array<std::array<std::array<LibertyEstimate, 8>, PieceTypeNum>, 0x100>, 2> short_liberties{};
        std::array<std::array<std::array<std::array<LibertyEstimate, 8>, PieceTypeNum>, 0x100>, 2> long_liberties{};
        std::array<std::array<std::array<uint8_t, 8>, 0x10000>, 2> drop_liberty{};
        std::array<std::array<std::array<uint8_t, 0x100>, 0x100>, 2> pmajor_liberty{};
        std::array<std::array<std::array<uint8_t, 0x100>, 0x100>, 2> promote_liberty{};
        std::array<std::array<std::array<uint8_t, 0x100>, 0x100>, 2> other_move_liberty{};

        ProofNumberRuntimeTables();
    };
    const ProofNumberRuntimeTables& proof_number_runtime_tables();
    uint8_t stand_piece_mask(const Position& pos, Color attack_color);

    Hand zero_hand();
    int hand_count(const Hand& hand, HandPiece hp);
    bool is_osl_normal_move(Move move);
    PieceType osl_move_ptype(const Position& pos, Move move);
    Move validated_mate_move_in_1(Position& pos);
    Move immediate_mate_move_in_1_osl(Position& pos, const OslPieceNumberState* current_piece_numbers = nullptr);
    ProofDisproof fixed_attack_osl_shortcut(Position& pos, Color attack_color, Move* best_move = nullptr,
        Hand* proof_pieces = nullptr, OslPieceNumberState* current_piece_numbers = nullptr);
    Hand proof_pieces_leaf(const Position& pos, const Color attack_color, const Hand& max);
    Hand fixed_attack_leaf_proof_pieces(Move best_move);
    Hand proof_pieces_after_attack(const Hand& prev, const Move move, const Hand& max);
    Move complete_move_for_position(const Position& pos, Move move);
    void generate_check_moves(Position& pos, std::vector<Move>& moves, bool* has_pawn_checkmate = nullptr,
        const Position* root_position = nullptr, const std::vector<Move>* move_history = nullptr,
        const OslPieceNumberState* current_piece_numbers = nullptr);
    std::vector<Move> generate_check_moves(Position& pos, bool* has_pawn_checkmate = nullptr);
    std::vector<Move> generate_fixed_depth_check_moves(Position& pos, bool* has_pawn_checkmate = nullptr,
        const OslPieceNumberState* current_piece_numbers = nullptr);
    template <class MoveContainer>
    void sort_moves(const Position& pos, Color turn, MoveContainer& moves);
    void generate_escape_moves(Position& pos, std::vector<Move>& moves, bool need_full_width = true, Square last_to = SquareNum,
        const OslPieceNumberState* current_piece_numbers = nullptr);
    std::vector<Move> generate_escape_moves(Position& pos, bool need_full_width = true, Square last_to = SquareNum,
        const OslPieceNumberState* current_piece_numbers = nullptr);
    template <class MoveContainer>
    void reorder_osl_numbered_escape_target_moves_impl(
        const OslPieceNumberState* current_piece_numbers,
        MoveContainer& moves);
    void reorder_osl_numbered_escape_target_moves(const OslPieceNumberState* current_piece_numbers, std::vector<Move>& moves);
    std::vector<Move> generate_all_legal_evasion_moves(Position& pos);
    bool has_ignored_unpromote_escape(Move move, Color defender);
    bool has_ignored_unpromote_check(Move move, Color attacker);
    bool is_ignored_unpromote_check_variant(Move move, Color attacker);
    bool unpromoted_piece_has_effect_to_king(const Position& pos, Move move, Color attacker);
    bool osl_pin_or_open_shadow(const Position& pos, Color attacker, Square blocker);
    bool is_legal_check_move(Position& pos, Move move);
    bool should_append_ignored_unpromote_check(const Position& pos, Move promoted, Move unpromoted, Color attacker);
    Move unpromote_counterpart(const Position& pos, Move promoted);
    Hand hand_max(const Hand& lhs, const Hand& rhs);
    void add_monopolized_pieces(const Hand& us, const Hand& them, const Hand& max, Hand& out);
    bool has_unblockable_effect_to_king(PieceType piece_type, Color attack_color, Square from, Square king);
    bool is_unblockable_check(const Position& pos);
    std::optional<Square> offset_square(Square sq, int file_delta, int rank_delta);
    int orient_for_attacker(Color attacker, int delta);
    Bitboard pinned_pieces_of(const Position& pos, Color pinned_color);
    int dir_index_from_delta(Color attacker, int file_delta, int rank_delta);
    std::optional<Square> king8_square(Square king, int dir_index, Color attack_color);
    King8RuntimeInfo make_king8_runtime_info(const Position& pos, Color attack_color);
    King8RuntimeInfo king8_runtime_info_at(const Position& pos, Color attack_color);
    King8RuntimeInfo reset_edge_from_liberty_runtime(Square king, Color attack_color, King8RuntimeInfo info);
    int move_candidate_mask_runtime(const Position& pos, Color attack_color, Square king, const King8RuntimeInfo& info);
    LibertyInfo defender_king_liberty_info(const Position& pos, const Color attack_color);
    ProofDisproof estimate_attack_pdp(const Position& pos, const Color attacker, const King8RuntimeInfo& king8_info, const Move move);
    ProofDisproof attack_estimation_zero(const Position& pos, const Color attack_color);
    ProofDisproof fixed_attack_estimation_zero(const Position& pos, const Color attack_color);
    LibertyEstimate effective_check_short(PieceType piece_type, int dir_index, uint8_t liberty_mask, Color attacker);
    unsigned int immediate_no_effect_mask(PieceType piece_type, int dir_index);
    bool immediate_candidate_preserves_liberty_candidate_effect(const Position& pos, Color attack_color,
        Square target_king, Move move, const King8RuntimeInfo& king8_info, int dir_index);
    bool immediate_opponent_long_effect_follows(const Position& pos, Color attack_color, Move move);
    bool immediate_king_open_move(const Position& pos, Color attack_color, Move move, const Bitboard& pinned_attack);
    int immediate_osl_piece_number_from_position(const Position& pos, Square square);
    int immediate_osl_piece_number_from_state_or_position(
        const Position& pos, Square square, const OslPieceNumberState* current_piece_numbers);
    bool osl_piece_number_info(
        const OslPieceNumberState* current_piece_numbers, int number, Color* owner, Square* square);
    bool immediate_can_checkmate_drop_ptype(PieceType piece_type, int dir_index, uint8_t liberty_mask);
    bool immediate_slow_drop_ok(const Position& pos, Color attack_color,
        Square target_king, PieceType piece_type, int dir_index, const King8RuntimeInfo& king8_info);
    Move immediate_mate_move_by_osl_move_candidates(Position& pos, Color attack_color,
        Square target_king, const King8RuntimeInfo& king8_info, bool fixed_probe_enabled,
        const OslPieceNumberState* current_piece_numbers = nullptr);
    bool immediate_reject_prook_false_positive(const Position& pos, Color attack_color, Square target_king, Move move);
    inline Bitboard effect_set_at(const Position& pos, const Color c, const Square sq);
    inline bool effect_has_at(const Position& pos, const Color c, const Square sq);
    inline int effect_count(const Position& pos, const Color c, const Square sq);
    inline bool has_multiple_effect_at(const Position& pos, Color c, Square sq);
    inline uint32_t saturate_sum(const uint64_t value, const uint32_t limit);
    int osl_king_escape_order_key(Color defense_color, Square from, Move move);
    bool is_blockable_single_check(const Position& pos, Square checker, Square defense_king);
    uint64_t osl_system_memory_use_limit() {
#ifdef _WIN32
        MEMORYSTATUSEX statex{};
        statex.dwLength = sizeof(statex);
        if (GlobalMemoryStatusEx(&statex)) {
            return static_cast<uint64_t>(statex.ullTotalPhys);
        }
        return 0;
#else
        std::ifstream is("/proc/meminfo");
        std::string name;
        uint64_t value = 0;
        std::string unit;
        if (is >> name >> value >> unit && name == "MemTotal:" && unit == "kB") {
            return value * 1024ull;
        }
        return 0;
#endif
    }

    uint64_t osl_resident_memory_use() {
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS pmc{};
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
            return static_cast<uint64_t>(pmc.WorkingSetSize);
        }
        return 0;
#else
        std::ifstream is("/proc/self/statm");
        uint64_t total = 0;
        uint64_t resident = 0;
        if (is >> total >> resident) {
            return resident * 4096ull;
        }
        return 0;
#endif
    }

    double osl_memory_use_ratio() {
        static const uint64_t memory_limit = osl_system_memory_use_limit();
        if (memory_limit == 0) {
            return 0.0;
        }
        return static_cast<double>(osl_resident_memory_use()) / static_cast<double>(memory_limit);
    }
    struct ImmediateMateOrderContext {
        Color attack_color = Black;
        Square target_king = SquareNum;
        int move_candidate_mask = 0;
        uint8_t drop_candidate_mask = 0;
    };

    int immediate_mate_dir_index(const Move move, const ImmediateMateOrderContext& ctx) {
        const int file_delta = static_cast<int>(makeFile(ctx.target_king)) - static_cast<int>(makeFile(move.to()));
        const int rank_delta = static_cast<int>(makeRank(ctx.target_king)) - static_cast<int>(makeRank(move.to()));
        if ((file_delta == 0 && rank_delta == 0)
            || std::abs(file_delta) > 1
            || std::abs(rank_delta) > 1) {
            return -1;
        }
        return dir_index_from_delta(
            ctx.attack_color,
            file_delta == 0 ? 0 : (file_delta > 0 ? 1 : -1),
            rank_delta == 0 ? 0 : (rank_delta > 0 ? 1 : -1));
    }

    uint32_t immediate_mate_order_key(const Move move, const ImmediateMateOrderContext& ctx) {
        const int dir_index = immediate_mate_dir_index(move, ctx);
        const uint32_t dir_key = dir_index >= 0 ? static_cast<uint32_t>(dir_index) : 0xffu;

        if (!move.isDrop() && move.pieceTypeFrom() != Knight) {
            if (dir_index >= 0 && (ctx.move_candidate_mask & (1 << dir_index)) != 0) {
                return dir_key;
            }
            return 0x100u + dir_key;
        }

        if (!move.isDrop()) {
            return 0x200u + dir_key;
        }

        const auto osl_drop_piece_key = [](const PieceType piece_type) -> uint32_t {
            switch (piece_type) {
            case Gold: return 1;
            case Pawn: return 2;
            case Lance: return 3;
            case Knight: return 4;
            case Silver: return 5;
            case Bishop: return 6;
            case Rook: return 7;
            default: return 0xff;
            }
        };
        const uint32_t piece_key = osl_drop_piece_key(move.pieceTypeDropped());
        if (dir_index >= 0 && (ctx.drop_candidate_mask & (1u << dir_index)) != 0) {
            return 0x300u + (piece_key << 4) + dir_key;
        }
        return 0x400u + (piece_key << 4) + dir_key;
    }

    int osl_king_escape_order_key(const Color defense_color, const Square from, const Move move) {
        if (move.isDrop() || move.pieceTypeFrom() != King) {
            return INT_MAX;
        }

        const int file_delta = static_cast<int>(makeFile(move.to())) - static_cast<int>(makeFile(from));
        const int rank_delta = static_cast<int>(makeRank(move.to())) - static_cast<int>(makeRank(from));
        static constexpr std::array<std::pair<int, int>, 8> kBlackOrder = { {
            { 1, -1 }, { -1, 1 }, { 0, -1 }, { 0, 1 },
            { -1, -1 }, { 1, 1 }, { 1, 0 }, { -1, 0 }
        } };
        static constexpr std::array<std::pair<int, int>, 8> kWhiteOrder = { {
            { 1, -1 }, { -1, 1 }, { 0, 1 }, { 0, -1 },
            { 1, 1 }, { -1, -1 }, { -1, 0 }, { 1, 0 }
        } };
        const auto& order = defense_color == Black ? kBlackOrder : kWhiteOrder;
        for (size_t i = 0; i < order.size(); ++i) {
            if (order[i].first == file_delta && order[i].second == rank_delta) {
                return static_cast<int>(i);
            }
        }
        return INT_MAX;
    }

    bool is_blockable_single_check(const Position& pos, const Square checker, const Square defense_king) {
        const PieceType checker_type = pieceToPieceType(pos.piece(checker));
        const Direction relation = squareRelation(checker, defense_king);
        const int distance = squareDistance(checker, defense_king);
        if (distance <= 1) {
            return false;
        }

        switch (checker_type) {
        case Bishop:
        case Horse:
            return relation == DirecDiagNESW || relation == DirecDiagNWSE;
        case Rook:
        case Dragon:
            return relation == DirecFile || relation == DirecRank;
        case Lance:
            return relation == DirecFile;
        default:
            return false;
        }
    }

    ProofDisproof fixed_escape_by_move_zero(Position& pos, const Color attack_color, Move* best_move = nullptr,
        Hand* proof_pieces = nullptr, const bool no_proof_pieces_attack_estimation = false,
        const bool may_unsafe = true, const OslPieceNumberState* current_piece_numbers = nullptr) {
        if (best_move) {
            *best_move = Move::moveNone();
        }
        if (proof_pieces) {
            *proof_pieces = zero_hand();
        }

        const Square target_king = pos.kingSquare(oppositeColor(attack_color));
        const bool still_checking = target_king && effect_has_at(pos, attack_color, target_king);
        if (may_unsafe && still_checking) {
            return ProofDisproof::NoEscape();
        }
        if (!pos.inCheck()) {
            const Move mate1 = immediate_mate_move_in_1_osl(pos, current_piece_numbers);
            if (mate1) {
                if (best_move) {
                    *best_move = mate1;
                }
                if (proof_pieces) {
                    *proof_pieces = fixed_attack_leaf_proof_pieces(mate1);
                }
                return ProofDisproof::Checkmate();
            }
        }

        const ProofDisproof result = no_proof_pieces_attack_estimation
            ? ProofDisproof::Unknown()
            : [&]() {
                return fixed_attack_estimation_zero(pos, attack_color);
            }();
        return result;
    }

    ProofDisproof fixed_has_escape_by_move_zero(Position& pos, const Color attack_color, Move* best_move = nullptr,
        Hand* proof_pieces = nullptr, const Move last_attack_move = Move::moveNone(),
        const bool no_proof_pieces_attack_estimation = false, const bool may_unsafe_attack = true,
        const OslPieceNumberState* current_piece_numbers = nullptr) {
        if (best_move) {
            *best_move = Move::moveNone();
        }
        if (proof_pieces) {
            *proof_pieces = zero_hand();
        }

        const Color defense_color = oppositeColor(attack_color);
        const Square attacker_king = pos.kingSquare(attack_color);
        if (attacker_king && effect_has_at(pos, defense_color, attacker_king)) {
            return ProofDisproof::NoCheckmate();
        }

        std::array<ExtMove, kMoveBufferSize> buffer;
        ExtMove* last = generateOslmateEscapeNonblockMoves(buffer.data(), pos, false);
        FixedMoveVector<kCheckOrEscapeMoveCapacity> moves;
        FixedMoveVector<kCheckOrEscapeMoveCapacity> king_moves;
        FixedMoveVector<kCheckOrEscapeMoveCapacity> raw_blocking_moves;
        bool raw_blocking_collected = false;

        const Square defense_king = pos.kingSquare(defense_color);
        Bitboard checkers = pos.checkersBB();
        const int checker_count = checkers.popCount();
        int num_captures = 0;
        bool blockable_check = false;
        int nonblock_moves = 0;

        if (checker_count != 1) {
            for (ExtMove* it = buffer.data(); it != last; ++it) {
                const Move move = it->move;
                if (!move.isDrop() && move.pieceTypeFrom() == King) {
                    king_moves.push_back(move);
                }
            }
        }
        else {
            const Square checker = checkers.firstOneFromSQ11();
            blockable_check = !is_unblockable_check(pos);
            for (ExtMove* it = buffer.data(); it != last; ++it) {
                const Move move = it->move;
                const bool is_king_move = !move.isDrop() && move.pieceTypeFrom() == King;
                if (is_king_move) {
                    king_moves.push_back(move);
                }
                else if (move.to() == checker) {
                    moves.push_back(move);
                    ++num_captures;
                }
            }
        }

        // OSL FixedDepthSearcher::defense uses the raw Escape<KING> order.
        // The DFPN main defense move order is sorted elsewhere, but CHECKMATE_D2
        // must keep the fixed-depth generator order because it can change the
        // first nonzero proof aggregate.
        for (const Move move : king_moves) {
            moves.push_back(move);
        }
        reorder_osl_numbered_escape_target_moves_impl(current_piece_numbers, moves);
        nonblock_moves = static_cast<int>(moves.size());

        const auto collect_raw_blocking_moves = [&]() {
            if (raw_blocking_collected) {
                return;
            }
            raw_blocking_collected = true;
            std::array<ExtMove, kMoveBufferSize> full_buffer;
            ExtMove* full_last = generateOslmateEscapeMoves(full_buffer.data(), pos, false, false);
            for (ExtMove* it = full_buffer.data(); it != full_last; ++it) {
                const Move move = it->move;
                const bool is_king_move = !move.isDrop() && move.pieceTypeFrom() == King;
                if (is_king_move || move.to() == checkers.firstOneFromSQ11()) {
                    continue;
                }
                raw_blocking_moves.push_back(move);
            }
            reorder_osl_numbered_escape_target_moves_impl(current_piece_numbers, raw_blocking_moves);
        };

        if (moves.empty()) {
            collect_raw_blocking_moves();
            for (const Move move : raw_blocking_moves) {
                if (move.isDrop()) {
                    if (!effect_has_at(pos, defense_color, move.to())) {
                        continue;
                    }
                }
                else if (std::abs(static_cast<int>(makeFile(move.from())) - static_cast<int>(makeFile(defense_king))) > 1
                    || std::abs(static_cast<int>(makeRank(move.from())) - static_cast<int>(makeRank(defense_king))) > 1) {
                    if (!has_multiple_effect_at(pos, defense_color, move.to())) {
                        continue;
                    }
                }
                moves.push_back(move);
            }
        }
        const size_t initial_moves = moves.size();
        if (moves.empty() && !blockable_check) {
            if (last_attack_move && last_attack_move.isDrop() && last_attack_move.pieceTypeDropped() == Pawn) {
                return ProofDisproof::PawnCheckmate();
            }
            if (proof_pieces) {
                *proof_pieces = proof_pieces_leaf(pos, attack_color, pos.hand(attack_color));
            }
            return ProofDisproof::NoEscape();
        }

        if (num_captures > 0 && nonblock_moves > 2) {
            if (nonblock_moves > 3) {
                return ProofDisproof(static_cast<uint32_t>(nonblock_moves + (blockable_check ? 1 : 0)), 1);
            }
        }

        const bool cut_candidate =
            nonblock_moves - ((pos.hand(attack_color).exists<HGold>() || pos.hand(attack_color).exists<HSilver>()) ? 1 : 0) > 4;
        if (cut_candidate) {
            return ProofDisproof(static_cast<uint32_t>(nonblock_moves), 1);
        }

        uint32_t min_disproof = ProofDisproof::DISPROOF_MAX;
        uint32_t sum_proof = 0;
        Hand aggregate = zero_hand();
        size_t move_index = 0;
        bool added_blocking_moves = false;
        size_t no_promote_moves = 0;
        while (true) {
            while (move_index < moves.size()) {
                const Move move = moves[move_index++];
                StateInfo st;
                pos.doMove(move, st);

                Move child_best = Move::moveNone();
                Hand child_proof = zero_hand();
                const ProofDisproof child_pdp = fixed_escape_by_move_zero(
                    pos, attack_color, &child_best, &child_proof,
                    no_proof_pieces_attack_estimation, false, current_piece_numbers);

                pos.undoMove(move);
                if (best_move && child_best) {
                    *best_move = child_best;
                }

                if (child_pdp.disproof < min_disproof) {
                    if (child_pdp.disproof == 0) {
                        return child_pdp;
                    }
                    min_disproof = child_pdp.disproof;
                }
                sum_proof += child_pdp.proof;
                if (sum_proof == 0) {
                    aggregate = hand_max(aggregate, child_proof);
                }
                if (sum_proof != 0) {
                    if (move_index < moves.size()) {
                        min_disproof = 1;
                        if (static_cast<int>(move_index - 1) < nonblock_moves) {
                            sum_proof += static_cast<uint32_t>(nonblock_moves - static_cast<int>(move_index));
                        }
                        if (blockable_check) {
                            ++sum_proof;
                        }
                    }
                    if (proof_pieces) {
                        *proof_pieces = aggregate;
                    }
                    return ProofDisproof(sum_proof, min_disproof);
                }
            }

            if (sum_proof == 0) {
                if (!added_blocking_moves && blockable_check && moves.size() == initial_moves) {
                    added_blocking_moves = true;
                    collect_raw_blocking_moves();
                    for (const Move move : raw_blocking_moves) {
                        moves.push_back(move);
                    }
                    if (static_cast<int>(moves.size()) > nonblock_moves) {
                        continue;
                    }
                    if (moves.empty()) {
                        if (proof_pieces) {
                            *proof_pieces = proof_pieces_leaf(pos, attack_color, pos.hand(attack_color));
                        }
                        return ProofDisproof::NoEscape();
                    }
                }

                if (no_promote_moves == 0) {
                    no_promote_moves = moves.size();
                    for (size_t i = 0; i < no_promote_moves; ++i) {
                        const Move move = moves[i];
                        if (!has_ignored_unpromote_escape(move, defense_color)) {
                            continue;
                        }
                        moves.push_back(unpromote_counterpart(pos, move));
                    }
                    if (moves.size() > no_promote_moves) {
                        continue;
                    }
                }
            }

            break;
        }

        if (sum_proof == 0) {
            if (proof_pieces) {
                *proof_pieces = aggregate;
                if (blockable_check) {
                    add_monopolized_pieces(pos.hand(attack_color), pos.hand(defense_color), pos.hand(attack_color), *proof_pieces);
                }
            }
            return ProofDisproof(sum_proof, min_disproof);
        }
        return ProofDisproof(sum_proof, min_disproof);
    }
    std::string history_to_usi_string(const std::vector<Move>& history) {
        std::string result;
        for (size_t i = 0; i < history.size(); ++i) {
            if (i != 0) {
                result.push_back(' ');
            }
            result += history[i].toUSI();
        }
        return result;
    }
    Hand zero_hand() {
        Hand hand;
        hand.set(0);
        return hand;
    }

    Move validated_mate_move_in_1(Position& pos) {
        if (pos.inCheck()) {
            return Move::moveNone();
        }

        std::vector<Move> moves = generate_check_moves(pos);
        const Color attack_color = pos.turn();
        const Square target_king = pos.kingSquare(oppositeColor(attack_color));
        const King8RuntimeInfo king8_info = king8_runtime_info_at(pos, attack_color);
        const ImmediateMateOrderContext order_ctx{
            attack_color,
            target_king,
            move_candidate_mask_runtime(pos, attack_color, target_king, king8_info),
            king8_info.drop_candidate,
        };
        std::stable_sort(moves.begin(), moves.end(), [&](const Move lhs, const Move rhs) {
            return immediate_mate_order_key(lhs, order_ctx) < immediate_mate_order_key(rhs, order_ctx);
        });
        for (const Move move : moves) {
            if (!move || !pos.moveIsLegal(move)) {
                continue;
            }
            StateInfo st;
            pos.doMove(move, st);
            const std::vector<Move> escape_moves = pos.inCheck()
                ? generate_escape_moves(pos, true, SquareNum)
                : std::vector<Move>();
            const bool is_mate = pos.inCheck() && escape_moves.empty();
            pos.undoMove(move);
            if (is_mate) {
                return move;
            }
        }
        return Move::moveNone();
    }

    bool immediate_candidate_is_actual_mate(Position& pos, const Move move, const bool) {
        StateInfo st;
        pos.doMove(move, st);
        std::array<ExtMove, kMoveBufferSize> escape_buffer;
        ExtMove* escape_last = nullptr;
        if (pos.inCheck()) {
            escape_last = generateOslmateEscapeMoves(escape_buffer.data(), pos, false, false);
        }
        const bool is_mate = pos.inCheck() && escape_last == escape_buffer.data();
        pos.undoMove(move);
        return is_mate;
    }

    bool has_hand_piece_type(const Position& pos, const Color color, const PieceType piece_type) {
        switch (piece_type) {
        case Pawn: return pos.hand(color).exists<HPawn>() != 0;
        case Lance: return pos.hand(color).exists<HLance>() != 0;
        case Knight: return pos.hand(color).exists<HKnight>() != 0;
        case Silver: return pos.hand(color).exists<HSilver>() != 0;
        case Gold: return pos.hand(color).exists<HGold>() != 0;
        case Bishop: return pos.hand(color).exists<HBishop>() != 0;
        case Rook: return pos.hand(color).exists<HRook>() != 0;
        default: return false;
        }
    }

    bool immediate_knight_blocking_vertical_attack(const Position& pos, const Color attack_color, Square sq) {
        const auto up = offset_square(sq, 0, orient_for_attacker(attack_color, -1));
        if (!up) {
            return false;
        }

        const Bitboard effect_at_sq = effect_set_at(pos, attack_color, sq);
        const Bitboard effect_at_up = effect_set_at(pos, attack_color, *up);
        Bitboard shared_long = effect_at_sq & effect_at_up;
        shared_long &= pos.bbOf(attack_color);
        shared_long &= pos.bbOf(Lance);
        bool has_vertical_pin = shared_long.isAny();

        if (!has_vertical_pin) {
            Bitboard rooks = effect_at_sq & effect_at_up;
            rooks &= pos.bbOf(attack_color);
            rooks &= pos.bbOf(Rook, Dragon);
            while (rooks.isAny()) {
                const Square from = rooks.firstOneFromSQ11();
                rooks.clearBit(from);
                if (makeFile(from) != makeFile(sq)) {
                    continue;
                }
                const int rank_delta = static_cast<int>(makeRank(from)) - static_cast<int>(makeRank(sq));
                if ((attack_color == Black && rank_delta > 0)
                    || (attack_color == White && rank_delta < 0)) {
                    has_vertical_pin = true;
                    break;
                }
            }
            if (!has_vertical_pin) {
                return false;
            }
        }

        Square scan = *up;
        for (int i = 0; i < 3; ++i) {
            const Piece piece = pos.piece(scan);
            const bool can_move_on_by_defender = piece == Empty || pieceToColor(piece) == attack_color;
            if (!can_move_on_by_defender) {
                return false;
            }
            if (effect_count(pos, attack_color, scan) == 1) {
                return true;
            }
            if (piece != Empty) {
                return false;
            }
            const auto next = offset_square(scan, 0, orient_for_attacker(attack_color, -1));
            if (!next) {
                return false;
            }
            scan = *next;
        }
        return false;
    }
    bool immediate_knight_blocking_diagonal_attack(const Position& pos, const Color attack_color,
        const Square target_king, const King8RuntimeInfo& king8_info, const Square sq) {
        const Color defense_color = oppositeColor(attack_color);
        const auto up = king8_square(target_king, 1, attack_color);
        if (!up) {
            return false;
        }
        if ((king8_info.liberty_candidate & (1u << 1)) == 0) {
            return false;
        }

        const Bitboard effect_at_up = effect_set_at(pos, attack_color, *up);
        const Bitboard effect_at_sq = effect_set_at(pos, attack_color, sq);
        Bitboard bishops = effect_at_up & effect_at_sq;
        bishops &= pos.bbOf(attack_color);
        bishops &= pos.bbOf(Bishop, Horse);
        while (bishops.isAny()) {
            const Square from = bishops.firstOneFromSQ11();
            bishops.clearBit(from);
            const int file_delta = static_cast<int>(makeFile(from)) - static_cast<int>(makeFile(*up));
            const int rank_delta = static_cast<int>(makeRank(from)) - static_cast<int>(makeRank(*up));
            const int file_step = file_delta == 0 ? 0 : (file_delta > 0 ? 1 : -1);
            const int rank_step = rank_delta == 0 ? 0 : (rank_delta > 0 ? 1 : -1);
            if (std::abs(file_delta) != std::abs(rank_delta)) {
                continue;
            }
            const auto next_on_line = offset_square(*up, file_step, rank_step);
            if (!next_on_line || *next_on_line != sq) {
                continue;
            }
            if (effect_at_up.popCount() == 1) {
                return true;
            }
            const Piece up_piece = pos.piece(*up);
            if (up_piece != Empty) {
                return false;
            }
            const auto behind = offset_square(*up, -file_step, -rank_step);
            if (!behind) {
                continue;
            }
            const Piece behind_piece = pos.piece(*behind);
            if ((behind_piece == Empty || pieceToColor(behind_piece) == attack_color)
                && effect_count(pos, attack_color, *behind) == 1) {
                return true;
            }
        }
        return false;
    }

    Move immediate_mate_move_by_osl_knight_candidates(Position& pos, const Color attack_color,
        const Square target_king, const King8RuntimeInfo& king8_info, const bool) {
        if (king8_info.liberty != 0) {
            return Move::moveNone();
        }

        const Color defense_color = oppositeColor(attack_color);
        const Bitboard pinned_attack = pinned_pieces_of(pos, attack_color);
        const Bitboard pinned_defense = pinned_pieces_of(pos, defense_color);
        const bool can_drop_knight = has_hand_piece_type(pos, attack_color, Knight);

        static constexpr std::array<std::pair<int, int>, 2> kOslKnightOrder = { {
            { 1, 2 }, { -1, 2 }
        } };

        for (const auto& [file_delta, rank_delta] : kOslKnightOrder) {
            const auto to = offset_square(
                target_king,
                orient_for_attacker(attack_color, file_delta),
                orient_for_attacker(attack_color, rank_delta));
            if (!to) {
                continue;
            }
            const Piece target_piece = pos.piece(*to);
            if (target_piece != Empty && pieceToColor(target_piece) == attack_color) {
                continue;
            }
            Bitboard defense_effect = effect_set_at(pos, defense_color, *to);
            defense_effect &= ~pinned_defense;
            if (defense_effect.isAny()) {
                continue;
            }

            Bitboard knight_attackers = effect_set_at(pos, attack_color, *to);
            knight_attackers &= pos.bbOf(Knight, attack_color);
            knight_attackers &= ~pinned_attack;
            if (knight_attackers.isAny()) {
                const Square from = knight_attackers.firstOneFromSQ11();
                const Move move = makeCaptureMove(Knight, from, *to, pos);
                if (immediate_knight_blocking_vertical_attack(pos, attack_color, *to)
                    || immediate_knight_blocking_diagonal_attack(pos, attack_color, target_king, king8_info, *to)) {
                    continue;
                }
                return move;
            }

            if (can_drop_knight && target_piece == Empty) {
                const Move move = makeDropMove(Knight, *to);
                if (immediate_knight_blocking_vertical_attack(pos, attack_color, *to)
                    || immediate_knight_blocking_diagonal_attack(pos, attack_color, target_king, king8_info, *to)) {
                    continue;
                }
                return move;
            }
        }

        return Move::moveNone();
    }

    Move immediate_mate_move_by_osl_drop_candidates(Position& pos, const Color attack_color,
        const Square target_king, const King8RuntimeInfo& king8_info, const bool) {
        static constexpr std::array<PieceType, 7> kOslDropPtypeOrder = {
            Gold, Pawn, Lance, Knight, Silver, Bishop, Rook
        };
        const ImmediateMateOrderContext order_ctx{
            attack_color,
            target_king,
            move_candidate_mask_runtime(pos, attack_color, target_king, king8_info),
            king8_info.drop_candidate,
        };

        for (const PieceType piece_type : kOslDropPtypeOrder) {
            if (!has_hand_piece_type(pos, attack_color, piece_type)) {
                continue;
            }
            for (int dir_index = 0; dir_index < 8; ++dir_index) {
                if ((king8_info.drop_candidate & static_cast<uint8_t>(1u << dir_index)) == 0) {
                    continue;
                }
                if (!immediate_can_checkmate_drop_ptype(piece_type, dir_index, king8_info.liberty)) {
                    continue;
                }
                if (!immediate_slow_drop_ok(pos, attack_color, target_king, piece_type, dir_index, king8_info)) {
                    continue;
                }
                const auto to = king8_square(target_king, dir_index, attack_color);
                if (!to) {
                    continue;
                }
                const Move move = makeDropMove(piece_type, *to);
                return move;
            }
        }

        return Move::moveNone();
    }

    Move immediate_mate_move_in_1_osl(Position& pos, const OslPieceNumberState* current_piece_numbers) {
        if (pos.inCheck()) {
            return Move::moveNone();
        }

        const Color attack_color = pos.turn();
        const Square target_king = pos.kingSquare(oppositeColor(attack_color));
        const King8RuntimeInfo king8_info = king8_runtime_info_at(pos, attack_color);
        const ImmediateMateOrderContext order_ctx{
            attack_color,
            target_king,
            move_candidate_mask_runtime(pos, attack_color, target_king, king8_info),
            king8_info.drop_candidate,
        };
        if (const Move move = immediate_mate_move_by_osl_move_candidates(
            pos, attack_color, target_king, king8_info, false, current_piece_numbers)) {
            return move;
        }

        if (const Move move = immediate_mate_move_by_osl_knight_candidates(
            pos, attack_color, target_king, king8_info, false)) {
            return move;
        }

        return immediate_mate_move_by_osl_drop_candidates(
            pos, attack_color, target_king, king8_info, false);
    }
    std::array<Hand, ColorNum> stand_pair(const Position& pos) {
        return { pos.hand(Black), pos.hand(White) };
    }

    std::array<Hand, ColorNum> stand_pair_after_move(const std::array<Hand, ColorNum>& stands,
        const Color mover, const Move move) {
        std::array<Hand, ColorNum> next = stands;
        if (move.isDrop()) {
            next[mover].minusOne(move.handPieceDropped());
            return next;
        }
        if (move.isCapture()) {
            const PieceType captured = move.cap();
            next[mover].plusOne(pieceTypeToHandPiece(osl_captured_ptype(captured)));
        }
        return next;
    }

    struct OracleState {
        Key board_index = 0;
        uint64_t board_secondary = 0;
        std::array<Hand, ColorNum> stands{ Hand(0), Hand(0) };

        OracleState() = default;
        explicit OracleState(const Position& pos)
            : board_index(board_index_key(pos)),
              board_secondary(secondary_board_key(pos)),
              stands(stand_pair(pos)) {
        }
    };

    template <HandPiece HP>
    int hand_count(const Hand& hand) {
        return static_cast<int>(hand.numOf<HP>());
    }

    int hand_count(const Hand& hand, const HandPiece hp) {
        return static_cast<int>(hand.numOf(hp));
    }

    uint32_t osl_piece_stand_flags(const Hand& hand) {
        // OSL PieceStand packs pieces as R,B,S,N,L,P,G and uses carry/borrow
        // bits for dominance instead of per-piece-count comparison.
        uint32_t flags = 0;
        flags |= static_cast<uint32_t>(hand.numOf(HRook)) << 0;
        flags |= static_cast<uint32_t>(hand.numOf(HBishop)) << 3;
        flags |= static_cast<uint32_t>(hand.numOf(HSilver)) << 6;
        flags |= static_cast<uint32_t>(hand.numOf(HKnight)) << 10;
        flags |= static_cast<uint32_t>(hand.numOf(HLance)) << 14;
        flags |= static_cast<uint32_t>(hand.numOf(HPawn)) << 18;
        flags |= static_cast<uint32_t>(hand.numOf(HGold)) << 24;
        return flags;
    }

    bool osl_stand_is_superior_or_equal(const Hand& lhs, const Hand& rhs) {
        constexpr uint32_t carry_mask = 0x48822224u;
        const uint32_t lhs_flags = osl_piece_stand_flags(lhs) | carry_mask;
        const uint32_t rhs_flags = osl_piece_stand_flags(rhs) & ~carry_mask;
        return ((lhs_flags - rhs_flags) & carry_mask) == carry_mask;
    }
    void set_hand_count(Hand& hand, const HandPiece hp, const int count) {
        if (count > 0) {
            hand.orEqual(count, hp);
        }
    }

    OracleState oracle_state_after_move(const OracleState& oracle, const Color mover, const Move move) {
        OracleState next = oracle;
        next.board_index = static_cast<Key>(board_index_key_after_move(next.board_index, mover, move));
        next.board_secondary = secondary_board_key_after_move(next.board_secondary, mover, move);
        if (move.isDrop()) {
            next.stands[mover].minusOne(move.handPieceDropped());
            return next;
        }
        if (move.isCapture()) {
            const PieceType captured = move.cap();
            next.stands[mover].plusOne(pieceTypeToHandPiece(osl_captured_ptype(captured)));
        }
        return next;
    }

    OracleState oracle_state_from_current_node(const Key board_index, const uint64_t board_secondary,
        const std::array<Hand, ColorNum>& stands) {
        OracleState oracle;
        oracle.board_index = board_index;
        oracle.board_secondary = board_secondary;
        oracle.stands = stands;
        return oracle;
    }

    OracleState oracle_state_before_move(const Key board_index, const uint64_t board_secondary,
        const std::array<Hand, ColorNum>& stands, const Move move, const Color mover) {
        OracleState prev;
        prev.board_index = static_cast<Key>(board_index_key_before_move(board_index, mover, move));
        prev.board_secondary = secondary_board_key_before_move(board_secondary, mover, move);
        prev.stands = stands;
        if (move.isDrop()) {
            prev.stands[mover].plusOne(move.handPieceDropped());
            return prev;
        }
        if (move.isCapture()) {
            const PieceType captured = move.cap();
            prev.stands[mover].minusOne(pieceTypeToHandPiece(osl_captured_ptype(captured)));
        }
        return prev;
    }

    Hand hand_max(const Hand& lhs, const Hand& rhs) {
        Hand result = zero_hand();
        set_hand_count(result, HPawn, std::max(hand_count(lhs, HPawn), hand_count(rhs, HPawn)));
        set_hand_count(result, HLance, std::max(hand_count(lhs, HLance), hand_count(rhs, HLance)));
        set_hand_count(result, HKnight, std::max(hand_count(lhs, HKnight), hand_count(rhs, HKnight)));
        set_hand_count(result, HSilver, std::max(hand_count(lhs, HSilver), hand_count(rhs, HSilver)));
        set_hand_count(result, HGold, std::max(hand_count(lhs, HGold), hand_count(rhs, HGold)));
        set_hand_count(result, HBishop, std::max(hand_count(lhs, HBishop), hand_count(rhs, HBishop)));
        set_hand_count(result, HRook, std::max(hand_count(lhs, HRook), hand_count(rhs, HRook)));
        return result;
    }

    bool decrement_hand_if_any(Hand& hand, const HandPiece hp) {
        if (hand.exists(hp)) {
            hand.minusOne(hp);
            return true;
        }
        return false;
    }

    void add_monopolized_pieces(const Hand& us, const Hand& them, const Hand& max, Hand& out) {
        (void)us;
        out.setPP(max, them);
    }

    bool is_unblockable_check(const Position& pos) {
        const Square king = pos.kingSquare(pos.turn());
        Bitboard checkers = pos.checkersBB();
        const int checker_count = checkers.popCount();
        if (checker_count != 1) {
            return checker_count > 1;
        }
        const Square from = checkers.firstOneFromSQ11();
        const Color attack_color = oppositeColor(pos.turn());
        const PieceType attacker_type = pieceToPieceType(pos.piece(from));
        return has_unblockable_effect_to_king(attacker_type, attack_color, from, king);
    }

    Hand proof_pieces_leaf(const Position& pos, const Color attack_color, const Hand& max) {
        Hand result = zero_hand();
        if (!is_unblockable_check(pos)) {
            add_monopolized_pieces(pos.hand(attack_color), pos.hand(oppositeColor(attack_color)), max, result);
        }
        return result;
    }

    Hand fixed_attack_leaf_proof_pieces(const Move best_move) {
        Hand result = zero_hand();
        if (best_move.isDrop()) {
            result.plusOne(best_move.handPieceDropped());
        }
        return result;
    }

    Hand disproof_pieces_leaf(const Position& pos, const Color attack_color, const Hand& max) {
        Hand result = zero_hand();
        add_monopolized_pieces(pos.hand(oppositeColor(attack_color)), pos.hand(attack_color), max, result);
        return result;
    }

    Hand proof_pieces_after_attack(const Hand& prev, const Move move, const Hand& max) {
        Hand result = prev;
        if (move.isDrop()) {
            const HandPiece hp = move.handPieceDropped();
            if (hand_count(result, hp) < hand_count(max, hp)) {
                result.plusOne(hp);
            }
        }
        else if (move.isCapture()) {
            decrement_hand_if_any(result, pieceTypeToHandPiece(osl_captured_ptype(move.cap())));
        }
        return result;
    }

    Move complete_move_for_position(const Position& pos, Move move) {
        if (!move || move.isDrop()) {
            return move;
        }
        PieceType from_type = move.pieceTypeFrom();
        if (from_type == Empty) {
            from_type = pieceToPieceType(pos.piece(move.from()));
        }
        Move result = pieceToPieceType(pos.piece(move.to())) == Empty
            ? makeMove(from_type, move.from(), move.to())
            : makeCaptureMove(from_type, move.from(), move.to(), pos);
        if (move.isPromotion() && (from_type & PTPromote) == 0) {
            result |= promoteFlag();
        }
        return result;
    }

    template <class MoveContainer>
    void complete_moves_for_position(const Position& pos, MoveContainer& moves) {
        for (Move& move : moves) {
            move = complete_move_for_position(pos, move);
        }
    }

    PieceType osl_move_ptype(const Position& pos, Move move) {
        if (!move) {
            return Occupied;
        }
        if (move.isDrop()) {
            return move.pieceTypeDropped();
        }

        const PieceType board_from_type = pieceToPieceType(pos.piece(move.from()));
        const PieceType from_type = board_from_type == Empty ? move.pieceTypeFrom() : board_from_type;
        return move.pieceTypeTo(from_type);
    }

    Hand disproof_pieces_after_defense(const Hand& prev, const Move move, const Hand& max) {
        Hand result = prev;
        if (move.isDrop()) {
            const HandPiece hp = move.handPieceDropped();
            if (hand_count(result, hp) < hand_count(max, hp)) {
                result.plusOne(hp);
            }
        }
        else if (move.isCapture()) {
            decrement_hand_if_any(result, pieceTypeToHandPiece(osl_captured_ptype(move.cap())));
        }
        return result;
    }

    bool grand_parent_simulation_suitable(const std::vector<Move>& move_history) {
        if (move_history.size() < 3) {
            return false;
        }

        const Move alm = move_history[move_history.size() - 1];
        const Move dlm = move_history[move_history.size() - 2];
        const Move alm2 = move_history[move_history.size() - 3];
        return is_osl_normal_move(dlm)
            && alm.to() == dlm.to()
            && !dlm.isCapture()
            && is_osl_normal_move(alm2)
            && alm2.to() == alm.from();
    }

    bool is_delay_escape_node(const Position& pos, const Square last_to) {
        const Color defense_color = pos.turn();
        const Color attack_color = oppositeColor(defense_color);
        if (last_to == SquareNum) {
            return false;
        }
        const Bitboard defense_effect = effect_set_at(pos, defense_color, last_to);
        const Bitboard defense_attackers_except_king =
            defense_effect & ~pos.bbOf(King, defense_color);
        return defense_effect.isAny()
            && (defense_attackers_except_king.isAny()
                || !effect_has_at(pos, attack_color, last_to));
    }

    class PathEncodingTable {
    public:
        static constexpr size_t MaxEncodingLength = 256;
        static constexpr size_t SquareCapacity = 0x100;
        static constexpr size_t PtypeCapacity = 16;

        PathEncodingTable() {
            std::mt19937 mt_random;
            for (size_t depth = 0; depth < MaxEncodingLength; ++depth) {
                for (size_t square = 0; square < SquareCapacity; ++square) {
                    for (size_t piece_type = 0; piece_type < PtypeCapacity; ++piece_type) {
                        const uint64_t high = mt_random();
                        const uint32_t low = mt_random();
                        values_[depth][square][piece_type] = (high << 32) + (low & ~UINT32_C(1));
                    }
                }
            }
        }

        uint64_t get(const size_t depth, const int pos, const int ptype) const {
            assert(0 <= pos && pos < static_cast<int>(SquareCapacity));
            assert(0 <= ptype && ptype < static_cast<int>(PtypeCapacity));
            return values_[depth % MaxEncodingLength][static_cast<size_t>(pos)][static_cast<size_t>(ptype)];
        }

        uint64_t get(const size_t depth, const Move move) const {
            const auto osl_square_index = [](const Square square) {
                if (!isInSquare(square)) {
                    return 0;
                }
                return (static_cast<int>(makeFile(square)) + 1) * 16
                    + (static_cast<int>(makeRank(square)) + 1) + 1;
            };
            const auto osl_ptype_index = [](const PieceType piece_type) {
                switch (piece_type) {
                case Pawn: return 10;
                case Lance: return 11;
                case Knight: return 12;
                case Silver: return 13;
                case Bishop: return 14;
                case Rook: return 15;
                case Gold: return 9;
                case King: return 8;
                case ProPawn: return 2;
                case ProLance: return 3;
                case ProKnight: return 4;
                case ProSilver: return 5;
                case Horse: return 6;
                case Dragon: return 7;
                default: return 0;
                }
            };
            const int from = move.isDrop() ? 0 : osl_square_index(move.from());
            const int to = osl_square_index(move.to());
            const int from_piece = osl_ptype_index(move.pieceTypeFromOrDropped());
            const int to_piece = osl_ptype_index(move.pieceTypeTo());
            return get(depth, from, from_piece) + get(depth, to, to_piece) + 1;
        }

        static const PathEncodingTable& instance() {
            static const PathEncodingTable table;
            return table;
        }

    private:
        std::array<std::array<std::array<uint64_t, PtypeCapacity>, SquareCapacity>, MaxEncodingLength> values_{};
    };

    class PathEncoding {
    public:
        explicit PathEncoding(const int depth = 0) : path_(0), depth_(depth) {}
        explicit PathEncoding(const Color turn, const int depth = 0)
            : path_(turn == Black ? 0 : 1), depth_(depth) {}

        void pushMove(const Move move) {
            path_ += PathEncodingTable::instance().get(static_cast<size_t>(depth_), move);
            ++depth_;
        }

        void popMove(const Move move) {
            --depth_;
            path_ -= PathEncodingTable::instance().get(static_cast<size_t>(depth_), move);
        }

        uint64_t getPath() const { return path_; }
        int getDepth() const { return depth_; }

        bool operator==(const PathEncoding& other) const { return path_ == other.path_; }
        bool operator!=(const PathEncoding& other) const { return !(*this == other); }

    private:
        uint64_t path_ = 0;
        int depth_ = 0;
    };

    struct DfpnRecord {
        ProofDisproof proof_disproof = ProofDisproof::Unknown();
        Move best_move = Move::moveNone();
        Hand stands[ColorNum]{ Hand(0), Hand(0) };
        uint64_t board_secondary = 0;
        Hand proof_pieces = Hand(0);
        Hand proof_pieces_candidate = Hand(0);
        uint64_t solved = 0;
        uint64_t dag_moves = 0;
        uint32_t node_count = 0;
        uint32_t tried_oracle = 0;
        uint32_t min_pdp = ProofDisproof::PROOF_MAX;
        uint32_t working_threads = 0;
        uint16_t remaining_depth = 0;
        Move last_move = Move::moveNone();
        Square last_to = SquareNum;
        ProofPiecesType proof_pieces_set = ProofPiecesType::Unset;
        uint8_t need_full_width = 0;
        bool false_branch = false;
        bool dag_terminal = false;
        bool exact = false;

        DfpnRecord() = default;
        explicit DfpnRecord(const Position& pos) {
            board_secondary = secondary_board_key(pos);
            stands[Black] = pos.hand(Black);
            stands[White] = pos.hand(White);
        }
        DfpnRecord(const Position& pos, const uint64_t secondary) {
            board_secondary = secondary;
            stands[Black] = pos.hand(Black);
            stands[White] = pos.hand(White);
        }
        DfpnRecord(const uint64_t secondary, const std::array<Hand, ColorNum>& current_stands) {
            board_secondary = secondary;
            stands[Black] = current_stands[Black];
            stands[White] = current_stands[White];
        }

        void refresh_stands(const Position& pos) {
            board_secondary = secondary_board_key(pos);
            stands[Black] = pos.hand(Black);
            stands[White] = pos.hand(White);
        }
        void refresh_stands(const Position& pos, const uint64_t secondary) {
            board_secondary = secondary;
            stands[Black] = pos.hand(Black);
            stands[White] = pos.hand(White);
        }
        void refresh_stands(const uint64_t secondary, const std::array<Hand, ColorNum>& current_stands) {
            board_secondary = secondary;
            stands[Black] = current_stands[Black];
            stands[White] = current_stands[White];
        }

        void setFrom(const DfpnRecord& src) {
            proof_disproof = src.proof_disproof;
            best_move = src.best_move;
            proof_pieces = src.proof_pieces;
            proof_pieces_candidate = src.proof_pieces_candidate;
            node_count = 1;
            tried_oracle = src.tried_oracle;
            min_pdp = src.min_pdp;
            working_threads = src.working_threads;
            remaining_depth = 0;
            last_move = Move::moveNone();
            last_to = SquareNum;
            proof_pieces_set = src.proof_pieces_set;
            solved = 0;
            dag_moves = 0;
            need_full_width = 0;
            false_branch = false;
            dag_terminal = false;
            exact = false;
        }

        uint32_t proof() const { return proof_disproof.proof; }
        uint32_t disproof() const { return proof_disproof.disproof; }

        void setProofPieces(const Hand& hand) {
            assert(proof_pieces_set == ProofPiecesType::Unset);
            proof_pieces_set = ProofPiecesType::Proof;
            proof_pieces = hand;
        }

        void setDisproofPieces(const Hand& hand) {
            assert(proof_pieces_set == ProofPiecesType::Unset);
            proof_pieces_set = ProofPiecesType::Disproof;
            proof_pieces = hand;
        }

        const Hand& proofPieces() const {
            assert(proof_pieces_set == ProofPiecesType::Proof);
            return proof_pieces;
        }

        const Hand& disproofPieces() const {
            assert(proof_pieces_set == ProofPiecesType::Disproof);
            return proof_pieces;
        }
    };

    void copy_record_aux_to_child(ChildState& child, const DfpnRecord& record) {
        child.proof_pieces_candidate = record.proof_pieces_candidate;
        child.solved = record.solved;
        child.dag_moves = record.dag_moves;
        child.tried_oracle = record.tried_oracle;
        child.min_pdp = record.min_pdp;
        child.working_threads = record.working_threads;
        child.remaining_depth = record.remaining_depth;
        child.last_to = record.last_to;
        child.need_full_width = record.need_full_width;
        child.false_branch = record.false_branch;
        child.dag_terminal = record.dag_terminal;
        child.exact = record.exact;
    }

    void copy_child_aux_to_record(DfpnRecord& record, const ChildState& child) {
        record.proof_pieces_candidate = child.proof_pieces_candidate;
        record.solved = child.solved;
        record.dag_moves = child.dag_moves;
        record.tried_oracle = child.tried_oracle;
        record.min_pdp = child.min_pdp;
        record.working_threads = child.working_threads;
        record.remaining_depth = child.remaining_depth;
        record.last_to = child.last_to;
        record.need_full_width = child.need_full_width;
        record.false_branch = child.false_branch;
        record.dag_terminal = child.dag_terminal;
        record.exact = child.exact;
    }

    bool is_osl_normal_move(const Move move) {
        return move.isAny() && move.isOK();
    }

    inline void accumulate_record_node_count(DfpnRecord& record, const uint32_t node_count_org, const uint32_t current_node_count) {
        if (current_node_count <= node_count_org) {
            return;
        }
        record.node_count = saturate_sum(
            static_cast<uint64_t>(record.node_count) + (current_node_count - node_count_org),
            std::numeric_limits<uint32_t>::max());
    }

    struct PathRecord {
        static constexpr int MaxDistance = 1024 * 128;

        int distance = MaxDistance;
        bool visiting = false;
        uint32_t node_count = 0;
        std::forward_list<PathEncoding> twin_paths;

        bool hasTwin(const PathEncoding& path) const {
            return std::find(twin_paths.begin(), twin_paths.end(), path) != twin_paths.end();
        }

        void addTwin(const PathEncoding& path) {
            twin_paths.push_front(path);
        }
    };

    inline void set_path_node_count(PathRecord* path_record, const uint32_t node_count) {
        if (path_record) {
            path_record->node_count = node_count;
        }
    }

    struct DepthLimitReached {};

    class DfpnTable {
    public:
        void reserve(const size_t n) { table_.reserve(n); }
        void clear() {
            table_.clear();
            total_size_ = 0;
        }

        void set_growth_limit(const size_t n) {
            growth_limit_ = std::max<size_t>(1024, n);
            table_.reserve(growth_limit_ + growth_limit_ / 128 + 1);
        }

        size_t growth_limit() const { return growth_limit_; }
        size_t size() const { return total_size_; }

        static bool same_stands(const DfpnRecord& record, const Position& pos) {
            return record.stands[Black] == pos.hand(Black);
        }

        static bool same_stands(const DfpnRecord& record, const uint64_t board_secondary,
            const std::array<Hand, ColorNum>& stands) {
            (void)board_secondary;
            return record.stands[Black] == stands[Black];
        }

        DfpnRecord probe(const Position& pos, const Color attack_color, const bool use_initial_dominance = true) const {
            DfpnRecord result(pos);
            const auto it = table_.find(make_oslmate_board_key(pos));
            if (it == table_.end()) {
                return result;
            }

            const Hand attack_stand = pos.hand(attack_color);
            const Hand defense_stand = pos.hand(oppositeColor(attack_color));
            uint32_t proof_hint = 1;
            uint32_t disproof_hint = 1;
            for (const DfpnRecord& record : it->second) {
                const bool same = same_stands(record, pos);
                if (same) {
                    result = record;
                    if (result.proof_disproof.isFinal()) {
                        break;
                    }
                    continue;
                }
                if (record.proof_disproof.isCheckmateSuccess()) {
                    if (osl_stand_is_superior_or_equal(attack_stand, record.proofPieces())) {
                        result.setFrom(record);
                        break;
                    }
                }
                else if (record.proof_disproof.isCheckmateFail()) {
                    if (osl_stand_is_superior_or_equal(defense_stand, record.disproofPieces())) {
                        result.setFrom(record);
                        break;
                    }
                }
                if (!use_initial_dominance || record.proof_disproof.isFinal()) {
                    continue;
                }
                const Hand record_attack = record.stands[attack_color];
                if (osl_stand_is_superior_or_equal(record_attack, attack_stand)) {
                    proof_hint = std::max(proof_hint, record.proof());
                }
                else if (osl_stand_is_superior_or_equal(attack_stand, record_attack)) {
                    disproof_hint = std::max(disproof_hint, record.disproof());
                }
            }

            if (use_initial_dominance
                && (result.proof_disproof == ProofDisproof::Unknown()
                    || result.proof_disproof == ProofDisproof(1, 1))) {
                result.proof_disproof = ProofDisproof(
                    std::min(proof_hint, kInitialDominanceProofMax),
                    std::min(disproof_hint, kInitialDominanceDisproofMax));
                ++result.node_count;
            }
            return result;
        }

        DfpnRecord probe(const Key board_index, const uint64_t board_secondary, const std::array<Hand, ColorNum>& stands,
            const Color attack_color, const bool use_initial_dominance = true) const {
            DfpnRecord result;
            result.board_secondary = board_secondary;
            result.stands[Black] = stands[Black];
            result.stands[White] = stands[White];
            (void)board_secondary;
            const auto it = table_.find(make_oslmate_board_key(board_index, board_secondary));
            if (it == table_.end()) {
                return result;
            }

            const Hand attack_stand = stands[attack_color];
            const Hand defense_stand = stands[oppositeColor(attack_color)];
            uint32_t proof_hint = 1;
            uint32_t disproof_hint = 1;

            for (const DfpnRecord& record : it->second) {
                const bool same = same_stands(record, board_secondary, stands);
                if (same) {
                    result = record;
                    if (result.proof_disproof.isFinal()) {
                        break;
                    }
                    continue;
                }
                if (record.proof_disproof.isCheckmateSuccess()) {
                    if (osl_stand_is_superior_or_equal(attack_stand, record.proofPieces())) {
                        result.setFrom(record);
                        break;
                    }
                }
                else if (record.proof_disproof.isCheckmateFail()) {
                    if (osl_stand_is_superior_or_equal(defense_stand, record.disproofPieces())) {
                        result.setFrom(record);
                        break;
                    }
                }
                if (!use_initial_dominance || record.proof_disproof.isFinal()) {
                    continue;
                }
                const Hand record_attack = record.stands[attack_color];
                if (osl_stand_is_superior_or_equal(record_attack, attack_stand)) {
                    proof_hint = std::max(proof_hint, record.proof());
                }
                else if (osl_stand_is_superior_or_equal(attack_stand, record_attack)) {
                    disproof_hint = std::max(disproof_hint, record.disproof());
                }
            }

            if (use_initial_dominance
                && (result.proof_disproof == ProofDisproof::Unknown()
                    || result.proof_disproof == ProofDisproof(1, 1))) {
                result.proof_disproof = ProofDisproof(
                    std::min(proof_hint, kInitialDominanceProofMax),
                    std::min(disproof_hint, kInitialDominanceDisproofMax));
                ++result.node_count;
            }
            return result;
        }

        DfpnRecord* exact(const Position& pos) {
            const auto it = table_.find(make_oslmate_board_key(pos));
            if (it == table_.end()) {
                return nullptr;
            }
            for (DfpnRecord& record : it->second) {
                if (same_stands(record, pos)) {
                    return &record;
                }
            }
            return nullptr;
        }

        DfpnRecord* exact(const Position& pos, const uint64_t board_secondary) {
            const auto it = table_.find(make_oslmate_board_key(board_index_key(pos), board_secondary));
            if (it == table_.end()) {
                return nullptr;
            }
            for (DfpnRecord& record : it->second) {
                if (same_stands(record, pos)) {
                    return &record;
                }
            }
            return nullptr;
        }

        const DfpnRecord* exact(const Position& pos) const {
            const auto it = table_.find(make_oslmate_board_key(pos));
            if (it == table_.end()) {
                return nullptr;
            }
            for (const DfpnRecord& record : it->second) {
                if (same_stands(record, pos)) {
                    return &record;
                }
            }
            return nullptr;
        }

        const DfpnRecord* exact(const Position& pos, const uint64_t board_secondary) const {
            const auto it = table_.find(make_oslmate_board_key(board_index_key(pos), board_secondary));
            if (it == table_.end()) {
                return nullptr;
            }
            for (const DfpnRecord& record : it->second) {
                if (same_stands(record, pos)) {
                    return &record;
                }
            }
            return nullptr;
        }

        const DfpnRecord* exact(const Key board_index, const uint64_t board_secondary,
            const Hand& black_stand, const Hand& white_stand) const {
            (void)board_secondary;
            (void)white_stand;
            const auto it = table_.find(make_oslmate_board_key(board_index, board_secondary));
            if (it == table_.end()) {
                return nullptr;
            }
            for (const DfpnRecord& record : it->second) {
                if (record.stands[Black] == black_stand) {
                    return &record;
                }
            }
            return nullptr;
        }

        DfpnRecord* exact(const Key board_index, const uint64_t board_secondary,
            const Hand& black_stand, const Hand& white_stand) {
            (void)board_secondary;
            (void)white_stand;
            const auto it = table_.find(make_oslmate_board_key(board_index, board_secondary));
            if (it == table_.end()) {
                return nullptr;
            }
            for (DfpnRecord& record : it->second) {
                if (record.stands[Black] == black_stand) {
                    return &record;
                }
            }
            return nullptr;
        }

        const DfpnRecord* same_stand_record(const Position& pos) const {
            const auto it = table_.find(make_oslmate_board_key(pos));
            if (it == table_.end()) {
                return nullptr;
            }
            for (const DfpnRecord& record : it->second) {
                if (same_stands(record, pos)) {
                    return &record;
                }
            }
            return nullptr;
        }

        const DfpnRecord* same_stand_record(const Key board_index, const uint64_t board_secondary,
            const Hand& black_stand) const {
            const auto it = table_.find(make_oslmate_board_key(board_index, board_secondary));
            if (it == table_.end()) {
                return nullptr;
            }
            for (const DfpnRecord& record : it->second) {
                if (record.stands[Black] == black_stand) {
                    return &record;
                }
            }
            return nullptr;
        }

        std::vector<DfpnRecord> bucket_records(const Position& pos) const {
            std::vector<DfpnRecord> result;
            const auto it = table_.find(make_oslmate_board_key(pos));
            if (it == table_.end()) {
                return result;
            }
            for (const DfpnRecord& record : it->second) {
                result.push_back(record);
            }
            return result;
        }

        static bool store_merge_oslmate(DfpnRecord& record, DfpnRecord& value) {
            // The shipped oslmate build is non-SMP. In that configuration
            // DfpnTable::List::store simply overwrites the same-stand record;
            // the min_pdp/solved/dag merge is inside OSL_DFPN_SMP only.
            record = value;
            return false;
        }

        DfpnRecord& store(const Position& pos) {
            auto& bucket = table_[make_oslmate_board_key(pos)];
            for (DfpnRecord& record : bucket) {
                if (same_stands(record, pos)) {
                    record.exact = true;
                    return record;
                }
            }
            bucket.push_front(DfpnRecord(pos));
            bucket.front().exact = true;
            ++total_size_;
            return bucket.front();
        }

        void store_exact_oslmate(const Position& pos, DfpnRecord& value) {
            value.refresh_stands(pos);
            value.exact = true;
            auto& bucket = table_[make_oslmate_board_key(pos)];
            for (DfpnRecord& record : bucket) {
                if (!same_stands(record, pos)) {
                    continue;
                }
                if (!store_merge_oslmate(record, value)) {
                    return;
                }
                record.refresh_stands(pos);
                record.exact = true;
                return;
            }
            bucket.push_front(value);
            bucket.front().refresh_stands(pos);
            bucket.front().exact = true;
            ++total_size_;
        }

        void store_exact_oslmate(const Position& pos, const uint64_t board_secondary, DfpnRecord& value) {
            store_exact_oslmate(pos, board_index_key(pos), board_secondary, value);
        }

        void store_exact_oslmate(const Position& pos, const Key board_index,
            const uint64_t board_secondary, DfpnRecord& value) {
            value.refresh_stands(pos, board_secondary);
            value.exact = true;
            auto& bucket = table_[make_oslmate_board_key(board_index, board_secondary)];
            for (DfpnRecord& record : bucket) {
                if (!same_stands(record, pos)) {
                    continue;
                }
                if (!store_merge_oslmate(record, value)) {
                    return;
                }
                record.refresh_stands(pos, board_secondary);
                record.exact = true;
                return;
            }

            bucket.push_front(value);
            bucket.front().refresh_stands(pos, board_secondary);
            bucket.front().exact = true;
            ++total_size_;
        }

        void store_exact_oslmate(const Key board_index, const uint64_t board_secondary,
            const std::array<Hand, ColorNum>& stands, DfpnRecord& value) {
            value.refresh_stands(board_secondary, stands);
            value.exact = true;
            auto& bucket = table_[make_oslmate_board_key(board_index, board_secondary)];
            for (DfpnRecord& record : bucket) {
                if (!same_stands(record, board_secondary, stands)) {
                    continue;
                }
                if (!store_merge_oslmate(record, value)) {
                    return;
                }
                record.refresh_stands(board_secondary, stands);
                record.exact = true;
                return;
            }

            bucket.push_front(value);
            bucket.front().refresh_stands(board_secondary, stands);
            bucket.front().exact = true;
            ++total_size_;
        }

        void store_nonexact_oslmate(const Position& pos, DfpnRecord& value) {
            value.refresh_stands(pos);
            value.exact = false;
            auto& bucket = table_[make_oslmate_board_key(pos)];
            for (DfpnRecord& record : bucket) {
                if (!same_stands(record, pos)) {
                    continue;
                }
                if (!store_merge_oslmate(record, value)) {
                    return;
                }
                record.refresh_stands(pos);
                return;
            }

            bucket.push_front(value);
            bucket.front().refresh_stands(pos);
            ++total_size_;
        }

        void store_nonexact_oslmate(const Position& pos, const uint64_t board_secondary, DfpnRecord& value) {
            store_nonexact_oslmate(pos, board_index_key(pos), board_secondary, value);
        }

        void store_nonexact_oslmate(const Position& pos, const Key board_index,
            const uint64_t board_secondary, DfpnRecord& value) {
            value.refresh_stands(pos, board_secondary);
            value.exact = false;
            auto& bucket = table_[make_oslmate_board_key(board_index, board_secondary)];
            for (DfpnRecord& record : bucket) {
                if (!same_stands(record, pos)) {
                    continue;
                }
                if (!store_merge_oslmate(record, value)) {
                    return;
                }
                record.refresh_stands(pos, board_secondary);
                return;
            }

            bucket.push_front(value);
            bucket.front().refresh_stands(pos, board_secondary);
            ++total_size_;
        }

        void store_nonexact_oslmate(const Key board_index, const uint64_t board_secondary,
            const std::array<Hand, ColorNum>& stands, DfpnRecord& value) {
            value.refresh_stands(board_secondary, stands);
            value.exact = false;
            auto& bucket = table_[make_oslmate_board_key(board_index, board_secondary)];
            for (DfpnRecord& record : bucket) {
                if (!same_stands(record, board_secondary, stands)) {
                    continue;
                }
                if (!store_merge_oslmate(record, value)) {
                    return;
                }
                record.refresh_stands(board_secondary, stands);
                return;
            }

            bucket.push_front(value);
            bucket.front().refresh_stands(board_secondary, stands);
            ++total_size_;
        }

        void add_dag(const Key board_index, const uint64_t board_secondary, DfpnRecord& value) {
            (void)board_secondary;
            auto& bucket = table_[make_oslmate_board_key(board_index, board_secondary)];
            for (DfpnRecord& record : bucket) {
                if (record.stands[Black] == value.stands[Black]) {
                    // Non-SMP oslmate addDag only copies the computed dag bitset.
                    record.dag_moves = value.dag_moves;
                    return;
                }
            }
        }

        DfpnRecord& store_nonexact(const Position& pos) {
            auto& bucket = table_[make_oslmate_board_key(pos)];
            for (DfpnRecord& record : bucket) {
                if (same_stands(record, pos)) {
                    return record;
                }
            }
            bucket.push_front(DfpnRecord(pos));
            bucket.front().exact = false;
            ++total_size_;
            return bucket.front();
        }

        DfpnRecord findProofOracle(const Key board_index, const uint64_t board_secondary, const std::array<Hand, ColorNum>& stands,
            const Color attack_color, const Move last_move) const {
            DfpnRecord result;
            result.board_secondary = board_secondary;
            result.stands[Black] = stands[Black];
            result.stands[White] = stands[White];
            (void)board_secondary;
            const auto it = table_.find(make_oslmate_board_key(board_index, board_secondary));
            if (it == table_.end()) {
                return result;
            }

            const Hand attack_stand = stands[attack_color];
            for (const DfpnRecord& record : it->second) {
                if (!record.proof_disproof.isCheckmateSuccess()) {
                    continue;
                }
                if (!osl_stand_is_superior_or_equal(attack_stand, record.proofPieces())) {
                    continue;
                }
                result.setFrom(record);
                ++const_cast<DfpnRecord&>(record).node_count;
                if (record.last_move == last_move) {
                    break;
                }
            }
            return result;
        }

        size_t run_gc() {
            if (total_size_ < growth_limit_ && (growth_limit_ - total_size_) >= growth_limit_ / 8) {
                return 0;
            }

            const size_t before = total_size_;
            size_t removed = 0;
            for (auto it = table_.begin(); it != table_.end();) {
                auto& bucket = it->second;
                auto prev = bucket.begin();
                while (prev != bucket.end()) {
                    auto cur = std::next(prev);
                    if (cur == bucket.end()) {
                        break;
                    }
                    if (!cur->proof_disproof.isFinal()
                        && cur->node_count < gc_threshold_) {
                        cur = bucket.erase_after(prev);
                        ++removed;
                        continue;
                    }
                    prev = cur;
                }
                const auto first = bucket.begin();
                if (first != bucket.end()
                    && !first->proof_disproof.isFinal()
                    && first->node_count < gc_threshold_) {
                    bucket.pop_front();
                    ++removed;
                }
                if (bucket.empty()) {
                    it = table_.erase(it);
                }
                else {
                    ++it;
                }
            }
            total_size_ -= std::min(total_size_, removed);
            gc_threshold_ += 15;
            static double memory_limit = 0.75;
            const double memory = osl_memory_use_ratio();
            if (memory > memory_limit) {
                growth_limit_ -= growth_limit_ / 8;
                gc_threshold_ += 15 + gc_threshold_ / 4;
                memory_limit += 0.01;
            }
            if (removed < before * 2 / 3) {
                gc_threshold_ += 15 + gc_threshold_ / 2;
            }
            if ((removed < before * 3 / 5 && memory > 0.75) || removed < before / 2) {
                throw DepthLimitReached();
            }
            return removed;
        }

    private:
        std::unordered_map<OslmateBoardKey, std::forward_list<DfpnRecord>, OslmateBoardKeyHash> table_;
        size_t total_size_ = 0;
        size_t growth_limit_ = std::numeric_limits<size_t>::max();
        size_t gc_threshold_ = 10;
    };

    class DfpnPathTable {
    public:
        enum class LoopToDominance {
            NoLoop,
            BadAttackLoop,
        };

        PathRecord* allocate(const Position& pos, const Hand& black_stand, const Color attack_color, const int depth, LoopToDominance& loop) {
            return allocate(board_index_key(pos), secondary_board_key(pos), black_stand, attack_color, depth, loop);
        }

        PathRecord* allocate(const Key board_index, const uint64_t board_secondary, const Hand& black_stand,
            const Color attack_color, const int depth, LoopToDominance& loop) {
            auto& bucket = table_[make_oslmate_board_key(board_index, board_secondary)];
            loop = LoopToDominance::NoLoop;
            PathRecord* exact = nullptr;
            for (auto& entry : bucket) {
                if (entry.first == black_stand) {
                    exact = &entry.second;
                    if (loop == LoopToDominance::BadAttackLoop || entry.second.visiting) {
                        break;
                    }
                }
                if (!entry.second.visiting) {
                    continue;
                }
                if (osl_stand_is_superior_or_equal(entry.first, black_stand)) {
                    if (attack_color == Black) {
                        loop = LoopToDominance::BadAttackLoop;
                        if (exact) {
                            break;
                        }
                    }
                }
                else if (osl_stand_is_superior_or_equal(black_stand, entry.first)) {
                    if (attack_color == White) {
                        loop = LoopToDominance::BadAttackLoop;
                        if (exact) {
                            break;
                        }
                    }
                }
            }
            if (exact) {
                exact->distance = std::min(exact->distance, depth);
                return exact;
            }
            bucket.push_front(std::make_pair(black_stand, PathRecord{}));
            bucket.front().second.distance = depth;
            ++total_size_;
            return &bucket.front().second;
        }

        const PathRecord* probe(const Position& pos, const Hand& black_stand) const {
            return probe(board_index_key(pos), secondary_board_key(pos), black_stand);
        }

        const PathRecord* probe(const Key board_index, const uint64_t board_secondary, const Hand& black_stand) const {
            const auto it = table_.find(make_oslmate_board_key(board_index, board_secondary));
            if (it == table_.end()) {
                return nullptr;
            }
            for (const auto& entry : it->second) {
                if (entry.first == black_stand) {
                    return &entry.second;
                }
            }
            return nullptr;
        }

        void clear() {
            table_.clear();
        }

        void reserve(const size_t n) { table_.reserve(n); }
        size_t size() const { return total_size_; }

        size_t run_gc() {
            size_t removed = 0;
            for (auto& entry : table_) {
                auto& bucket = entry.second;
                auto prev = bucket.begin();
                while (prev != bucket.end()) {
                    auto cur = prev;
                    ++cur;
                    if (cur == bucket.end()) {
                        break;
                    }
                    if (!path_record_precious(cur->second, gc_threshold_)) {
                        bucket.erase_after(prev);
                        ++removed;
                        continue;
                    }
                    prev = cur;
                }
                if (!bucket.empty()
                    && !path_record_precious(bucket.front().second, gc_threshold_)) {
                    bucket.pop_front();
                    ++removed;
                }
            }
            total_size_ -= removed;
            gc_threshold_ += 15;
            static double memory_threshold = 0.8;
            const double memory = osl_memory_use_ratio();
            if (memory > memory_threshold) {
                gc_threshold_ += 15;
                memory_threshold += 1.0 / 128.0;
            }
            return removed;
        }

    private:
        static bool path_record_precious(const PathRecord& record, const size_t threshold) {
            return record.visiting
                || record.node_count > threshold
                || (!record.twin_paths.empty()
                    && record.node_count > threshold - 10);
        }

        std::unordered_map<OslmateBoardKey, std::forward_list<std::pair<Hand, PathRecord>>, OslmateBoardKeyHash> table_;
        size_t total_size_ = 0;
        size_t gc_threshold_ = 10;
    };

    struct VisitLock {
        PathRecord* record = nullptr;

        explicit VisitLock(PathRecord* r) : record(r) {
            if (record) {
                assert(!record->visiting);
                record->visiting = true;
            }
        }

        ~VisitLock() {
            if (record) {
                assert(record->visiting);
                record->visiting = false;
            }
        }
    };

    ProofDisproof mark_loop(PathRecord* record, const PathEncoding& path) {
        if (record) {
            record->addTwin(path);
        }
        return ProofDisproof::Unknown();
    }

    void set_loop_detection_record(DfpnRecord& record, PathRecord* path_record,
        const PathEncoding& path, DfpnRecord* out_exact) {
        record.proof_disproof = ProofDisproof::Unknown();
        if (out_exact) {
            *out_exact = record;
        }
        if (path_record) {
            path_record->addTwin(path);
        }
    }

    size_t path_twin_count(const PathRecord* record) {
        if (!record) {
            return 0;
        }
        return static_cast<size_t>(std::distance(record->twin_paths.begin(), record->twin_paths.end()));
    }

    enum class ChildLoopReason : uint8_t {
        None = 0,
        Visiting,
        Twin,
    };

    ChildLoopReason child_loop_reason(const ChildState& child, const PathEncoding& current_path) {
        if (!child.path_record || child.pdp.isFinal()) {
            return ChildLoopReason::None;
        }
        if (child.path_record->visiting) {
            return ChildLoopReason::Visiting;
        }
        PathEncoding child_path = current_path;
        child_path.pushMove(child.move);
        return child.path_record->hasTwin(child_path) ? ChildLoopReason::Twin : ChildLoopReason::None;
    }

    bool child_is_loop(const ChildState& child, const PathEncoding& current_path) {
        return child_loop_reason(child, current_path) != ChildLoopReason::None;
    }

    inline uint32_t saturate_sum(const uint64_t value, const uint32_t limit) {
        return static_cast<uint32_t>(std::min<uint64_t>(value, limit));
    }

    inline uint32_t saturate_inc(const uint32_t value, const uint32_t limit) {
        return value >= limit ? limit : value + 1;
    }

    inline uint32_t slow_increase(const uint32_t n) {
        if (n <= 1) {
            return 1;
        }
        unsigned long index = 0;
        _BitScanReverse(&index, n);
        return static_cast<uint32_t>(index + 1);
    }

    inline uint32_t oslmate_attack_child_proof_threshold(const uint32_t threshold_proof,
        const uint32_t second_proof, const uint32_t proof_average, const uint32_t proof_cost) {
        const uint32_t proof_c = static_cast<uint32_t>(std::min<uint64_t>(
            static_cast<uint64_t>(second_proof) + proof_average,
            threshold_proof));
        return proof_c - proof_cost;
    }

    inline uint32_t oslmate_attack_child_disproof_threshold(const uint32_t threshold_disproof,
        const uint64_t sum_disproof, const uint32_t self_disproof) {
        uint32_t disproof_c = static_cast<uint32_t>(threshold_disproof - (sum_disproof - self_disproof));
        if (disproof_c > threshold_disproof) {
            disproof_c = static_cast<uint32_t>(self_disproof + (threshold_disproof - sum_disproof));
        }
        return disproof_c;
    }

    inline uint32_t oslmate_defense_child_proof_threshold(const uint32_t threshold_proof,
        const uint64_t sum_proof, const uint32_t self_proof) {
        uint32_t proof_c = static_cast<uint32_t>(threshold_proof - (sum_proof - self_proof));
        if (proof_c > threshold_proof) {
            proof_c = static_cast<uint32_t>(self_proof + (threshold_proof - sum_proof));
        }
        return proof_c;
    }

    inline void normalize_pawn_drop_no_escape(const Move move, ProofDisproof& pdp) {
        if (pdp == ProofDisproof::NoEscape() && move.isDrop() && move.pieceTypeDropped() == Pawn) {
            pdp = ProofDisproof::PawnCheckmate();
        }
    }

    inline bool is_pawn_drop_no_escape(const Move move, const ProofDisproof& pdp) {
        return pdp == ProofDisproof::NoEscape()
            && move.isDrop()
            && move.pieceTypeDropped() == Pawn;
    }

    inline uint64_t child_bit(const size_t index) {
        // OSL uses `1ull << i` directly.  On the reference x64 build the shift
        // count is masked by the target instruction, so preserve that behavior
        // instead of treating indexes >= 64 as absent.
        return 1ull << (index & 63);
    }

    inline void set_no_checkmate_child_in_attack(DfpnRecord& record, const ChildState& child, const size_t index) {
        record.solved |= child_bit(index);
        record.min_pdp = std::min(record.min_pdp, child.pdp.proof);
        assert(child.proof_pieces_type == ProofPiecesType::Disproof);
        record.proof_pieces_candidate = hand_max(record.proof_pieces_candidate, child.proof_pieces);
    }

    inline void set_checkmate_child_in_defense(DfpnRecord& record, const ChildState& child, const size_t index) {
        record.solved |= child_bit(index);
        record.min_pdp = std::min(record.min_pdp, child.pdp.disproof);
        assert(child.proof_pieces_type == ProofPiecesType::Proof);
        record.proof_pieces_candidate = hand_max(record.proof_pieces_candidate, child.proof_pieces);
    }

    size_t adjacent_promoted_counterpart_index(const std::vector<Move>& moves, const size_t index) {
        if (index == 0 || index >= moves.size() || moves[index].isDrop() || moves[index].isPromotion()) {
            return std::numeric_limits<size_t>::max();
        }
        const size_t prev = index - 1;
        if (!moves[prev].isDrop()
            && moves[prev].isPromotion()
            && moves[prev].fromAndTo() == moves[index].fromAndTo()
            && moves[prev].pieceTypeFrom() == moves[index].pieceTypeFrom()) {
            return prev;
        }
        return std::numeric_limits<size_t>::max();
    }

    inline bool is_osl_ignored_unpromote_pair(const std::vector<Move>& moves, const size_t index) {
        if (index == 0 || index >= moves.size()) {
            return false;
        }
        const Move current = moves[index];
        const Move previous = moves[index - 1];
        // OSL skips any adjacent non-drop move with the same fromTo().  In
        // normal OSL generation this is the ignored-unpromote pair; keep the
        // same predicate here instead of strengthening it by promotion flags.
        return !current.isDrop()
            && current.fromAndTo() == previous.fromAndTo();
    }

    inline int effect_count(const Position& pos, const Color c, const Square sq);

    PieceType unpromote_piece_type(const PieceType piece_type) {
        switch (piece_type) {
        case ProPawn: return Pawn;
        case ProLance: return Lance;
        case ProKnight: return Knight;
        case ProSilver: return Silver;
        case Horse: return Bishop;
        case Dragon: return Rook;
        default: return piece_type;
        }
    }

    constexpr std::array<int, 8> kDirFileDelta = { 1, 0, -1, 1, -1, 1, 0, -1 };
    constexpr std::array<int, 8> kDirRankDelta = { -1, -1, -1, 0, 0, 1, 1, 1 };
    constexpr std::array<const char*, 8> kDirLabels = { "UL", "U", "UR", "L", "R", "DL", "D", "DR" };

    bool is_edge_square(const Square sq) {
        const File file = makeFile(sq);
        const Rank rank = makeRank(sq);
        return file == File1 || file == File9 || rank == Rank1 || rank == Rank9;
    }

    std::optional<Square> offset_square(const Square sq, const int file_delta, const int rank_delta) {
        const int file = static_cast<int>(makeFile(sq)) + file_delta;
        const int rank = static_cast<int>(makeRank(sq)) + rank_delta;
        if (!isInSquare(static_cast<File>(file), static_cast<Rank>(rank))) {
            return std::nullopt;
        }
        return makeSquare(static_cast<File>(file), static_cast<Rank>(rank));
    }

    uint8_t popcount8(const uint8_t value) {
        uint8_t count = 0;
        for (uint8_t bits = value; bits != 0; bits &= static_cast<uint8_t>(bits - 1)) {
            ++count;
        }
        return count;
    }

    int lsb_u8(uint8_t value) {
        int index = 0;
        while ((value & 1u) == 0) {
            value >>= 1;
            ++index;
        }
        return index;
    }

    int orient_for_attacker(const Color attacker, const int delta) {
        return attacker == Black ? delta : -delta;
    }

    int dir_index_from_delta(const Color attacker, const int file_delta, const int rank_delta) {
        const int oriented_file_delta = orient_for_attacker(attacker, file_delta);
        const int oriented_rank_delta = orient_for_attacker(attacker, rank_delta);
        for (size_t dir = 0; dir < kDirFileDelta.size(); ++dir) {
            if (kDirFileDelta[dir] == oriented_file_delta && kDirRankDelta[dir] == oriented_rank_delta) {
                return static_cast<int>(dir);
            }
        }
        return -1;
    }

    std::optional<Square> king8_square(const Square king, const int dir_index, const Color attack_color) {
        return offset_square(
            king,
            -orient_for_attacker(attack_color, kDirFileDelta[static_cast<size_t>(dir_index)]),
            -orient_for_attacker(attack_color, kDirRankDelta[static_cast<size_t>(dir_index)]));
    }

    Bitboard pinned_pieces_of(const Position& pos, const Color pinned_color) {
        Bitboard result = allZeroBB();
        const Color attacker = oppositeColor(pinned_color);
        const Square king_sq = pos.kingSquare(pinned_color);

        for (size_t dir = 0; dir < kDirFileDelta.size(); ++dir) {
            std::optional<Square> first = offset_square(
                king_sq,
                -kDirFileDelta[dir],
                -kDirRankDelta[dir]);
            while (first && pos.piece(*first) == Empty) {
                first = offset_square(*first, -kDirFileDelta[dir], -kDirRankDelta[dir]);
            }
            if (!first) {
                continue;
            }

            const Piece first_piece = pos.piece(*first);
            if (first_piece == Empty || pieceToColor(first_piece) != pinned_color) {
                continue;
            }

            std::optional<Square> slider = offset_square(*first, -kDirFileDelta[dir], -kDirRankDelta[dir]);
            while (slider && pos.piece(*slider) == Empty) {
                slider = offset_square(*slider, -kDirFileDelta[dir], -kDirRankDelta[dir]);
            }
            if (!slider) {
                continue;
            }

            const Piece slider_piece = pos.piece(*slider);
            if (slider_piece == Empty || pieceToColor(slider_piece) != attacker) {
                continue;
            }
            const PieceType slider_type = pieceToPieceType(slider_piece);
            const bool has_long_effect =
                (slider_type == Lance && lanceAttackToEdge(attacker, *slider).isSet(*first))
                || ((slider_type == Rook || slider_type == Dragon) && rookAttackToEdge(*slider).isSet(*first))
                || ((slider_type == Bishop || slider_type == Horse) && bishopAttackToEdge(*slider).isSet(*first));
            if (has_long_effect) {
                result.setBit(*first);
            }
        }
        return result;
    }

    bool same_dir_as_king8_index(const Square king, const Square sq, const int dir_index, const Color attack_color) {
        const Color defense_color = oppositeColor(attack_color);
        const int file_delta = static_cast<int>(makeFile(sq)) - static_cast<int>(makeFile(king));
        const int rank_delta = static_cast<int>(makeRank(sq)) - static_cast<int>(makeRank(king));
        const int step_file = orient_for_attacker(defense_color, file_delta == 0 ? 0 : (file_delta > 0 ? 1 : -1));
        const int step_rank = orient_for_attacker(defense_color, rank_delta == 0 ? 0 : (rank_delta > 0 ? 1 : -1));
        return step_file == kDirFileDelta[static_cast<size_t>(dir_index)]
            && step_rank == kDirRankDelta[static_cast<size_t>(dir_index)];
    }

    bool has_additional_attack_effect_target(const Position& pos, const Color attacker, const Square target) {
        Bitboard direct = effect_set_at(pos, attacker, target);
        while (direct.isAny()) {
            const Square from = direct.firstOneFromSQ11();
            direct.clearBit(from);
            const PieceType direct_piece_type = pieceToPieceType(pos.piece(from));
            if (direct_piece_type == Knight) {
                continue;
            }

            const int file_delta = static_cast<int>(makeFile(target)) - static_cast<int>(makeFile(from));
            const int rank_delta = static_cast<int>(makeRank(target)) - static_cast<int>(makeRank(from));
            const int file_step = file_delta == 0 ? 0 : (file_delta > 0 ? 1 : -1);
            const int rank_step = rank_delta == 0 ? 0 : (rank_delta > 0 ? 1 : -1);
            if (!(file_delta == 0 || rank_delta == 0 || std::abs(file_delta) == std::abs(rank_delta))) {
                continue;
            }

            Bitboard occupied_without_direct = pos.occupiedBB();
            occupied_without_direct.clearBit(from);
            std::optional<Square> scan = offset_square(from, -file_step, -rank_step);
            while (scan) {
                const Piece candidate = pos.piece(*scan);
                if (candidate != Empty) {
                    return pieceToColor(candidate) == attacker
                        && Position::attacksFrom(pieceToPieceType(candidate), pieceToColor(candidate), *scan, occupied_without_direct).isSet(target);
                }
                scan = offset_square(*scan, -file_step, -rank_step);
            }
        }
        return false;
    }

    bool has_additional_attack_effect_at(const Position& pos, const Color attacker, const Square target) {
        return has_additional_attack_effect_target(pos, attacker, target);
    }

    bool has_enough_defense_effect(const Position& pos, const Color attack_color, const Square target_king,
        const Square sq, const Bitboard& pinned_defense, const int dir_index) {
        const Color defense_color = oppositeColor(attack_color);
        Bitboard defenders = effect_set_at(pos, defense_color, sq);
        defenders.clearBit(target_king);
        if (!defenders.isAny()) {
            return false;
        }

        if ((defenders & ~pinned_defense).isAny()) {
            return true;
        }

        while (defenders.isAny()) {
            const Square from = defenders.firstOneFromSQ11();
            defenders.clearBit(from);
            if (pinned_defense.isSet(from) && same_dir_as_king8_index(target_king, from, dir_index, attack_color)) {
                return true;
            }
        }
        return false;
    }

    bool is_long_king_effect_attacker(const Position& pos, const Color attack_color, const Square king, const Square from) {
        const PieceType piece_type = pieceToPieceType(pos.piece(from));
        if (!(isSlider(piece_type) || piece_type == Lance)) {
            return false;
        }

        const int file_delta = static_cast<int>(makeFile(king)) - static_cast<int>(makeFile(from));
        const int rank_delta = static_cast<int>(makeRank(king)) - static_cast<int>(makeRank(from));
        if (std::max(std::abs(file_delta), std::abs(rank_delta)) <= 1) {
            return false;
        }

        if (piece_type == Lance && file_delta != 0) {
            return false;
        }

        if (!(file_delta == 0 || rank_delta == 0 || std::abs(file_delta) == std::abs(rank_delta))) {
            return false;
        }

        (void)attack_color;
        return true;
    }

    King8RuntimeInfo make_king8_runtime_info(const Position& pos, const Color attack_color) {
        const Color defense_color = oppositeColor(attack_color);
        const Square king = pos.kingSquare(defense_color);
        const Bitboard pinned_defense = pinned_pieces_of(pos, defense_color);
        King8RuntimeInfo info;

        for (size_t dir = 0; dir < kDirFileDelta.size(); ++dir) {
            const auto sq = king8_square(king, static_cast<int>(dir), attack_color);
            if (!sq) {
                continue;
            }

            const Piece piece = pos.piece(*sq);
            const bool is_empty = piece == Empty;
            const bool is_defense_piece = piece != Empty && pieceToColor(piece) == defense_color;
            const bool is_attack_piece = piece != Empty && pieceToColor(piece) == attack_color;
            const bool attack_has_effect = effect_has_at(pos, attack_color, *sq);

            if (!attack_has_effect) {
                if (!is_empty && !is_attack_piece) {
                    continue;
                }
                info.liberty |= static_cast<uint8_t>(1u << dir);
                info.liberty_candidate |= static_cast<uint8_t>(1u << dir);
                if (is_empty || is_attack_piece) {
                    ++info.liberty_count;
                }
                if (is_empty) {
                    info.spaces |= static_cast<uint8_t>(1u << dir);
                }
                continue;
            }

            const bool enough_defense = has_enough_defense_effect(pos, attack_color, king, *sq, pinned_defense, static_cast<int>(dir));
            if (enough_defense) {
                if (is_empty) {
                    info.liberty_candidate |= static_cast<uint8_t>(1u << dir);
                    info.spaces |= static_cast<uint8_t>(1u << dir);
                    info.moves |= static_cast<uint8_t>(1u << dir);
                }
                else if (is_attack_piece) {
                    info.liberty_candidate |= static_cast<uint8_t>(1u << dir);
                }
                else {
                    info.moves |= static_cast<uint8_t>(1u << dir);
                }
                continue;
            }

            if (is_empty) {
                info.drop_candidate |= static_cast<uint8_t>(1u << dir);
                info.liberty_candidate |= static_cast<uint8_t>(1u << dir);
                info.move_candidate2 |= static_cast<uint8_t>(1u << dir);
                info.spaces |= static_cast<uint8_t>(1u << dir);
                info.moves |= static_cast<uint8_t>(1u << dir);
            }
            else if (is_attack_piece) {
                info.liberty_candidate |= static_cast<uint8_t>(1u << dir);
            }
            else {
                // OSL King8Info::hasEffectMask uses 0x10001000000 << Dir:
                // bits 24 and 40, i.e. moveCandidate2 and moves.
                info.move_candidate2 |= static_cast<uint8_t>(1u << dir);
                info.moves |= static_cast<uint8_t>(1u << dir);
            }
        }

        Bitboard long_attackers = effect_set_at(pos, attack_color, king);
        while (long_attackers.isAny()) {
            const Square from = long_attackers.firstOneFromSQ11();
            if (!is_long_king_effect_attacker(pos, attack_color, king, from)) {
                continue;
            }
            const int file_delta = static_cast<int>(makeFile(from)) - static_cast<int>(makeFile(king));
            const int rank_delta = static_cast<int>(makeRank(from)) - static_cast<int>(makeRank(king));
            const int dir_index = dir_index_from_delta(attack_color,
                file_delta == 0 ? 0 : (file_delta > 0 ? 1 : -1),
                rank_delta == 0 ? 0 : (rank_delta > 0 ? 1 : -1));
            if (dir_index >= 0 && (info.liberty & static_cast<uint8_t>(1u << dir_index)) != 0) {
                info.liberty &= static_cast<uint8_t>(~(1u << dir_index));
                if (info.liberty_count > 0) {
                    --info.liberty_count;
                }
            }
        }

        return info;
    }

    class King8InfoCache {
    public:
        King8RuntimeInfo get(const Position& pos, const Color attack_color) {
            const Key key = pos.getBoardKey();
            Entry& entry = entries_[index(key, attack_color)];
            if (!entry.valid || entry.board_key != key || entry.attack_color != attack_color) {
                entry.valid = true;
                entry.board_key = key;
                entry.attack_color = attack_color;
                entry.info = make_king8_runtime_info(pos, attack_color);
            }
            return entry.info;
        }

    private:
        static constexpr size_t kEntryCount = 4096;
        struct Entry {
            bool valid = false;
            Key board_key = 0;
            Color attack_color = Black;
            King8RuntimeInfo info{};
        };

        static size_t index(uint64_t key, const Color attack_color) {
            key ^= key >> 32;
            key ^= key >> 16;
            key ^= static_cast<uint64_t>(attack_color) << 7;
            return static_cast<size_t>(key) & (kEntryCount - 1);
        }

        std::array<Entry, kEntryCount> entries_{};
    };

    King8RuntimeInfo king8_runtime_info_at(const Position& pos, const Color attack_color) {
        static thread_local King8InfoCache cache;
        return cache.get(pos, attack_color);
    }

    King8RuntimeInfo reset_edge_from_liberty_runtime(const Square king, const Color attack_color, King8RuntimeInfo info) {
        for (size_t dir = 0; dir < kDirFileDelta.size(); ++dir) {
            const uint8_t bit = static_cast<uint8_t>(1u << dir);
            if ((info.liberty & bit) == 0) {
                continue;
            }
            const auto sq = king8_square(king, static_cast<int>(dir), attack_color);
            if (!sq) {
                continue;
            }
            if (is_edge_square(*sq)) {
                info.liberty &= static_cast<uint8_t>(~bit);
            }
        }
        info.liberty_count = popcount8(info.liberty);
        return info;
    }

    int liberty_after_all_drop_runtime(const Position& pos, const Color attack_color, const King8RuntimeInfo& info) {
        int result = static_cast<int>(info.liberty_count) - 1;
        if (result < 2) {
            return 1;
        }

        const auto& tables = proof_number_runtime_tables();
        const size_t color_index = static_cast<size_t>(attack_color);
        const uint16_t liberty_drop_mask = info.liberty_drop_mask();
        const uint8_t piece_mask = stand_piece_mask(pos, attack_color);
        for (; result > 1 && (piece_mask & tables.drop_liberty[color_index][liberty_drop_mask][result - 1]) != 0; --result) {
        }
        return result;
    }

    int move_candidate_mask_runtime(const Position& pos, const Color attack_color, const Square king, const King8RuntimeInfo& info) {
        int mask = 0;
        for (size_t dir = 0; dir < kDirFileDelta.size(); ++dir) {
            const uint8_t bit = static_cast<uint8_t>(1u << dir);
            if ((info.move_candidate2 & bit) == 0) {
                continue;
            }
            const auto sq = king8_square(king, static_cast<int>(dir), attack_color);
            if (!sq) {
                continue;
            }
            if (has_multiple_effect_at(pos, attack_color, *sq) || has_additional_attack_effect_at(pos, attack_color, *sq)) {
                mask |= static_cast<int>(bit);
            }
        }
        return mask;
    }

    bool has_pmajor_runtime(const Position& pos, const Color attack_color, const Square king) {
        Bitboard majors = pos.bbOf(Bishop, Horse, Rook, Dragon) & pos.bbOf(attack_color);
        const Rank relative_rank = makeRank(inverseIfWhite(attack_color, king));
        while (majors.isAny()) {
            const Square from = majors.firstOneFromSQ11();
            const PieceType piece_type = pieceToPieceType(pos.piece(from));
            if (relative_rank > Rank3 && (piece_type & PTPromote) == 0 && !canPromote(attack_color, from)) {
                continue;
            }
            if ((Position::attacksFrom(piece_type, attack_color, from, pos.occupiedBB()) & kingAttack(king)).isAny()) {
                return true;
            }
        }
        return false;
    }

    int liberty_after_all_move_runtime(const Position& pos, const Color attack_color, const King8RuntimeInfo& info) {
        const Square king = pos.kingSquare(oppositeColor(attack_color));
        const int move_candidate_mask = move_candidate_mask_runtime(pos, attack_color, king, info);
        const auto& tables = proof_number_runtime_tables();
        const size_t color_index = static_cast<size_t>(attack_color);

        if (has_pmajor_runtime(pos, attack_color, king)) {
            return tables.pmajor_liberty[color_index][info.liberty][move_candidate_mask];
        }

        const Rank relative_rank = makeRank(inverseIfWhite(attack_color, king));
        bool promoted_area = relative_rank == Rank1 || relative_rank == Rank2;
        if (!promoted_area) {
            const auto front = offset_square(king, 0, attack_color == Black ? 1 : -1);
            promoted_area = front && (effect_set_at(pos, attack_color, *front) & pos.goldsBB(attack_color)).isAny();
        }

        if (promoted_area) {
            return tables.promote_liberty[color_index][info.liberty][move_candidate_mask];
        }

        return tables.other_move_liberty[color_index][info.liberty][move_candidate_mask];
    }

    int disproof_after_all_check_runtime(const Position& pos, const Color attack_color, const Square king, const King8RuntimeInfo& info) {
        int num_checks = 0;
        for (size_t dir = 0; dir < kDirFileDelta.size(); ++dir) {
            const uint8_t bit = static_cast<uint8_t>(1u << dir);
            if ((info.move_candidate2 & bit) == 0) {
                continue;
            }
            const auto sq = king8_square(king, static_cast<int>(dir), attack_color);
            if (!sq) {
                continue;
            }
            if (has_multiple_effect_at(pos, attack_color, *sq) || has_additional_attack_effect_at(pos, attack_color, *sq)) {
                ++num_checks;
            }
        }

        const int drop_scale = (pos.hand(attack_color).exists<HGold>() ? 1 : 0)
            + (pos.hand(attack_color).exists<HSilver>() ? 1 : 0);
        if (drop_scale > 0) {
            num_checks += static_cast<int>(popcount8(info.drop_candidate)) * drop_scale;
        }
        return std::max(1, num_checks);
    }

    ProofDisproof attack_estimation_zero_with_info(const Position& pos, const Color attack_color, const King8RuntimeInfo& info) {
        const Square king = pos.kingSquare(oppositeColor(attack_color));
        int proof = liberty_after_all_drop_runtime(pos, attack_color, info);
        const int drop_proof = proof;
        const int move_proof = proof >= 2 ? liberty_after_all_move_runtime(pos, attack_color, info) : proof;
        if (proof >= 2) {
            proof = std::min(proof, move_proof);
        }
        const int disproof = disproof_after_all_check_runtime(pos, attack_color, king, info);
        if (false || false) {
            const std::string sfen = pos.toSFEN();
            if (sfen.rfind("5+S1kl/7r1/3p3p1/p2nppp1p/5ns2/3Pl3P/1PNK1PP2/1GGS2S2/5G1NL b ", 0) == 0
                || sfen.rfind("5+S2l/7kr/3p3p1/p2nppp1p/5ns2/3Pl3P/1PNK1PP2/1GGS2S2/5G1NL b ", 0) == 0
                || false) {
                const int move_mask = move_candidate_mask_runtime(pos, attack_color, king, info);
            }
        }
        if (false && false) {
            const int move_mask = move_candidate_mask_runtime(pos, attack_color, king, info);
            const int move_count = popcount8(static_cast<uint8_t>(move_mask));
            const int drop_scale = (pos.hand(attack_color).exists<HGold>() ? 1 : 0)
                + (pos.hand(attack_color).exists<HSilver>() ? 1 : 0);
            for (size_t dir = 0; dir < kDirFileDelta.size(); ++dir) {
                const uint8_t bit = static_cast<uint8_t>(1u << dir);
                const auto sq = king8_square(king, static_cast<int>(dir), attack_color);
                const int effects = sq ? effect_count(pos, attack_color, *sq) : 0;
                const bool additional = sq && has_additional_attack_effect_at(pos, attack_color, *sq);
                const Color defense_color = oppositeColor(attack_color);
                Bitboard defenders = sq ? effect_set_at(pos, defense_color, *sq) : allZeroBB();
                defenders.clearBit(king);
                const Bitboard pinned_defense = pinned_pieces_of(pos, defense_color);
                const bool enough = sq && has_enough_defense_effect(pos, attack_color, king, *sq, pinned_defense, static_cast<int>(dir));
                const int defender_count = defenders.popCount();
                const int pinned_defender_count = (defenders & pinned_defense).popCount();
                while (defenders.isAny()) {
                    const Square from = defenders.firstOneFromSQ11();
                    defenders.clearBit(from);
                }
            }
        }
        return { static_cast<uint32_t>(proof), static_cast<uint32_t>(disproof) };
    }

    ProofDisproof attack_estimation_zero(const Position& pos, const Color attack_color) {
        const Square king = pos.kingSquare(oppositeColor(attack_color));
        const King8RuntimeInfo info = reset_edge_from_liberty_runtime(king, attack_color, king8_runtime_info_at(pos, attack_color));
        return attack_estimation_zero_with_info(pos, attack_color, info);
    }

    ProofDisproof fixed_attack_estimation_zero(const Position& pos, const Color attack_color) {
        const Square king = pos.kingSquare(oppositeColor(attack_color));
        const King8RuntimeInfo info = reset_edge_from_liberty_runtime(king, attack_color, king8_runtime_info_at(pos, attack_color));
        return attack_estimation_zero_with_info(pos, attack_color, info);
    }
    int osl_sort_ptype(const PieceType piece_type) {
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

    int osl_square_index(const Square square) {
        if (square == SquareNum) {
            return 0;
        }
        const int x = static_cast<int>(makeFile(square)) + 1;
        const int y = static_cast<int>(makeRank(square)) + 1;
        return x * 16 + y + 1;
    }

    int osl_piece_number_group_key(const PieceType piece_type) {
        switch (unpromote_piece_type(piece_type)) {
        case Pawn: return 0;
        case Knight: return 18;
        case Silver: return 22;
        case Gold: return 26;
        case King: return 30;
        case Lance: return 32;
        case Bishop: return 36;
        case Rook: return 38;
        default: return 128;
        }
    }

    int osl_piece_iteration_key(const Move move) {
        if (move.isDrop()) {
            return 0;
        }
        return osl_piece_number_group_key(move.pieceTypeFrom()) * 256
            + osl_square_index(move.from());
    }

    using MoveSortKey = std::tuple<bool, int, bool>;

    MoveSortKey move_sort_key(const Position& pos, const Color turn, const Move move) {
        const int attack_support = effect_count(pos, turn, move.to()) + (move.isDrop() ? 1 : 0);
        const int defense_support = effect_count(pos, oppositeColor(turn), move.to());
        const int move_sort_turn_sign = turn == Black ? 1 : -1;
        const int file = static_cast<int>(makeFile(move.to())) + 1;
        const int to_y = move_sort_turn_sign * (static_cast<int>(makeRank(move.to())) + 1);
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

    struct MoveWithSortKey {
        Move move;
        uint32_t key;
    };

    PieceType osl_old_piece_type(const Position& pos, const Move move) {
        if (move.isDrop()) {
            return Occupied;
        }
        const Piece piece = pos.piece(move.from());
        if (piece != Empty) {
            return pieceToPieceType(piece);
        }
        return move.pieceTypeFrom();
    }

    enum class DfpnCheckGenerationPhase : int {
        U = 0,
        Knight = 1,
        UL = 2,
        UR = 3,
        L = 4,
        R = 5,
        D = 6,
        DL = 7,
        DR = 8,
        RookLong = 9,
        BishopLong = 10,
        DropGold = 11,
        DropSilver = 12,
        DropBishop = 13,
        DropRook = 14,
        Other = 15,
    };

    int dfpn_check_adjacent_dir_index(const Color attacker, const Square king, const Square to) {
        // Match OSL DirectionTraits<Dir>::blackDx/blackDy.  OSL
        // AddEffectWithEffect checks pos = target - offset, so the delta here
        // is king - move.to, not move.to - king.
        static constexpr std::array<std::pair<int, int>, 8> kDirs = {{
            { 1, -1 },
            { 0, -1 },
            { -1, -1 },
            { 1, 0 },
            { -1, 0 },
            { 1, 1 },
            { 0, 1 },
            { -1, 1 },
        }};

        const int file_delta = static_cast<int>(makeFile(king)) - static_cast<int>(makeFile(to));
        const int rank_delta = static_cast<int>(makeRank(king)) - static_cast<int>(makeRank(to));
        const int oriented_file = orient_for_attacker(attacker, file_delta);
        const int oriented_rank = orient_for_attacker(attacker, rank_delta);

        for (size_t i = 0; i < kDirs.size(); ++i) {
            if (kDirs[i].first == oriented_file && kDirs[i].second == oriented_rank) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    int dfpn_check_adjacent_phase(const int dir_index) {
        switch (dir_index) {
        case 1: return static_cast<int>(DfpnCheckGenerationPhase::U);
        case 0: return static_cast<int>(DfpnCheckGenerationPhase::UL);
        case 2: return static_cast<int>(DfpnCheckGenerationPhase::UR);
        case 3: return static_cast<int>(DfpnCheckGenerationPhase::L);
        case 4: return static_cast<int>(DfpnCheckGenerationPhase::R);
        case 6: return static_cast<int>(DfpnCheckGenerationPhase::D);
        case 5: return static_cast<int>(DfpnCheckGenerationPhase::DL);
        case 7: return static_cast<int>(DfpnCheckGenerationPhase::DR);
        default: return static_cast<int>(DfpnCheckGenerationPhase::Other);
        }
    }

    int dfpn_check_knight_side_index(const Color attacker, const Square king, const Square to) {
        const int file_delta = static_cast<int>(makeFile(king)) - static_cast<int>(makeFile(to));
        const int rank_delta = static_cast<int>(makeRank(king)) - static_cast<int>(makeRank(to));
        const int oriented_file = orient_for_attacker(attacker, file_delta);
        const int oriented_rank = orient_for_attacker(attacker, rank_delta);
        if (oriented_file == 1 && oriented_rank == -2) {
            return 0;
        }
        if (oriented_file == -1 && oriented_rank == -2) {
            return 1;
        }
        return 2;
    }

    int dfpn_check_line_dir_index(const Color attacker, const Square king, const Square to) {
        const int file_delta = static_cast<int>(makeFile(king)) - static_cast<int>(makeFile(to));
        const int rank_delta = static_cast<int>(makeRank(king)) - static_cast<int>(makeRank(to));
        const int file_step = file_delta == 0 ? 0 : (file_delta > 0 ? 1 : -1);
        const int rank_step = rank_delta == 0 ? 0 : (rank_delta > 0 ? 1 : -1);
        if (!(file_delta == 0 || rank_delta == 0 || std::abs(file_delta) == std::abs(rank_delta))) {
            return -1;
        }
        const int oriented_file = orient_for_attacker(attacker, file_step);
        const int oriented_rank = orient_for_attacker(attacker, rank_step);
        static constexpr std::array<std::pair<int, int>, 8> kDirs = {{
            { 1, -1 },
            { 0, -1 },
            { -1, -1 },
            { 1, 0 },
            { -1, 0 },
            { 1, 1 },
            { 0, 1 },
            { -1, 1 },
        }};
        for (size_t i = 0; i < kDirs.size(); ++i) {
            if (kDirs[i].first == oriented_file && kDirs[i].second == oriented_rank) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    int dfpn_check_knight_subphase(const Position& pos, const Color attacker, const Move move) {
        const int side = dfpn_check_knight_side_index(attacker, pos.kingSquare(oppositeColor(attacker)), move.to());
        if (side < 2) {
            return side * 2 + (move.isDrop() ? 1 : 0);
        }
        return 4 + (move.isDrop() ? 1 : 0);
    }

    int dfpn_check_gold_drop_subphase(const int dir_index) {
        switch (dir_index) {
        case 1: return 0;
        case 0: return 1;
        case 2: return 2;
        case 3: return 3;
        case 4: return 4;
        case 6: return 5;
        default: return 6;
        }
    }

    int dfpn_check_silver_drop_subphase(const int dir_index) {
        switch (dir_index) {
        case 5: return 0;
        case 7: return 1;
        case 1: return 2;
        case 0: return 3;
        case 2: return 4;
        default: return 5;
        }
    }

    int dfpn_check_bishop_drop_subphase(const int dir_index) {
        switch (dir_index) {
        case 5: return 0;
        case 7: return 1;
        case 0: return 2;
        case 2: return 3;
        default: return 4;
        }
    }

    int dfpn_check_rook_drop_subphase(const int dir_index) {
        switch (dir_index) {
        case 1: return 0;
        case 3: return 1;
        case 4: return 2;
        case 6: return 3;
        default: return 4;
        }
    }

    PieceType dfpn_check_piece_type_after_move(const Position& pos, const Move move) {
        if (move.isDrop()) {
            return move.pieceTypeDropped();
        }
        PieceType piece_type = pieceToPieceType(pos.piece(move.from()));
        if (move.isPromotion() && (piece_type & PTPromote) == 0) {
            piece_type = static_cast<PieceType>(piece_type + PTPromote);
        }
        return piece_type;
    }

    bool dfpn_check_move_is_direct_check(const Position& pos, const Color attacker, const Move move) {
        const Square to = move.to();
        const Square king = pos.kingSquare(oppositeColor(attacker));
        const PieceType piece_type_to = dfpn_check_piece_type_after_move(pos, move);

        switch (piece_type_to) {
        case Pawn:
            return pawnAttack(attacker, to).isSet(king);
        case Knight:
            return knightAttack(attacker, to).isSet(king);
        case Silver:
            return silverAttack(attacker, to).isSet(king);
        case Gold:
        case ProPawn:
        case ProLance:
        case ProKnight:
        case ProSilver:
            return goldAttack(attacker, to).isSet(king);
        case King:
            return kingAttack(to).isSet(king);
        case Lance: {
            const int file_delta = static_cast<int>(makeFile(king)) - static_cast<int>(makeFile(to));
            const int rank_delta = static_cast<int>(makeRank(king)) - static_cast<int>(makeRank(to));
            if (file_delta != 0 || (attacker == Black ? rank_delta >= 0 : rank_delta <= 0)) {
                return false;
            }
            Bitboard occupied = pos.occupiedBB();
            if (move.isDrop()) {
                occupied.setBit(to);
            }
            else {
                occupied.xorBit(move.from());
                occupied.setBit(to);
            }
            return !(betweenBB(to, king) & occupied).isAny();
        }
        case Bishop:
        case Horse: {
            if (piece_type_to == Horse && kingAttack(to).isSet(king)) {
                return true;
            }
            const int file_delta = static_cast<int>(makeFile(king)) - static_cast<int>(makeFile(to));
            const int rank_delta = static_cast<int>(makeRank(king)) - static_cast<int>(makeRank(to));
            if (std::abs(file_delta) != std::abs(rank_delta)) {
                return false;
            }
            Bitboard occupied = pos.occupiedBB();
            if (move.isDrop()) {
                occupied.setBit(to);
            }
            else {
                occupied.xorBit(move.from());
                occupied.setBit(to);
            }
            return !(betweenBB(to, king) & occupied).isAny();
        }
        case Rook:
        case Dragon: {
            if (piece_type_to == Dragon && kingAttack(to).isSet(king)) {
                return true;
            }
            const int file_delta = static_cast<int>(makeFile(king)) - static_cast<int>(makeFile(to));
            const int rank_delta = static_cast<int>(makeRank(king)) - static_cast<int>(makeRank(to));
            if (file_delta != 0 && rank_delta != 0) {
                return false;
            }
            Bitboard occupied = pos.occupiedBB();
            if (move.isDrop()) {
                occupied.setBit(to);
            }
            else {
                occupied.xorBit(move.from());
                occupied.setBit(to);
            }
            return !(betweenBB(to, king) & occupied).isAny();
        }
        default:
            return false;
        }
    }

    bool dfpn_check_move_is_discovered_check(const Position& pos, const Color attacker, const Move move) {
        if (move.isDrop()) {
            return false;
        }
        const Square king = pos.kingSquare(oppositeColor(attacker));
        return pos.discoveredCheckBB().isSet(move.from()) && !isAligned<true>(move.from(), move.to(), king);
    }

    int dfpn_check_discovered_long_phase(const Position& pos, const Color attacker, const Move move) {
        if (move.isDrop()) {
            return -1;
        }
        if (!pos.discoveredCheckBB().isSet(move.from())) {
            return -1;
        }
        const Square king = pos.kingSquare(oppositeColor(attacker));

        // OSL NumEffectState::makePinOpen marks the first piece from the king
        // when an attacker's long piece is behind it.  AddEffectWithEffect then
        // generates the opened move from the long-piece generator phase.
        if ((betweenBB(move.from(), king) & pos.occupiedBB()).isAny()) {
            return -1;
        }

        Bitboard occupied_without_blocker = pos.occupiedBB();
        occupied_without_blocker.xorBit(move.from());

        Bitboard rook_pinners = pos.bbOf(attacker) & pos.bbOf(Rook, Dragon)
            & rookAttack(king, occupied_without_blocker);
        while (rook_pinners) {
            const Square pinner = rook_pinners.firstOneFromSQ11();
            rook_pinners.clearBit(pinner);
            if ((betweenBB(pinner, king) & pos.occupiedBB()) == setMaskBB(move.from())) {
                return static_cast<int>(DfpnCheckGenerationPhase::RookLong);
            }
        }

        Bitboard bishop_pinners = pos.bbOf(attacker) & pos.bbOf(Bishop, Horse)
            & bishopAttack(king, occupied_without_blocker);
        while (bishop_pinners) {
            const Square pinner = bishop_pinners.firstOneFromSQ11();
            bishop_pinners.clearBit(pinner);
            if ((betweenBB(pinner, king) & pos.occupiedBB()) == setMaskBB(move.from())) {
                return static_cast<int>(DfpnCheckGenerationPhase::BishopLong);
            }
        }
        return -1;
    }

    std::optional<Square> dfpn_check_long_generator_square(
        const Position& pos, const Color attacker, const Move move, const int phase) {
        if (move.isDrop()) {
            return std::nullopt;
        }

        const PieceType moving_type = pieceToPieceType(pos.piece(move.from()));
        if (phase == static_cast<int>(DfpnCheckGenerationPhase::RookLong)
            && (moving_type == Rook || moving_type == Dragon)) {
            return move.from();
        }
        if (phase == static_cast<int>(DfpnCheckGenerationPhase::BishopLong)
            && (moving_type == Bishop || moving_type == Horse)) {
            return move.from();
        }

        const Square king = pos.kingSquare(oppositeColor(attacker));
        if ((betweenBB(move.from(), king) & pos.occupiedBB()).isAny()) {
            return std::nullopt;
        }

        Bitboard occupied_without_blocker = pos.occupiedBB();
        occupied_without_blocker.xorBit(move.from());
        Bitboard pinners;
        if (phase == static_cast<int>(DfpnCheckGenerationPhase::RookLong)) {
            pinners = pos.bbOf(attacker) & pos.bbOf(Rook, Dragon)
                & rookAttack(king, occupied_without_blocker);
        }
        else if (phase == static_cast<int>(DfpnCheckGenerationPhase::BishopLong)) {
            pinners = pos.bbOf(attacker) & pos.bbOf(Bishop, Horse)
                & bishopAttack(king, occupied_without_blocker);
        }
        else {
            return std::nullopt;
        }

        while (pinners) {
            const Square pinner = pinners.firstOneFromSQ11();
            pinners.clearBit(pinner);
            if ((betweenBB(pinner, king) & pos.occupiedBB()) == setMaskBB(move.from())) {
                return pinner;
            }
        }
        return std::nullopt;
    }

    int dfpn_check_move_phase(const Position& pos, const Color attacker, const Move move) {
        const Square king = pos.kingSquare(oppositeColor(attacker));
        if (move.isDrop()) {
            switch (move.pieceTypeDropped()) {
            case Pawn:
            case Lance:
                return static_cast<int>(DfpnCheckGenerationPhase::U);
            case Knight:
                return static_cast<int>(DfpnCheckGenerationPhase::Knight);
            case Gold:
                return static_cast<int>(DfpnCheckGenerationPhase::DropGold);
            case Silver:
                return static_cast<int>(DfpnCheckGenerationPhase::DropSilver);
            case Bishop:
                return static_cast<int>(DfpnCheckGenerationPhase::DropBishop);
            case Rook:
                return static_cast<int>(DfpnCheckGenerationPhase::DropRook);
            default:
                return static_cast<int>(DfpnCheckGenerationPhase::Other);
            }
        }

        const bool direct_check = dfpn_check_move_is_direct_check(pos, attacker, move);
        const PieceType piece_type_to = dfpn_check_piece_type_after_move(pos, move);
        const bool direct_unpromoted_long =
            direct_check && (piece_type_to == Bishop || piece_type_to == Rook);

        const int discovered_long_phase = dfpn_check_discovered_long_phase(pos, attacker, move);
        if (discovered_long_phase >= 0 && !direct_unpromoted_long) {
            return discovered_long_phase;
        }

        if (direct_check) {
            if (piece_type_to == Knight) {
                return static_cast<int>(DfpnCheckGenerationPhase::Knight);
            }
            const int adjacent_dir = dfpn_check_adjacent_dir_index(attacker, king, move.to());
            if (adjacent_dir >= 0) {
                return dfpn_check_adjacent_phase(adjacent_dir);
            }
            if (piece_type_to == Bishop) {
                return static_cast<int>(DfpnCheckGenerationPhase::BishopLong);
            }
            if (piece_type_to == Rook) {
                return static_cast<int>(DfpnCheckGenerationPhase::RookLong);
            }
            if (piece_type_to == Horse) {
                return static_cast<int>(DfpnCheckGenerationPhase::BishopLong);
            }
            if (piece_type_to == Dragon) {
                return static_cast<int>(DfpnCheckGenerationPhase::RookLong);
            }

            const int line_dir = dfpn_check_line_dir_index(attacker, king, move.to());
            if (line_dir >= 0) {
                return dfpn_check_adjacent_phase(line_dir);
            }
        }

        const bool discovered_check = dfpn_check_move_is_discovered_check(pos, attacker, move);
        if (discovered_check) {
            const int line_dir = dfpn_check_line_dir_index(attacker, king, move.from());
            if (line_dir >= 0) {
                return dfpn_check_adjacent_phase(line_dir);
            }
        }

        // OSL AddEffectWithEffect::generateKing emits remaining long-piece
        // checks from the rook/bishop long generators after adjacent phases.
        // If our direct-check classifier missed one, keep it in the same raw
        // phase instead of falling through to Other, because Dfpn::sort depends
        // on the resulting oldPtype segments.
        const PieceType moving_type = pieceToPieceType(pos.piece(move.from()));
        if (moving_type == Bishop || moving_type == Horse) {
            return static_cast<int>(DfpnCheckGenerationPhase::BishopLong);
        }
        if (moving_type == Rook || moving_type == Dragon) {
            return static_cast<int>(DfpnCheckGenerationPhase::RookLong);
        }

        return static_cast<int>(DfpnCheckGenerationPhase::Other);
    }

    int dfpn_check_move_subphase(const Position& pos, const Color attacker, const Move move, const int phase) {
        if (move.isDrop()) {
            const Square king = pos.kingSquare(oppositeColor(attacker));
            switch (move.pieceTypeDropped()) {
            case Pawn:
                return 1;
            case Lance:
                return 2;
            case Knight:
                return dfpn_check_knight_subphase(pos, attacker, move);
            case Gold:
                return dfpn_check_gold_drop_subphase(dfpn_check_adjacent_dir_index(attacker, king, move.to()));
            case Silver:
                return dfpn_check_silver_drop_subphase(dfpn_check_adjacent_dir_index(attacker, king, move.to()));
            case Bishop:
                return dfpn_check_bishop_drop_subphase(dfpn_check_line_dir_index(attacker, king, move.to()));
            case Rook:
                return dfpn_check_rook_drop_subphase(dfpn_check_line_dir_index(attacker, king, move.to()));
            default:
                return 0;
            }
        }

        if (phase == static_cast<int>(DfpnCheckGenerationPhase::Knight)) {
            return dfpn_check_knight_subphase(pos, attacker, move);
        }
        return 0;
    }

    auto dfpn_check_generation_raw_key(const Position& pos, const Color attacker, const Move move) {
        const int phase = dfpn_check_move_phase(pos, attacker, move);
        const int subphase = dfpn_check_move_subphase(pos, attacker, move, phase);
        const int to_key = osl_square_index(move.to());
        const int from_key = osl_piece_iteration_key(move);
        const int piece_key = static_cast<int>(move.pieceTypeFromOrDropped());
        const int promotion_key = move.isPromotion() ? 0 : 1;
        return std::make_tuple(phase, subphase, to_key, move.isDrop() ? 1 : 0, from_key, piece_key, promotion_key);
    }
#ifdef NDEBUG
#endif

    void sort_check_generation_raw_moves(const Position& pos, const Color turn, std::vector<Move>& moves) {
        std::sort(moves.begin(), moves.end(),
            [&](const Move lhs, const Move rhs) {
                return dfpn_check_generation_raw_key(pos, turn, lhs) < dfpn_check_generation_raw_key(pos, turn, rhs);
            });
    }

    template <class MoveContainer>
    void sort_moves(const Position& pos, const Color turn, MoveContainer& moves) {
        std::array<int, static_cast<size_t>(ColorNum) * static_cast<size_t>(SquareNum)> effect_count_cache;
        effect_count_cache.fill(-1);
        const auto cached_effect_count = [&](const Color color, const Square square) {
            const size_t index = static_cast<size_t>(color) * static_cast<size_t>(SquareNum) + static_cast<size_t>(square);
            int& value = effect_count_cache[index];
            if (value < 0) {
                value = effect_count(pos, color, square);
            }
            return value;
        };
        const auto sort_key = [&](const Move move) -> uint32_t {
            const int attack_support = cached_effect_count(turn, move.to()) + (move.isDrop() ? 1 : 0);
            const int defense_support = cached_effect_count(oppositeColor(turn), move.to());
            const int move_sort_turn_sign = turn == Black ? 1 : -1;
            const int file = static_cast<int>(makeFile(move.to())) + 1;
            const int to_y = move_sort_turn_sign * (static_cast<int>(makeRank(move.to())) + 1);
            const int to_x = (5 - std::abs(5 - file)) * 2 + (file > 5 ? 1 : 0);
            int from_to = (to_y * 16 + to_x) * 256;
            if (move.isDrop()) {
                from_to += osl_sort_ptype(move.pieceTypeDropped());
            }
            else {
                from_to += osl_square_index(move.from());
            }
            return ((attack_support > defense_support) ? 0x80000000u : 0u)
                | (static_cast<uint32_t>(from_to + 65536) << 1)
                | (move.isPromotion() ? 1u : 0u);
        };
        const auto sort_segment = [&](const size_t begin, const size_t end) {
            if (end <= begin + 1) {
                return;
            }
            std::array<MoveWithSortKey, kCheckOrEscapeMoveCapacity> keyed;
            assert(end - begin <= keyed.size());
            for (size_t i = begin; i < end; ++i) {
                keyed[i - begin] = MoveWithSortKey{ moves[i], sort_key(moves[i]) };
            }
            std::sort(keyed.begin(), keyed.begin() + static_cast<std::ptrdiff_t>(end - begin),
                [](const MoveWithSortKey& lhs, const MoveWithSortKey& rhs) {
                    return lhs.key > rhs.key;
                });
            for (size_t i = 0; i < end - begin; ++i) {
                moves[begin + i] = keyed[i].move;
            }
        };

        size_t last_sorted = 0;
        size_t cur = 0;
        PieceType last_piece_type = Occupied;
        for (; cur < moves.size(); ++cur) {
            const PieceType piece_type = osl_old_piece_type(pos, moves[cur]);
            if (moves[cur].isDrop() || piece_type == last_piece_type) {
                continue;
            }
            sort_segment(last_sorted, cur);
            last_sorted = cur;
            last_piece_type = piece_type;
        }
        sort_segment(last_sorted, cur);
    }

    LibertyInfo defender_king_liberty_info(const Position& pos, const Color attack_color) {
        const Color defender = oppositeColor(attack_color);
        const Square king = pos.kingSquare(defender);
        LibertyInfo info;

        for (size_t dir = 0; dir < kDirFileDelta.size(); ++dir) {
            const auto next = offset_square(king, kDirFileDelta[dir], kDirRankDelta[dir]);
            if (!next || is_edge_square(*next) || pos.bbOf(defender).isSet(*next)) {
                continue;
            }

            Bitboard occupied = pos.occupiedBB();
            occupied.clearBit(king);
            occupied.setBit(*next);
            if (!pos.attackersToIsAny(attack_color, *next, occupied)) {
                info.mask |= static_cast<uint8_t>(1u << dir);
            }
        }
        info.count = popcount8(info.mask);
        return info;
    }

    bool piece_has_effect_delta(const PieceType piece_type, const Color attacker, const int file_delta, const int rank_delta) {
        const Square from = SQ55;
        const auto target = offset_square(
            from,
            orient_for_attacker(attacker, file_delta),
            orient_for_attacker(attacker, rank_delta));
        if (!target) {
            return false;
        }
        return Position::attacksFrom(piece_type, attacker, from, allZeroBB()).isSet(*target);
    }

    unsigned int immediate_no_effect_mask(const PieceType piece_type, const int dir_index) {
        static const auto table = []() {
            std::array<std::array<unsigned short, 8>, PieceTypeNum> masks{};
            for (int piece = Occupied; piece < PieceTypeNum; ++piece) {
                const PieceType ptype = static_cast<PieceType>(piece);
                for (int dir = 0; dir < 8; ++dir) {
                    unsigned int mask = 0x1ffu;
                    const int dx = kDirFileDelta[dir];
                    const int dy = kDirRankDelta[dir];
                    const bool can_check_from_dir =
                        piece_has_effect_delta(ptype, Black, dx, dy)
                        || piece_has_effect_delta(ptype, Black, dx * 2, dy * 2);
                    if (can_check_from_dir) {
                        mask = 0;
                        for (int other = 0; other < 8; ++other) {
                            if (other == dir) {
                                continue;
                            }
                            const int rel_dx = dx - kDirFileDelta[other];
                            const int rel_dy = dy - kDirRankDelta[other];
                            if (!piece_has_effect_delta(ptype, Black, rel_dx, rel_dy)) {
                                mask |= (1u << other);
                            }
                        }
                    }
                    masks[piece][dir] = static_cast<unsigned short>(mask);
                }
            }
            return masks;
        }();
        return table[static_cast<size_t>(piece_type)][static_cast<size_t>(dir_index)];
    }

    unsigned int immediate_blocking_mask(const PieceType piece_type, const int dir_index) {
        static const auto table = []() {
            std::array<std::array<unsigned char, 8>, PieceTypeNum> masks{};
            for (int piece = Occupied; piece < PieceTypeNum; ++piece) {
                const PieceType ptype = static_cast<PieceType>(piece);
                for (int dir = 0; dir < 8; ++dir) {
                    unsigned int mask = 0;
                    const int dx = kDirFileDelta[dir];
                    const int dy = kDirRankDelta[dir];
                    const bool can_check_from_dir =
                        piece_has_effect_delta(ptype, Black, dx, dy)
                        || piece_has_effect_delta(ptype, Black, dx * 2, dy * 2);
                    if (can_check_from_dir) {
                        for (int other = 0; other < 8; ++other) {
                            const int dx1 = kDirFileDelta[other];
                            const int dy1 = kDirRankDelta[other];
                            const int rel_dx = dx - dx1;
                            const int rel_dy = dy - dy1;
                            const bool short_not_knight = (rel_dx != 0 || rel_dy != 0)
                                && (rel_dx == 0 || rel_dy == 0 || std::abs(rel_dx) == std::abs(rel_dy));
                            if (!piece_has_effect_delta(ptype, Black, rel_dx, rel_dy)
                                && short_not_knight
                                && !(dx == -dx1 && dy == -dy1)) {
                                mask |= (1u << other);
                            }
                        }
                    }
                    masks[piece][dir] = static_cast<unsigned char>(mask);
                }
            }
            return masks;
        }();
        return table[static_cast<size_t>(piece_type)][static_cast<size_t>(dir_index)];
    }

    bool immediate_can_checkmate_drop_ptype(const PieceType piece_type, const int dir_index, const uint8_t liberty_mask) {
        if (piece_type == King || piece_type == Pawn) {
            return false;
        }
        const int dx = kDirFileDelta[dir_index];
        const int dy = kDirRankDelta[dir_index];
        if (!piece_has_effect_delta(piece_type, Black, dx, dy)
            && !piece_has_effect_delta(piece_type, Black, dx * 2, dy * 2)) {
            return false;
        }
        for (int liberty_dir = 0; liberty_dir < 8; ++liberty_dir) {
            if ((liberty_mask & static_cast<uint8_t>(1u << liberty_dir)) == 0) {
                continue;
            }
            const int rel_dx = dx - kDirFileDelta[liberty_dir];
            const int rel_dy = dy - kDirRankDelta[liberty_dir];
            if (!piece_has_effect_delta(piece_type, Black, rel_dx, rel_dy)) {
                return false;
            }
        }
        return true;
    }

    bool immediate_slow_drop_ok(const Position& pos, const Color attack_color,
        const Square target_king, const PieceType piece_type, const int dir_index,
        const King8RuntimeInfo& king8_info) {
        unsigned int blocking_mask = immediate_blocking_mask(piece_type, dir_index)
            & static_cast<unsigned int>(king8_info.liberty_candidate);
        if (blocking_mask == 0) {
            return true;
        }

        const auto drop = king8_square(target_king, dir_index, attack_color);
        if (!drop) {
            return false;
        }

        Bitboard long_effect = effect_set_at(pos, attack_color, *drop);
        long_effect &= pos.bbOf(attack_color);
        long_effect &= pos.bbOf(Lance, Bishop, Rook, Horse, Dragon);
        if (!long_effect.isAny()) {
            return true;
        }
        while (blocking_mask != 0) {
            const int blocking_dir = lsb_u8(static_cast<uint8_t>(blocking_mask));
            blocking_mask &= blocking_mask - 1;
            const auto pos1 = king8_square(target_king, blocking_dir, attack_color);
            if (!pos1) {
                continue;
            }
            const Bitboard effect_at_pos1 = effect_set_at(pos, attack_color, *pos1);
            const int attack_effect_count = effect_at_pos1.popCount();
            if (attack_effect_count > 1) {
                continue;
            }
            Bitboard long_effect1 = effect_at_pos1 & long_effect;
            if (long_effect1.isAny()) {
                const Square from = long_effect1.firstOneFromSQ11();
                if (betweenBB(from, *pos1).isSet(*drop)) {
                    return false;
                }
            }
        }

        return true;
    }

    Move immediate_mate_move_by_osl_move_candidates(Position& pos, const Color attack_color,
        const Square target_king, const King8RuntimeInfo& king8_info, const bool fixed_probe_enabled,
        const OslPieceNumberState* current_piece_numbers) {
        const auto is_promotable_basic = [](const PieceType piece_type) {
            switch (piece_type) {
            case Pawn:
            case Lance:
            case Knight:
            case Silver:
            case Bishop:
            case Rook:
                return true;
            default:
                return false;
            }
        };
        const auto is_osl_major_basic_or_pawn = [](const PieceType piece_type) {
            return piece_type == Pawn || piece_type == Bishop || piece_type == Rook;
        };
        std::optional<Bitboard> pinned_attack;
        const auto king_open_move = [&](const Move move) {
            if (!pinned_attack) {
                pinned_attack = pinned_pieces_of(pos, attack_color);
            }
            return immediate_king_open_move(pos, attack_color, move, *pinned_attack);
        };
        const auto try_move = [&](const Move move, const int dir_index) -> Move {
            if (!move) {
                return Move::moveNone();
            }
            if (move.pieceTypeFrom() == King) {
                return Move::moveNone();
            }
            if (king_open_move(move)) {
                return Move::moveNone();
            }
            if (immediate_opponent_long_effect_follows(pos, attack_color, move)) {
                return Move::moveNone();
            }
            const unsigned int no_effect_mask = immediate_no_effect_mask(move.pieceTypeTo(), dir_index);
            if ((((unsigned int)king8_info.liberty | 0x100u) & no_effect_mask) != 0) {
                return Move::moveNone();
            }
            if (!immediate_candidate_preserves_liberty_candidate_effect(
                pos, attack_color, target_king, move, king8_info, dir_index)) {
                return Move::moveNone();
            }
            if (immediate_reject_prook_false_positive(pos, attack_color, target_king, move)) {
                return Move::moveNone();
            }
            return move;
        };

        for (int dir_index = 0; dir_index < 8; ++dir_index) {
            const uint8_t bit = static_cast<uint8_t>(1u << dir_index);
            if ((king8_info.move_candidate2 & bit) == 0) {
                continue;
            }
            const auto to = king8_square(target_king, dir_index, attack_color);
            if (!to) {
                continue;
            }
            const Bitboard effect_at_to = effect_set_at(pos, attack_color, *to);
            if (effect_at_to.popCount() < 2
                && !has_additional_attack_effect_at(pos, attack_color, *to)) {
                continue;
            }

            Bitboard attackers = effect_at_to & pos.bbOf(attack_color);
            attackers.clearBit(pos.kingSquare(attack_color));
            if (current_piece_numbers) {
                for (int num = 0; num < 40; ++num) {
                    Color owner = ColorNum;
                    Square from = SquareNum;
                    if (!osl_piece_number_info(current_piece_numbers, num, &owner, &from)
                        || owner != attack_color
                        || !isInSquare(from)
                        || !attackers.isSet(from)) {
                        continue;
                    }
                    const PieceType from_type = pieceToPieceType(pos.piece(from));
                    if (from_type == King || from_type == Empty) {
                        continue;
                    }
                    const bool can_promote_move = is_promotable_basic(from_type) && canPromote(attack_color, from, *to);
                    if (can_promote_move) {
                        if (const Move mate = try_move(makeCapturePromoteMove(from_type, from, *to, pos), dir_index)) {
                            return mate;
                        }
                        if (is_osl_major_basic_or_pawn(from_type)) {
                            continue;
                        }
                    }
                    if (const Move mate = try_move(makeCaptureMove(from_type, from, *to, pos), dir_index)) {
                        return mate;
                    }
                }
                continue;
            }

            std::array<Move, 64> candidates;
            size_t candidate_count = 0;
            const auto push_candidate = [&](const Move move) {
                assert(candidate_count < candidates.size());
                candidates[candidate_count++] = move;
            };
            while (attackers.isAny()) {
                const Square from = attackers.firstOneFromSQ11();
                const PieceType from_type = pieceToPieceType(pos.piece(from));
                if (from_type == King) {
                    continue;
                }
                const bool can_promote_move = is_promotable_basic(from_type) && canPromote(attack_color, from, *to);
                if (can_promote_move) {
                    push_candidate(makeCapturePromoteMove(from_type, from, *to, pos));
                    if (!is_osl_major_basic_or_pawn(from_type)) {
                        push_candidate(makeCaptureMove(from_type, from, *to, pos));
                    }
                }
                else {
                    push_candidate(makeCaptureMove(from_type, from, *to, pos));
                }
            }
            const auto less_candidate = [&](const Move lhs, const Move rhs) {
                const int lhs_num = immediate_osl_piece_number_from_state_or_position(pos, lhs.from(), current_piece_numbers);
                const int rhs_num = immediate_osl_piece_number_from_state_or_position(pos, rhs.from(), current_piece_numbers);
                if (lhs_num != rhs_num) {
                    return lhs_num < rhs_num;
                }
                return lhs.isPromotion() && !rhs.isPromotion();
            };
            for (size_t i = 1; i < candidate_count; ++i) {
                const Move candidate = candidates[i];
                size_t j = i;
                while (j > 0 && less_candidate(candidate, candidates[j - 1])) {
                    candidates[j] = candidates[j - 1];
                    --j;
                }
                candidates[j] = candidate;
            }
            for (size_t i = 0; i < candidate_count; ++i) {
                const Move move = candidates[i];
                if (const Move mate = try_move(move, dir_index)) {
                    return mate;
                }
            }
        }
        return Move::moveNone();
    }

    bool immediate_candidate_preserves_liberty_candidate_effect(const Position& pos, const Color attack_color,
        const Square target_king, const Move move, const King8RuntimeInfo& king8_info, const int dir_index) {
        if (move.isDrop() || dir_index < 0) {
            return true;
        }

        unsigned int mask = static_cast<unsigned int>(king8_info.liberty_candidate)
            & immediate_no_effect_mask(move.pieceTypeTo(), dir_index);
        if (mask == 0) {
            return true;
        }

        const Square from = move.from();
        const Square to = move.to();
        const Bitboard attackers_to_to = effect_set_at(pos, attack_color, to);
        const Bitboard from_effect = Position::attacksFrom(move.pieceTypeFrom(), attack_color, from, pos.occupiedBB());

        while (mask != 0) {
            const int liberty_dir = lsb_u8(static_cast<uint8_t>(mask));
            mask &= mask - 1;
            const auto pos1 = king8_square(target_king, liberty_dir, attack_color);
            if (!pos1) {
                continue;
            }

            const Bitboard effect_at_pos1 = effect_set_at(pos, attack_color, *pos1);
            int count = effect_at_pos1.popCount();
            if (from_effect.isSet(*pos1)) {
                --count;
            }
            if (count <= 0) {
                return false;
            }

            Bitboard blocking = effect_at_pos1 & attackers_to_to;
            while (blocking.isAny()) {
                const Square attacker_sq = blocking.firstOneFromSQ11();
                if (attacker_sq == from) {
                    continue;
                }
                const PieceType attacker_ptype = pieceToPieceType(pos.piece(attacker_sq));
                if (attacker_ptype != Lance
                    && attacker_ptype != Bishop
                    && attacker_ptype != Rook
                    && attacker_ptype != Horse
                    && attacker_ptype != Dragon) {
                    continue;
                }
                if (!betweenBB(attacker_sq, *pos1).isSet(to)) {
                    continue;
                }
                --count;
                if (count <= 0) {
                    return false;
                }
            }
        }

        return true;
    }

    bool immediate_same_king_ray(const Square king, const Square lhs, const Square rhs) {
        const int lhs_file = static_cast<int>(makeFile(lhs)) - static_cast<int>(makeFile(king));
        const int lhs_rank = static_cast<int>(makeRank(lhs)) - static_cast<int>(makeRank(king));
        const int rhs_file = static_cast<int>(makeFile(rhs)) - static_cast<int>(makeFile(king));
        const int rhs_rank = static_cast<int>(makeRank(rhs)) - static_cast<int>(makeRank(king));
        const auto normalize = [](const int value) {
            return value == 0 ? 0 : (value > 0 ? 1 : -1);
        };
        const auto on_king_ray = [](const int file_delta, const int rank_delta) {
            return file_delta == 0
                || rank_delta == 0
                || std::abs(file_delta) == std::abs(rank_delta);
        };
        if (!on_king_ray(lhs_file, lhs_rank) || !on_king_ray(rhs_file, rhs_rank)) {
            return false;
        }
        return normalize(lhs_file) == normalize(rhs_file)
            && normalize(lhs_rank) == normalize(rhs_rank);
    }

    bool immediate_king_open_move(
        const Position& pos, const Color attack_color, const Move move, const Bitboard& pinned_attack) {
        if (move.isDrop() || move.pieceTypeFrom() == King || !pinned_attack.isSet(move.from())) {
            return false;
        }
        return !immediate_same_king_ray(pos.kingSquare(attack_color), move.from(), move.to());
    }

    bool immediate_opponent_long_effect_follows(const Position& pos, const Color attack_color, const Move move) {
        if (move.isDrop() || move.pieceTypeFrom() == Knight) {
            return false;
        }

        const int file_delta = static_cast<int>(makeFile(move.to())) - static_cast<int>(makeFile(move.from()));
        const int rank_delta = static_cast<int>(makeRank(move.to())) - static_cast<int>(makeRank(move.from()));
        if (file_delta == 0 && rank_delta == 0) {
            return false;
        }
        if (file_delta != 0 && rank_delta != 0 && std::abs(file_delta) != std::abs(rank_delta)) {
            return false;
        }

        const int file_step = file_delta == 0 ? 0 : (file_delta > 0 ? 1 : -1);
        const int rank_step = rank_delta == 0 ? 0 : (rank_delta > 0 ? 1 : -1);
        const Color defense_color = oppositeColor(attack_color);
        Bitboard occupied_without_from = pos.occupiedBB();
        occupied_without_from.clearBit(move.from());

        // OSL checks longEffectNumTable()[moving_piece][direction(from,to)].
        // That table is keyed by the moving piece's current square; it does
        // not inspect opponent long effects beyond the destination square.
        std::optional<Square> scan = offset_square(move.from(), -file_step, -rank_step);
        while (scan) {
            const Piece piece = pos.piece(*scan);
            if (piece == Empty) {
                scan = offset_square(*scan, -file_step, -rank_step);
                continue;
            }
            if (pieceToColor(piece) != defense_color) {
                return false;
            }
            const PieceType piece_type = pieceToPieceType(piece);
            return (piece_type == Lance || piece_type == Bishop || piece_type == Rook
                || piece_type == Horse || piece_type == Dragon)
                && Position::attacksFrom(piece_type, defense_color, *scan, occupied_without_from).isSet(move.to());
        }

        return false;
    }

    int immediate_osl_piece_number_from_position(const Position& pos, const Square square) {
        if (!isInSquare(square)) {
            return 1024;
        }
        const Piece target_piece = pos.piece(square);
        if (target_piece == Empty) {
            return 1024;
        }

        const auto number_range = [](const PieceType piece_type) {
            switch (unpromote_piece_type(piece_type)) {
            case Pawn: return std::pair<int, int>{ 0, 18 };
            case Knight: return std::pair<int, int>{ 18, 22 };
            case Silver: return std::pair<int, int>{ 22, 26 };
            case Gold: return std::pair<int, int>{ 26, 30 };
            case King: return std::pair<int, int>{ 30, 32 };
            case Lance: return std::pair<int, int>{ 32, 36 };
            case Bishop: return std::pair<int, int>{ 36, 38 };
            case Rook: return std::pair<int, int>{ 38, 40 };
            default: return std::pair<int, int>{ 1024, 1024 };
            }
        };

        std::array<int, SquareNum> board_number;
        board_number.fill(-1);
        std::array<bool, 40> used{};

        const auto assign_piece = [&](const Color owner, const Square sq, const PieceType ptype) {
            const auto [begin, end] = number_range(ptype);
            for (int num = begin; num < end; ++num) {
                if (used[static_cast<size_t>(num)]) {
                    continue;
                }
                if (ptype == King && num != 30 + static_cast<int>(owner)) {
                    continue;
                }
                used[static_cast<size_t>(num)] = true;
                if (isInSquare(sq)) {
                    board_number[sq] = num;
                }
                return;
            }
        };

        for (int rank = static_cast<int>(Rank1); rank <= static_cast<int>(Rank9); ++rank) {
            for (int file = static_cast<int>(File9); file >= static_cast<int>(File1); --file) {
                const Square sq = makeSquare(static_cast<File>(file), static_cast<Rank>(rank));
                const Piece piece = pos.piece(sq);
                if (piece == Empty) {
                    continue;
                }
                assign_piece(pieceToColor(piece), sq, pieceToPieceType(piece));
            }
        }

        return board_number[square] >= 0 ? board_number[square] : 1024;
    }

    bool immediate_reject_prook_false_positive(const Position& pos, const Color attack_color,
        const Square target_king, const Move move) {
        if (move.isDrop() || move.pieceTypeTo() != Dragon) {
            return false;
        }

        const Square from = move.from();
        const Square to = move.to();
        const int dx = static_cast<int>(makeFile(target_king)) - static_cast<int>(makeFile(to));
        const int dy = static_cast<int>(makeRank(target_king)) - static_cast<int>(makeRank(to));
        if (std::abs(dx) != 1 || std::abs(dy) != 1) {
            return false;
        }

        const Color defense_color = oppositeColor(attack_color);
        Bitboard occupied_after = pos.occupiedBB();
        occupied_after.clearBit(from);
        occupied_after.setBit(to);
        const auto from_effect = Position::attacksFrom(move.pieceTypeFrom(), attack_color, from, pos.occupiedBB());
        const auto moved_effect = Position::attacksFrom(move.pieceTypeTo(), attack_color, to, occupied_after);

        const auto rejects_by_escape_square = [&](const std::optional<Square>& pos2) {
            if (!pos2) {
                return false;
            }
            const Piece piece2 = pos.piece(*pos2);
            if (piece2 != Empty && pieceToColor(piece2) == defense_color) {
                return false;
            }
            const Bitboard effect_at_pos2 = effect_set_at(pos, attack_color, *pos2);
            int count = effect_at_pos2.popCount();
            const int original_count = count;
            if (from_effect.isSet(*pos2)) {
                --count;
            }
            if (moved_effect.isSet(*pos2)) {
                ++count;
            }
            return count == 0 || (count == 1 && moved_effect.isSet(*pos2));
        };
        const auto moved_piece_matches_offset = [&](const int file_delta, const int rank_delta) {
            const auto sq = offset_square(target_king, file_delta, rank_delta);
            return sq && *sq == from;
        };
        const auto piece_attacks_to = [&](const Square piece_sq, const Square dst) {
            const Piece piece = pos.piece(piece_sq);
            if (piece == Empty) {
                return false;
            }
            return Position::attacksFrom(pieceToPieceType(piece), pieceToColor(piece), piece_sq, pos.occupiedBB()).isSet(dst);
        };

        {
            const auto pos1 = offset_square(to, dx, 0);
            if (pos1) {
                const Piece p1 = pos.piece(*pos1);
                if (p1 != Empty) {
                    const auto pos2 = offset_square(to, dx * 2, 0);
                    if (rejects_by_escape_square(pos2)) {
                        return true;
                    }
                    if (moved_piece_matches_offset(0, -2 * dy) && piece_attacks_to(*pos1, to)) {
                        return true;
                    }
                }
            }
        }
        {
            const auto pos1 = offset_square(to, 0, dy);
            if (pos1) {
                const Piece p1 = pos.piece(*pos1);
                if (p1 != Empty) {
                    const auto pos2 = offset_square(to, 0, 2 * dy);
                    if (rejects_by_escape_square(pos2)) {
                        return true;
                    }
                    if (moved_piece_matches_offset(-2 * dx, 0) && piece_attacks_to(*pos1, to)) {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    bool has_additional_attack_effect(const Position& pos, const Color attacker, const Move move);

    LibertyEstimate effective_check_short(const PieceType piece_type, const int dir_index, const uint8_t liberty_mask) {
        if (piece_type == King) {
            return { 0, false };
        }

        const int file_delta = kDirFileDelta[dir_index];
        const int rank_delta = kDirRankDelta[dir_index];
        const bool has_effect = piece_has_effect_delta(piece_type, Black, file_delta, rank_delta)
            || piece_has_effect_delta(piece_type, Black, file_delta * 2, rank_delta * 2);

        int count = 0;
        for (size_t liberty_dir = 0; liberty_dir < kDirFileDelta.size(); ++liberty_dir) {
            if ((liberty_mask & (1u << liberty_dir)) == 0) {
                continue;
            }
            const int target_file_delta = file_delta - kDirFileDelta[liberty_dir];
            const int target_rank_delta = rank_delta - kDirRankDelta[liberty_dir];
            if ((file_delta != kDirFileDelta[liberty_dir] || rank_delta != kDirRankDelta[liberty_dir])
                && !piece_has_effect_delta(piece_type, Black, target_file_delta, target_rank_delta)) {
                ++count;
            }
        }
        return { static_cast<uint8_t>(std::max(count, 1)), has_effect };
    }

    LibertyEstimate effective_check_long(PieceType piece_type, const int dir_index, const uint8_t liberty_mask) {
        const int file_delta = kDirFileDelta[dir_index] * 2;
        const int rank_delta = kDirRankDelta[dir_index] * 2;
        const bool has_effect = piece_has_effect_delta(piece_type, Black, file_delta, rank_delta);

        int count = 0;
        for (size_t liberty_dir = 0; liberty_dir < kDirFileDelta.size(); ++liberty_dir) {
            if ((liberty_mask & (1u << liberty_dir)) == 0) {
                continue;
            }
            const int target_file_delta = file_delta - kDirFileDelta[liberty_dir];
            const int target_rank_delta = rank_delta - kDirRankDelta[liberty_dir];
            if (!piece_has_effect_delta(piece_type, Black, target_file_delta, target_rank_delta)) {
                ++count;
            }
        }
        return { static_cast<uint8_t>(std::max(count, 1)), has_effect };
    }

    ProofNumberRuntimeTables::ProofNumberRuntimeTables() {
        for (size_t color_index = 0; color_index < 2; ++color_index) {
            for (int liberty_mask = 0; liberty_mask < 0x100; ++liberty_mask) {
                for (int piece = Pawn; piece < PieceTypeNum; ++piece) {
                    for (size_t dir = 0; dir < kDirFileDelta.size(); ++dir) {
                        short_liberties[color_index][liberty_mask][piece][dir] =
                            effective_check_short(static_cast<PieceType>(piece), static_cast<int>(dir), static_cast<uint8_t>(liberty_mask));
                        long_liberties[color_index][liberty_mask][piece][dir] =
                            effective_check_long(static_cast<PieceType>(piece), static_cast<int>(dir), static_cast<uint8_t>(liberty_mask));
                    }
                }
            }

            for (auto& by_result : pmajor_liberty[color_index]) {
                by_result.fill(8);
            }
            for (auto& by_result : promote_liberty[color_index]) {
                by_result.fill(8);
            }
            for (auto& by_result : other_move_liberty[color_index]) {
                by_result.fill(8);
            }

            for (int liberty_drop_mask = 0; liberty_drop_mask < 0x10000; ++liberty_drop_mask) {
                const uint8_t liberty_mask = static_cast<uint8_t>((liberty_drop_mask >> 8) & 0xff);
                const int liberty_count = popcount8(liberty_mask);
                if (liberty_count <= 2) {
                    continue;
                }

                for (size_t piece_index = 0; piece_index < kStandPieceTypes.size(); ++piece_index) {
                    const PieceType piece_type = kStandPieceTypes[piece_index];
                    int minimum_liberty = liberty_count;
                    for (size_t dir = 0; dir < kDirFileDelta.size(); ++dir) {
                        if ((liberty_drop_mask & (0x1 << dir)) == 0) {
                            continue;
                        }
                        if ((liberty_drop_mask & (0x100 << dir)) != 0) {
                            continue;
                        }
                        const LibertyEstimate estimate = short_liberties[color_index][liberty_mask][piece_type][dir];
                        if (!estimate.has_effect) {
                            continue;
                        }
                        minimum_liberty = std::min(minimum_liberty, static_cast<int>(estimate.liberty));
                    }
                    for (int value = minimum_liberty; value < liberty_count; ++value) {
                        drop_liberty[color_index][liberty_drop_mask][value] |= static_cast<uint8_t>(1u << piece_index);
                    }
                }
            }

            for (int liberty_mask = 0; liberty_mask < 0x100; ++liberty_mask) {
                const int liberty_count = popcount8(static_cast<uint8_t>(liberty_mask));
                for (int move_mask = 0; move_mask < 0x100; ++move_mask) {
                    if ((liberty_mask & move_mask) != 0) {
                        continue;
                    }

                    int minimum_liberty = std::max(2, liberty_count) - 1;
                    if (minimum_liberty > 1) {
                        for (size_t dir = 0; dir < kDirFileDelta.size(); ++dir) {
                            if ((move_mask & (1 << dir)) == 0) {
                                continue;
                            }
                            minimum_liberty = std::min(minimum_liberty,
                                static_cast<int>(short_liberties[color_index][liberty_mask][Dragon][dir].liberty));
                            minimum_liberty = std::min(minimum_liberty,
                                static_cast<int>(short_liberties[color_index][liberty_mask][Horse][dir].liberty));
                        }
                    }
                    pmajor_liberty[color_index][liberty_mask][move_mask] = static_cast<uint8_t>(std::max(minimum_liberty, 1));

                    minimum_liberty = std::max(2, liberty_count) - 1;
                    if (minimum_liberty > 1) {
                        for (size_t dir = 0; dir < kDirFileDelta.size(); ++dir) {
                            if ((move_mask & (1 << dir)) == 0) {
                                continue;
                            }
                            for (const PieceType piece_type : kMoveLibertyPieceTypes) {
                                const LibertyEstimate estimate = short_liberties[color_index][liberty_mask][piece_type][dir];
                                if (!estimate.has_effect) {
                                    continue;
                                }
                                minimum_liberty = std::min(minimum_liberty, static_cast<int>(estimate.liberty));
                            }
                        }
                    }
                    promote_liberty[color_index][liberty_mask][move_mask] = static_cast<uint8_t>(std::max(minimum_liberty, 1));

                    minimum_liberty = std::max(2, liberty_count) - 1;
                    if (minimum_liberty > 1) {
                        for (size_t dir = 0; dir < kDirFileDelta.size(); ++dir) {
                            if ((move_mask & (1 << dir)) == 0) {
                                continue;
                            }
                            for (const PieceType piece_type : kMoveLibertyPieceTypes) {
                                if (dir == 1 && (piece_type == Gold
                                    || piece_type == ProPawn
                                    || piece_type == ProLance
                                    || piece_type == ProKnight
                                    || piece_type == ProSilver)) {
                                    continue;
                                }
                                const LibertyEstimate estimate = short_liberties[color_index][liberty_mask][piece_type][dir];
                                if (!estimate.has_effect) {
                                    continue;
                                }
                                minimum_liberty = std::min(minimum_liberty, static_cast<int>(estimate.liberty));
                            }
                        }
                    }
                    other_move_liberty[color_index][liberty_mask][move_mask] = static_cast<uint8_t>(std::max(minimum_liberty, 1));
                }
            }
        }
    }

    const ProofNumberRuntimeTables& proof_number_runtime_tables() {
        static const ProofNumberRuntimeTables tables;
        return tables;
    }

    uint8_t stand_piece_mask(const Position& pos, const Color attack_color) {
        uint8_t mask = 0;
        for (size_t piece_index = 0; piece_index < kStandPieceTypes.size(); ++piece_index) {
            const HandPiece hand_piece = pieceTypeToHandPiece(kStandPieceTypes[piece_index]);
            if (pos.hand(attack_color).exists(hand_piece)) {
                mask |= static_cast<uint8_t>(1u << piece_index);
            }
        }
        return mask;
    }

    int estimate_attack_liberty(const Position& pos, const Color attacker, const LibertyInfo& liberty_info, const Move move) {
        const Color defender = oppositeColor(attacker);
        const Square king = pos.kingSquare(defender);
        const Square to = move.to();
        PieceType piece_type = osl_move_ptype(pos, move);
        const auto defender_effect_count_at_to = [&]() -> int {
            return effect_count(pos, defender, to);
        };
        const auto attacker_effect_count_at_to = [&]() -> int {
            return effect_count(pos, attacker, to);
        };
        if (piece_type == Knight) {
            const int defender_count = defender_effect_count_at_to();
            return std::max(1, static_cast<int>(liberty_info.count) + defender_count);
        }

        const int file_delta = static_cast<int>(makeFile(king)) - static_cast<int>(makeFile(to));
        const int rank_delta = static_cast<int>(makeRank(king)) - static_cast<int>(makeRank(to));
        const bool neighboring = std::abs(file_delta) <= 1 && std::abs(rank_delta) <= 1;

        LibertyEstimate liberty;
        if (neighboring) {
            const int dir_index = dir_index_from_delta(attacker, file_delta, rank_delta);
            if (dir_index < 0) {
                return std::max(1, static_cast<int>(liberty_info.count) - 1);
            }
            liberty = effective_check_short(piece_type, dir_index, liberty_info.mask);
        }
        else {
            const int step_file = (file_delta == 0) ? 0 : (file_delta > 0 ? 1 : -1);
            const int step_rank = (rank_delta == 0) ? 0 : (rank_delta > 0 ? 1 : -1);
            const int dir_index = dir_index_from_delta(attacker, step_file, step_rank);
            if (dir_index < 0) {
                return std::max(1, static_cast<int>(liberty_info.count) - 1);
            }
            const bool straight_or_diag = file_delta == 0 || rank_delta == 0 || std::abs(file_delta) == std::abs(rank_delta);
            if (!straight_or_diag) {
                return std::max(1, static_cast<int>(liberty_info.count) - 1);
            }
            if (std::abs(file_delta) > 2 || std::abs(rank_delta) > 2) {
                if (piece_type == Bishop || piece_type == Rook || piece_type == Horse || piece_type == Dragon) {
                    piece_type = unpromote_piece_type(piece_type);
                }
                else if (piece_type != Lance) {
                    return std::max(1, static_cast<int>(liberty_info.count) - 1);
                }
            }
            liberty = effective_check_long(piece_type, dir_index, liberty_info.mask);
        }

        int value = liberty.liberty;
        if (value == 0) {
            return std::max(static_cast<int>(liberty_info.count) - 1, 1);
        }
        if (!neighboring && liberty.has_effect) {
            ++value;
        }
        const int defender_count = defender_effect_count_at_to();
        value += defender_count;
        if (move.isDrop()) {
            const int attacker_count = neighboring ? attacker_effect_count_at_to() : 0;
            if (neighboring && attacker_count > 0) {
                --value;
            }
            return std::max(value, 1);
        }
        const int attacker_count = neighboring ? attacker_effect_count_at_to() : 0;
        if (neighboring && (attacker_count >= 2 || has_additional_attack_effect(pos, attacker, move))) {
            --value;
        }
        return std::max(value, 1);
    }

    bool has_additional_attack_effect(const Position& pos, const Color attacker, const Move move) {
        return !move.isDrop() && has_additional_attack_effect_target(pos, attacker, move.to());
    }

    bool is_unattacked_drop_escape(const Position& pos, const Color attack_color, const Move move) {
        return move.isDrop() && !effect_has_at(pos, oppositeColor(attack_color), move.to());
    }

    enum class LongDropClass : uint8_t {
        None,
        Lance,
        Bishop,
        Rook,
    };

    bool has_unblockable_effect_to_king(const PieceType piece_type, const Color attack_color, const Square from, const Square king) {
        const int file_delta = orient_for_attacker(
            attack_color,
            static_cast<int>(makeFile(king)) - static_cast<int>(makeFile(from)));
        const int rank_delta = orient_for_attacker(
            attack_color,
            static_cast<int>(makeRank(king)) - static_cast<int>(makeRank(from)));
        const int abs_file = std::abs(file_delta);
        const int abs_rank = std::abs(rank_delta);
        const bool adjacent = std::max(abs_file, abs_rank) == 1;

        switch (piece_type) {
        case Pawn:
            return file_delta == 0 && rank_delta == -1;
        case Lance:
            return file_delta == 0 && rank_delta == -1;
        case Knight:
            return abs_file == 1 && rank_delta == -2;
        case Silver:
            return adjacent
                && (rank_delta == -1 || (abs_file == 1 && rank_delta == 1));
        case Gold:
        case ProPawn:
        case ProLance:
        case ProKnight:
        case ProSilver:
            return adjacent && !(abs_file == 1 && rank_delta == 1);
        case Bishop:
            return adjacent && abs_file == abs_rank;
        case Rook:
            return adjacent && ((abs_file == 0 && abs_rank == 1) || (abs_file == 1 && abs_rank == 0));
        case Horse:
            return adjacent;
        case Dragon:
            return adjacent;
        case King:
            return adjacent;
        default:
            return false;
        }
    }

    LongDropClass long_drop_attack_class(const Position& pos, const Color attack_color, const Move move, const Square king) {
        PieceType effect_type = Occupied;
        if (move.isDrop()) {
            effect_type = move.pieceTypeDropped();
        }
        else {
            const PieceType ptype = osl_move_ptype(pos, move);
            if ((ptype != Horse && ptype != Dragon)
                || move.isCapture()
                || move.isPromotion()
                || effect_has_at(pos, oppositeColor(attack_color), move.to())) {
                return LongDropClass::None;
            }
            effect_type = ptype;
        }

        if (has_unblockable_effect_to_king(effect_type, attack_color, move.to(), king)) {
            return LongDropClass::None;
        }

        switch (unpromote_piece_type(effect_type)) {
        case Rook:
            return LongDropClass::Rook;
        case Bishop:
            return LongDropClass::Bishop;
        default:
            return LongDropClass::Lance;
        }
    }

    int count_liberty_runtime(const Position& pos, const Color attacker, const King8RuntimeInfo& info, const Move move) {
        return estimate_attack_liberty(pos, attacker, LibertyInfo{ info.liberty, info.liberty_count }, move);
    }

    ProofDisproof estimate_attack_pdp_with_support(const Position& pos, const Color attacker,
        const King8RuntimeInfo& king8_info, const Move move, const int attack_support_base,
        const int defense_support) {
        const uint32_t base_proof_number = static_cast<uint32_t>(count_liberty_runtime(pos, attacker, king8_info, move));
        uint32_t proof_number = base_proof_number;
        uint32_t disproof_number = 1;

        const Color defender = oppositeColor(attacker);
        if (defense_support >= 2) {
            ++proof_number;
        }

        const int attack_support = attack_support_base + (move.isDrop() ? 1 : 0);
        if (attack_support > defense_support) {
            disproof_number = 2;
        }
        else if (move.isCapture()) {
            const PieceType captured_piece = unpromote_piece_type(move.cap());
            if (captured_piece == Silver || captured_piece == Gold) {
                disproof_number = 2;
            }
            else {
                ++proof_number;
                disproof_number = 1;
            }
        }
        else {
            ++proof_number;
            disproof_number = 1;
        }
        return { proof_number, disproof_number };
    }

    ProofDisproof estimate_attack_pdp(const Position& pos, const Color attacker, const King8RuntimeInfo& king8_info, const Move move) {
        const Square to = move.to();
        const int attack_support_base = effect_count(pos, attacker, to);
        const int defense_support = effect_count(pos, oppositeColor(attacker), to);
        return estimate_attack_pdp_with_support(pos, attacker, king8_info, move, attack_support_base, defense_support);
    }

    int attack_proof_cost_with_support(const Position& pos, const Color attacker, const Move move,
        const int attack_support, const int defense_support) {
        static constexpr std::array<int8_t, PieceTypeNum> kAttackSacrificeCost = {
            0,  // Occupied
            1,  // Pawn
            2,  // Lance
            2,  // Knight
            2,  // Silver
            3,  // Bishop
            3,  // Rook
            4,  // Gold
            0,  // King
            1,  // ProPawn
            2,  // ProLance
            2,  // ProKnight
            2,  // ProSilver
            4,  // Horse
            4   // Dragon
        };

        if (move.isCapture()) {
            return 0;
        }
        if (attack_support > defense_support) {
            return 0;
        }

        int proof = kAttackSacrificeCost[static_cast<size_t>(osl_move_ptype(pos, move))];
        if (defense_support >= 2 && attack_support == defense_support) {
            proof /= 2;
        }
        return proof;
    }

    int attack_proof_cost(const Position& pos, const Color attacker, const Move move) {
        const Square to = move.to();
        const int attack_support = effect_count(pos, attacker, to) + (move.isDrop() ? 1 : 0);
        const int defense_support = effect_count(pos, oppositeColor(attacker), to);
        return attack_proof_cost_with_support(pos, attacker, move, attack_support, defense_support);
    }

    class EffectSetCache {
    public:
        void ensure_position(const Position& pos) {
            const Key key = pos.getBoardKey();
            if (valid_ && board_key_ == key) {
                return;
            }
            valid_ = true;
            board_key_ = key;
            ++generation_;
            if (generation_ == 0) {
                known_generation_.fill(0);
                known_count_generation_.fill(0);
                generation_ = 1;
            }
        }

        Bitboard get(const Position& pos, const Color c, const Square sq) {
            ensure_position(pos);
            const size_t offset = static_cast<size_t>(c) * static_cast<size_t>(SquareNum)
                + static_cast<size_t>(sq);
            ensure_effect(pos, c, sq, offset);
            return effects_[offset];
        }

        int count(const Position& pos, const Color c, const Square sq) {
            ensure_position(pos);
            const size_t offset = static_cast<size_t>(c) * static_cast<size_t>(SquareNum)
                + static_cast<size_t>(sq);
            ensure_effect(pos, c, sq, offset);
            if (known_count_generation_[offset] != generation_) {
                known_count_generation_[offset] = generation_;
                counts_[offset] = effects_[offset].popCount();
            }
            return counts_[offset];
        }

        bool any(const Position& pos, const Color c, const Square sq) {
            ensure_position(pos);
            const size_t offset = static_cast<size_t>(c) * static_cast<size_t>(SquareNum)
                + static_cast<size_t>(sq);
            ensure_effect(pos, c, sq, offset);
            return effects_[offset].isAny();
        }

    private:
        void ensure_effect(const Position& pos, const Color c, const Square sq, const size_t offset) {
            if (known_generation_[offset] != generation_) {
                known_generation_[offset] = generation_;
                effects_[offset] = pos.attackersTo(c, sq);
            }
        }

        bool valid_ = false;
        Key board_key_ = 0;
        uint16_t generation_ = 0;
        std::array<uint16_t, static_cast<size_t>(ColorNum) * static_cast<size_t>(SquareNum)> known_generation_{};
        std::array<uint16_t, static_cast<size_t>(ColorNum) * static_cast<size_t>(SquareNum)> known_count_generation_{};
        std::array<Bitboard, static_cast<size_t>(ColorNum) * static_cast<size_t>(SquareNum)> effects_{};
        std::array<int8_t, static_cast<size_t>(ColorNum) * static_cast<size_t>(SquareNum)> counts_{};
    };

    inline EffectSetCache& effect_cache() {
        static thread_local EffectSetCache cache;
        return cache;
    }

    inline Bitboard effect_set_at(const Position& pos, const Color c, const Square sq) {
        EffectSetCache& cache = effect_cache();
        return cache.get(pos, c, sq);
    }

    inline bool effect_has_at(const Position& pos, const Color c, const Square sq) {
        EffectSetCache& cache = effect_cache();
        return cache.any(pos, c, sq);
    }

    inline int effect_count(const Position& pos, const Color c, const Square sq) {
        EffectSetCache& cache = effect_cache();
        return cache.count(pos, c, sq);
    }

    inline bool has_multiple_effect_at(const Position& pos, const Color c, const Square sq) {
        EffectSetCache& cache = effect_cache();
        const Bitboard effects = cache.get(pos, c, sq);
        const uint64_t lo = effects.p(0);
        const uint64_t hi = effects.p(1);
        return (lo & (lo - 1)) != 0 || (hi & (hi - 1)) != 0 || (lo != 0 && hi != 0);
    }

    inline void do_known_check_move(Position& pos, const Move move, StateInfo& st) {
        const CheckInfo ci(pos);
        pos.doMove(move, st, ci, true);
    }

    template <Color THEM>
    void make_banned_king_to(Bitboard& banned_king_to_bb, const Position& pos, const Square check_sq, const Square king_sq) {
        switch (pos.piece(check_sq)) {
        case (THEM == Black ? BPawn : WPawn):
        case (THEM == Black ? BKnight : WKnight):
            break;
        case (THEM == Black ? BLance : WLance):
            banned_king_to_bb |= lanceAttackToEdge(THEM, check_sq);
            break;
        case (THEM == Black ? BSilver : WSilver):
            banned_king_to_bb |= silverAttack(THEM, check_sq);
            break;
        case (THEM == Black ? BGold : WGold):
        case (THEM == Black ? BProPawn : WProPawn):
        case (THEM == Black ? BProLance : WProLance):
        case (THEM == Black ? BProKnight : WProKnight):
        case (THEM == Black ? BProSilver : WProSilver):
            banned_king_to_bb |= goldAttack(THEM, check_sq);
            break;
        case (THEM == Black ? BBishop : WBishop):
            banned_king_to_bb |= bishopAttackToEdge(check_sq);
            break;
        case (THEM == Black ? BHorse : WHorse):
            banned_king_to_bb |= horseAttackToEdge(check_sq);
            break;
        case (THEM == Black ? BRook : WRook):
            banned_king_to_bb |= rookAttackToEdge(check_sq);
            break;
        case (THEM == Black ? BDragon : WDragon):
            if (squareRelation(check_sq, king_sq) & DirecDiag) {
                banned_king_to_bb |= pos.attacksFrom<Dragon>(check_sq);
            }
            else {
                banned_king_to_bb |= dragonAttackToEdge(check_sq);
            }
            break;
        default:
            UNREACHABLE;
        }
    }

    std::vector<Move> generate_king_escape_moves(Position& pos) {
        assert(pos.inCheck());

        const Color us = pos.turn();
        const Color them = oppositeColor(us);
        const Square king_sq = pos.kingSquare(us);
        const Bitboard checkers = pos.checkersBB();
        Bitboard bb = checkers;
        Bitboard banned_king_to_bb = allZeroBB();
        int checker_count = 0;

        do {
            const Square check_sq = bb.firstOneFromSQ11();
            assert(pieceToColor(pos.piece(check_sq)) == them);
            ++checker_count;
            if (them == Black) {
                make_banned_king_to<Black>(banned_king_to_bb, pos, check_sq, king_sq);
            }
            else {
                make_banned_king_to<White>(banned_king_to_bb, pos, check_sq, king_sq);
            }
        } while (bb);

        bb = banned_king_to_bb.notThisAnd(pos.bbOf(us).notThisAnd(kingAttack(king_sq)));
        std::vector<Move> moves;
        moves.reserve(static_cast<size_t>(bb.popCount()));
        FOREACH_BB(bb, const Square to, {
            moves.push_back(makeNonPromoteMove<Capture>(King, king_sq, to, pos));
        });

        (void)checker_count;
        return moves;
    }

    std::vector<Move> generate_all_legal_evasion_moves(Position& pos) {
        std::array<ExtMove, kMoveBufferSize> buffer;
        ExtMove* last = generateMoves<Evasion>(buffer.data(), pos);
        const Bitboard pinned = pos.pinnedBB();
        ExtMove* cur = buffer.data();
        while (cur != last) {
            if (!pos.pseudoLegalMoveIsLegal<false, false>(cur->move, pinned)) {
                cur->move = (--last)->move;
            }
            else {
                ++cur;
            }
        }

        std::vector<Move> moves;
        moves.reserve(static_cast<size_t>(last - buffer.data()));
        for (ExtMove* it = buffer.data(); it != last; ++it) {
            moves.push_back(it->move);
        }
        return moves;
    }

    void generate_cheap_escape_moves(Position& pos, std::vector<Move>& moves) {
        std::array<ExtMove, kMoveBufferSize> buffer;
        ExtMove* last = generateOslmateEscapeMoves(buffer.data(), pos, true, false);
        moves.clear();
        moves.reserve(static_cast<size_t>(last - buffer.data()));
        for (ExtMove* it = buffer.data(); it != last; ++it) {
            moves.push_back(it->move);
        }
    }

    std::vector<Move> generate_cheap_escape_moves(Position& pos) {
        std::vector<Move> moves;
        generate_cheap_escape_moves(pos, moves);
        return moves;
    }

    bool has_ignored_unpromote_escape(const Move move, const Color defender) {
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

    bool has_ignored_unpromote_check(const Move move, const Color attacker) {
        if (move.isDrop() || !move.isPromotion()) {
            return false;
        }

        switch (move.pieceTypeFrom()) {
        case Pawn:
            return (attacker == Black && makeRank(move.to()) != Rank1)
                || (attacker == White && makeRank(move.to()) != Rank9);
        case Lance:
            return makeRank(move.to()) == (attacker == Black ? Rank2 : Rank8);
        case Bishop:
        case Rook:
            return true;
        default:
            return false;
        }
    }

    bool is_ignored_unpromote_check_variant(const Move move, const Color attacker) {
        if (move.isDrop() || move.isPromotion()) {
            return false;
        }

        // Match OSL Move::ignoreUnpromote<P>().  A normal pawn push check
        // outside the promotion zone is not an ignored unpromote variant.
        switch (move.pieceTypeFrom()) {
        case Pawn:
            return canPromote(attacker, move.to());
        case Lance:
            return makeRank(move.to()) == (attacker == Black ? Rank2 : Rank8);
        case Bishop:
        case Rook:
            return canPromote(attacker, move.from(), move.to());
        default:
            return false;
        }
    }

    bool should_append_ignored_unpromote_check(Position& pos, const Move promoted, const Move unpromoted, const Color attacker) {
        if (!promoted.isPromotion() || !unpromoted) {
            return false;
        }
        // OSL generateCheck appends Move::unpromote() only when the
        // unpromoted piece still attacks the king or the moved piece is
        // in pinOrOpen(alt(P)).
        return unpromoted_piece_has_effect_to_king(pos, unpromoted, attacker)
            || osl_pin_or_open_shadow(pos, attacker, promoted.from());
    }

    Move unpromote_counterpart(const Position& pos, const Move promoted) {
        (void)pos;
        if (!promoted || promoted.isDrop() || !promoted.isPromotion()) {
            return Move::moveNone();
        }
        return Move(static_cast<u32>(promoted.value() & ~Move::PromoteFlag));
    }

    void append_ignored_unpromote_checks(Position& pos, std::vector<Move>& moves) {
        const Color attacker = pos.turn();
        const size_t base_size = moves.size();
        for (size_t i = 0; i < base_size; ++i) {
            const Move promoted = moves[i];
            if (!has_ignored_unpromote_check(promoted, attacker)) {
                continue;
            }
            const Move unpromoted = unpromote_counterpart(pos, promoted);
            if (should_append_ignored_unpromote_check(pos, promoted, unpromoted, attacker)) {
                moves.push_back(unpromoted);
            }
        }
    }

    class OslPieceNumberState {
    public:
        struct PieceInfo {
            Color owner = ColorNum;
            PieceType ptype = Occupied;
            Square square = SquareNum;
            bool used = false;
        };

        struct Undo {
            Color previous_turn = Black;
            int moving_num = -1;
            PieceInfo moving_before{};
            int captured_num = -1;
            PieceInfo captured_before{};
        };

        static std::optional<OslPieceNumberState> from_position(const Position& position) {
            OslPieceNumberState state;
            if (!state.initialize(position)) {
                return std::nullopt;
            }
            return state;
        }

        static std::optional<OslPieceNumberState> from_history(
            const Position& root_position,
            const std::vector<Move>& history) {
            OslPieceNumberState state;
            if (!state.initialize(root_position)) {
                return std::nullopt;
            }
            for (const Move move : history) {
                if (!state.apply_move(move)) {
                    return std::nullopt;
                }
            }
            return state;
        }

        int number_of_move_piece(const Move move) const {
            if (move.isDrop()) {
                return -1;
            }
            if (!isInSquare(move.from())) {
                return -1;
            }
            return board_number_[move.from()];
        }

        int number_of_square(const Square square) const {
            if (!isInSquare(square)) {
                return -1;
            }
            return board_number_[square];
        }

        const PieceInfo* piece_of_number(const int num) const {
            if (num < 0 || num >= static_cast<int>(pieces_.size()) || !pieces_[num].used) {
                return nullptr;
            }
            return &pieces_[num];
        }

        bool apply_move(const Move move, Undo* undo = nullptr) {
            return apply(move, undo);
        }

        void undo_move(const Undo& undo) {
            pieces_turn_ = undo.previous_turn;
            if (undo.moving_num >= 0) {
                const Square current_square = pieces_[undo.moving_num].square;
                if (isInSquare(current_square)) {
                    board_number_[current_square] = -1;
                }
            }
            if (undo.captured_num >= 0) {
                pieces_[undo.captured_num] = undo.captured_before;
                if (isInSquare(undo.captured_before.square)) {
                    board_number_[undo.captured_before.square] = undo.captured_num;
                }
            }
            if (undo.moving_num >= 0) {
                pieces_[undo.moving_num] = undo.moving_before;
                if (isInSquare(undo.moving_before.square)) {
                    board_number_[undo.moving_before.square] = undo.moving_num;
                }
            }
        }

    private:
        std::array<PieceInfo, 40> pieces_{};
        std::array<int, SquareNum> board_number_{};

        static PieceType unpromote_ptype(const PieceType ptype) {
            switch (ptype) {
            case ProPawn: return Pawn;
            case ProLance: return Lance;
            case ProKnight: return Knight;
            case ProSilver: return Silver;
            case Horse: return Bishop;
            case Dragon: return Rook;
            default: return ptype;
            }
        }

        static std::pair<int, int> number_range(const PieceType ptype) {
            switch (unpromote_ptype(ptype)) {
            case Pawn: return { 0, 18 };
            case Knight: return { 18, 22 };
            case Silver: return { 22, 26 };
            case Gold: return { 26, 30 };
            case King: return { 30, 32 };
            case Lance: return { 32, 36 };
            case Bishop: return { 36, 38 };
            case Rook: return { 38, 40 };
            default: return { 0, 0 };
            }
        }

        bool set_piece(const Color owner, const Square square, const PieceType ptype) {
            const auto [begin, end] = number_range(ptype);
            for (int num = begin; num < end; ++num) {
                if (pieces_[num].used) {
                    continue;
                }
                if (ptype == King && num != 30 + static_cast<int>(owner)) {
                    continue;
                }
                pieces_[num] = { owner, ptype, square, true };
                if (isInSquare(square)) {
                    board_number_[square] = num;
                }
                return true;
            }
            return false;
        }

        bool initialize(const Position& pos) {
            board_number_.fill(-1);
            pieces_turn_ = pos.turn();
            for (int rank = static_cast<int>(Rank1); rank <= static_cast<int>(Rank9); ++rank) {
                for (int file = static_cast<int>(File9); file >= static_cast<int>(File1); --file) {
                    const Square square = makeSquare(static_cast<File>(file), static_cast<Rank>(rank));
                    const Piece piece = pos.piece(square);
                    if (piece == Empty) {
                        continue;
                    }
                    if (!set_piece(pieceToColor(piece), square, pieceToPieceType(piece))) {
                        return false;
                    }
                }
            }
            for (const Color owner : { Black, White }) {
                for (const HandPiece hp : { HRook, HBishop, HGold, HSilver, HKnight, HLance, HPawn }) {
                    const PieceType ptype = handPieceToPieceType(hp);
                    const int count = hand_count(pos.hand(owner), hp);
                    for (int i = 0; i < count; ++i) {
                        if (!set_piece(owner, SquareNum, ptype)) {
                            return false;
                        }
                    }
                }
            }
            return true;
        }

        int lowest_stand_piece(const Color owner, const PieceType ptype) const {
            const auto [begin, end] = number_range(ptype);
            for (int num = begin; num < end; ++num) {
                if (pieces_[num].used
                    && pieces_[num].owner == owner
                    && pieces_[num].square == SquareNum
                    && unpromote_ptype(pieces_[num].ptype) == unpromote_ptype(ptype)) {
                    return num;
                }
            }
            return -1;
        }

        bool apply(const Move move, Undo* undo) {
            if (!move.isAny()) {
                return false;
            }
            if (undo) {
                *undo = Undo{};
                undo->previous_turn = pieces_turn_;
            }
            if (move.isDrop()) {
                const Color owner = pieces_turn_;
                const int num = lowest_stand_piece(owner, move.pieceTypeDropped());
                if (num < 0) {
                    return false;
                }
                if (undo) {
                    undo->moving_num = num;
                    undo->moving_before = pieces_[num];
                }
                pieces_[num].ptype = move.pieceTypeDropped();
                pieces_[num].square = move.to();
                board_number_[move.to()] = num;
                pieces_turn_ = oppositeColor(pieces_turn_);
                return true;
            }

            if (!isInSquare(move.from()) || !isInSquare(move.to())) {
                return false;
            }
            const int num = board_number_[move.from()];
            if (num < 0) {
                return false;
            }
            if (undo) {
                undo->moving_num = num;
                undo->moving_before = pieces_[num];
            }
            const Color owner = pieces_[num].owner;
            if (move.isCapture()) {
                const int captured_num = board_number_[move.to()];
                if (captured_num < 0) {
                    return false;
                }
                if (undo) {
                    undo->captured_num = captured_num;
                    undo->captured_before = pieces_[captured_num];
                }
                pieces_[captured_num].owner = owner;
                pieces_[captured_num].ptype = unpromote_ptype(pieces_[captured_num].ptype);
                pieces_[captured_num].square = SquareNum;
            }
            board_number_[move.from()] = -1;
            pieces_[num].ptype = move.pieceTypeTo(pieces_[num].ptype);
            pieces_[num].square = move.to();
            board_number_[move.to()] = num;
            pieces_turn_ = oppositeColor(pieces_turn_);
            return true;
        }

        Color pieces_turn_ = Black;
    };

    int immediate_osl_piece_number_from_state_or_position(
        const Position& pos, const Square square, const OslPieceNumberState* current_piece_numbers) {
        if (current_piece_numbers) {
            const int number = current_piece_numbers->number_of_square(square);
            if (number >= 0) {
                return number;
            }
        }
        return immediate_osl_piece_number_from_position(pos, square);
    }

    bool osl_piece_number_info(
        const OslPieceNumberState* current_piece_numbers, const int number, Color* owner, Square* square) {
        if (!current_piece_numbers) {
            return false;
        }
        const OslPieceNumberState::PieceInfo* piece = current_piece_numbers->piece_of_number(number);
        if (!piece) {
            return false;
        }
        if (owner) {
            *owner = piece->owner;
        }
        if (square) {
            *square = piece->square;
        }
        return true;
    }

    bool is_osl_bishop_number_ordered_move(const Position& pos, const Move move) {
        if (move.isDrop()) {
            return false;
        }
        if (dfpn_check_move_phase(pos, pos.turn(), move) != static_cast<int>(DfpnCheckGenerationPhase::BishopLong)) {
            return false;
        }
        const PieceType ptype = move.pieceTypeFrom();
        return ptype == Bishop || ptype == Horse;
    }

    bool is_osl_rook_number_ordered_move(const Position& pos, const Move move) {
        if (move.isDrop()) {
            return false;
        }
        if (dfpn_check_move_phase(pos, pos.turn(), move) != static_cast<int>(DfpnCheckGenerationPhase::RookLong)) {
            return false;
        }
        const PieceType ptype = move.pieceTypeFrom();
        return ptype == Rook || ptype == Dragon;
    }

    template <class MoveContainer>
    bool reorder_osl_numbered_long_moves_impl(
        const Position& pos,
        MoveContainer& moves,
        const Position* root_position,
        const std::vector<Move>* move_history,
        const OslPieceNumberState* current_piece_numbers) {
        if (!move_history || move_history->empty()) {
            return false;
        }
        std::optional<OslPieceNumberState> reconstructed_state;
        const auto piece_numbers = [&]() -> const OslPieceNumberState* {
            if (current_piece_numbers) {
                return current_piece_numbers;
            }
            if (!reconstructed_state && root_position && move_history && !move_history->empty()) {
                reconstructed_state = OslPieceNumberState::from_history(*root_position, *move_history);
            }
            return reconstructed_state ? &*reconstructed_state : nullptr;
        };
        bool changed = false;
        const auto reorder_kind = [&](const auto predicate) {
            std::vector<size_t> indices;
            for (size_t i = 0; i < moves.size(); ++i) {
                if (predicate(pos, moves[i])
                    && !is_ignored_unpromote_check_variant(moves[i], pos.turn())) {
                    indices.push_back(i);
                }
            }
            if (indices.size() < 2) {
                return;
            }
            const OslPieceNumberState* state = piece_numbers();
            if (!state) {
                return;
            }
            std::vector<Move> ordered;
            ordered.reserve(indices.size());
            for (const size_t index : indices) {
                ordered.push_back(moves[index]);
            }
            std::stable_sort(ordered.begin(), ordered.end(), [&](const Move lhs, const Move rhs) {
                const int lhs_num = state->number_of_move_piece(lhs);
                const int rhs_num = state->number_of_move_piece(rhs);
                if (lhs_num < 0 || rhs_num < 0 || lhs_num == rhs_num) {
                    return false;
                }
                return lhs_num < rhs_num;
            });
            for (size_t i = 0; i < indices.size(); ++i) {
                if (moves[indices[i]] != ordered[i]) {
                    changed = true;
                }
                moves[indices[i]] = ordered[i];
            }
        };
        reorder_kind(is_osl_rook_number_ordered_move);
        reorder_kind(is_osl_bishop_number_ordered_move);
        return changed;
    }

    bool reorder_osl_numbered_long_moves(
        const Position& pos,
        std::vector<Move>& moves,
        const Position* root_position,
        const std::vector<Move>* move_history,
        const OslPieceNumberState* current_piece_numbers) {
        return reorder_osl_numbered_long_moves_impl(pos, moves, root_position, move_history, current_piece_numbers);
    }

    bool reorder_osl_numbered_check_moves(
        const Position& pos,
        std::vector<Move>& moves,
        const Position* root_position,
        const std::vector<Move>* move_history,
        const OslPieceNumberState* current_piece_numbers) {
        if (moves.size() < 2) {
            return false;
        }
        std::optional<OslPieceNumberState> reconstructed_state;
        const auto piece_numbers = [&]() -> const OslPieceNumberState* {
            if (current_piece_numbers) {
                return current_piece_numbers;
            }
            if (!reconstructed_state && root_position && move_history) {
                reconstructed_state = OslPieceNumberState::from_history(*root_position, *move_history);
            }
            return reconstructed_state ? &*reconstructed_state : nullptr;
        };
        const OslPieceNumberState* state = piece_numbers();
        if (!state) {
            return false;
        }

        struct RawCheckKey {
            int phase;
            int subphase;
            int primary;
            int drop;
            int secondary;
            int promotion;

            bool operator<(const RawCheckKey& rhs) const {
                if (phase != rhs.phase) return phase < rhs.phase;
                if (subphase != rhs.subphase) return subphase < rhs.subphase;
                if (primary != rhs.primary) return primary < rhs.primary;
                if (drop != rhs.drop) return drop < rhs.drop;
                if (secondary != rhs.secondary) return secondary < rhs.secondary;
                return promotion < rhs.promotion;
            }
        };

        const auto raw_key = [&](const Move move) -> RawCheckKey {
            const int phase = dfpn_check_move_phase(pos, pos.turn(), move);
            const int subphase = dfpn_check_move_subphase(pos, pos.turn(), move, phase);
            int from_key = 0;
            if (!move.isDrop()) {
                const int piece_number = state->number_of_move_piece(move);
                from_key = piece_number >= 0 ? piece_number : osl_piece_iteration_key(move);
            }
            const bool long_piece_phase =
                phase == static_cast<int>(DfpnCheckGenerationPhase::BishopLong)
                || phase == static_cast<int>(DfpnCheckGenerationPhase::RookLong);
            if (long_piece_phase) {
                if (const std::optional<Square> generator_square =
                    dfpn_check_long_generator_square(pos, pos.turn(), move, phase)) {
                    const int piece_number = state->number_of_square(*generator_square);
                    if (piece_number >= 0) {
                        from_key = piece_number;
                    }
                }
            }
            return RawCheckKey{
                phase,
                subphase,
                long_piece_phase ? from_key : osl_square_index(move.to()),
                move.isDrop() ? 1 : 0,
                long_piece_phase ? osl_square_index(move.to()) : from_key,
                move.isPromotion() ? 0 : 1
            };
        };

        if (moves.size() < 2) {
            return false;
        }
        struct RawKeyedMove {
            Move move;
            RawCheckKey key;
        };

        std::array<RawKeyedMove, kMoveBufferSize> ordered;
        std::array<Move, kMoveBufferSize> ignored_unpromotes;
        size_t ordered_size = 0;
        size_t ignored_unpromotes_size = 0;
        for (const Move move : moves) {
            if (is_ignored_unpromote_check_variant(move, pos.turn())) {
                ignored_unpromotes[ignored_unpromotes_size++] = move;
            }
            else {
                ordered[ordered_size++] = RawKeyedMove{ move, raw_key(move) };
            }
        }
        std::sort(ordered.begin(), ordered.begin() + static_cast<std::ptrdiff_t>(ordered_size),
            [](const RawKeyedMove& lhs, const RawKeyedMove& rhs) {
                return lhs.key < rhs.key;
            });

        bool changed = false;
        if (ordered_size + ignored_unpromotes_size != moves.size()) {
            return false;
        }
        size_t out = 0;
        for (size_t i = 0; i < ordered_size; ++i) {
            const RawKeyedMove& keyed = ordered[i];
            if (moves[out] != keyed.move) {
                changed = true;
            }
            moves[out++] = keyed.move;
        }
        for (size_t i = 0; i < ignored_unpromotes_size; ++i) {
            const Move move = ignored_unpromotes[i];
            if (moves[out] != move) {
                changed = true;
            }
            moves[out++] = move;
        }
        return changed;
    }

    template <class MoveContainer>
    void reorder_osl_numbered_escape_target_moves_impl(
        const OslPieceNumberState* current_piece_numbers,
        MoveContainer& moves) {
        if (!current_piece_numbers || moves.size() < 2) {
            return;
        }

        size_t begin = 0;
        while (begin < moves.size()) {
            const Move first = moves[begin];
            if (first.isDrop() || first.pieceTypeFrom() == King) {
                ++begin;
                continue;
            }

            const Square target = first.to();
            size_t end = begin + 1;
            while (end < moves.size()) {
                const Move move = moves[end];
                if (move.isDrop() || move.pieceTypeFrom() == King || move.to() != target) {
                    break;
                }
                ++end;
            }

            if (end - begin > 1) {
                std::stable_sort(
                    moves.begin() + static_cast<std::ptrdiff_t>(begin),
                    moves.begin() + static_cast<std::ptrdiff_t>(end),
                    [&](const Move lhs, const Move rhs) {
                        const int lhs_num = current_piece_numbers->number_of_move_piece(lhs);
                        const int rhs_num = current_piece_numbers->number_of_move_piece(rhs);
                        if (lhs_num < 0 || rhs_num < 0 || lhs_num == rhs_num) {
                            return false;
                        }
                        return lhs_num < rhs_num;
                    });
            }
            begin = end;
        }
    }

    void reorder_osl_numbered_escape_target_moves(
        const OslPieceNumberState* current_piece_numbers,
        std::vector<Move>& moves) {
        reorder_osl_numbered_escape_target_moves_impl(current_piece_numbers, moves);
    }

    bool unpromoted_piece_has_effect_to_king(const Position& pos, const Move move, const Color attacker) {
        const Square from = move.to();
        const Square king = pos.kingSquare(oppositeColor(attacker));
        const int file_delta = static_cast<int>(makeFile(king)) - static_cast<int>(makeFile(from));
        const int rank_delta = static_cast<int>(makeRank(king)) - static_cast<int>(makeRank(from));
        switch (move.pieceTypeFrom()) {
        case Pawn:
            return pawnAttack(attacker, from).isSet(king);
        case Lance:
            return file_delta == 0
                && ((attacker == Black && rank_delta < 0) || (attacker == White && rank_delta > 0));
        case Bishop:
            return std::abs(file_delta) == std::abs(rank_delta);
        case Rook:
            return file_delta == 0 || rank_delta == 0;
        default:
            return false;
        }
    }

    bool osl_pin_or_open_shadow(const Position& pos, const Color attacker, const Square blocker) {
        const Square king = pos.kingSquare(oppositeColor(attacker));
        if ((betweenBB(blocker, king) & pos.occupiedBB()).isAny()) {
            return false;
        }

        Bitboard occupied_without_blocker = pos.occupiedBB();
        occupied_without_blocker.xorBit(blocker);
        Bitboard pinners = pos.bbOf(attacker)
            & ((rookAttack(king, occupied_without_blocker) & pos.bbOf(Rook, Dragon))
                | (bishopAttack(king, occupied_without_blocker) & pos.bbOf(Bishop, Horse))
                | (lanceAttack(oppositeColor(attacker), king, occupied_without_blocker) & pos.bbOf(Lance)));

        while (pinners) {
            const Square pinner = pinners.firstOneFromSQ11();
            pinners.clearBit(pinner);
            if ((betweenBB(pinner, king) & pos.occupiedBB()) == setMaskBB(blocker)) {
                return true;
            }
        }
        return false;
    }

    bool is_legal_check_move(Position& pos, const Move move) {
        if (!pos.moveIsLegal(move)) {
            return false;
        }
        StateInfo st;
        pos.doMove(move, st);
        const bool gives_check = pos.inCheck();
        pos.undoMove(move);
        return gives_check;
    }

    bool has_pawn_drop_checkmate(Position& pos, const Color us) {
        if (!pos.hand(us).exists<HPawn>()) {
            return false;
        }
        const Color them = oppositeColor(us);
        const Square king = pos.kingSquare(them);
        constexpr SquareDelta black_pawn_check_delta = DeltaS;
        constexpr SquareDelta white_pawn_check_delta = DeltaN;
        const Rank last_rank = us == Black ? Rank9 : Rank1;
        if (!king || makeRank(king) == last_rank) {
            return false;
        }
        const Square to = king + (us == Black ? black_pawn_check_delta : white_pawn_check_delta);
        if (!isInSquare(to) || pos.piece(to) != Empty) {
            return false;
        }
        if (pos.bbOf(Pawn, us).andIsAny(squareFileMask(to))) {
            return false;
        }
        return pos.isPawnDropCheckMate(us, to);
    }

    void generate_check_moves(Position& pos, std::vector<Move>& moves, bool* has_pawn_checkmate,
        const Position* root_position, const std::vector<Move>* move_history,
        const OslPieceNumberState* current_piece_numbers) {
        if (has_pawn_checkmate) {
            *has_pawn_checkmate = false;
        }
        moves.clear();
        const bool in_check = pos.inCheck();
        if (in_check) {
            std::array<ExtMove, kMoveBufferSize> buffer;
            ExtMove* last = generateOslmateEscapeMoves(buffer.data(), pos, false, false);
            moves.reserve(static_cast<size_t>(last - buffer.data()));
            for (ExtMove* it = buffer.data(); it != last; ++it) {
                // OSL Dfpn::generateCheck uses GenerateEscape::generateKingEscape,
                // which includes king moves, captures of the checker, and blocks.
                if (pos.checkMoveIsEvasion(it->move)
                    && is_legal_check_move(pos, it->move)) {
                    moves.push_back(it->move);
                }
            }
            append_ignored_unpromote_checks(pos, moves);
        }
        else {
            if (has_pawn_checkmate) {
                *has_pawn_checkmate = has_pawn_drop_checkmate(pos, pos.turn());
            }
            std::array<ExtMove, kMoveBufferSize> candidate_buffer;
            ExtMove* candidate_last = generateMoves<CheckAllOslmate>(candidate_buffer.data(), pos);
            moves.reserve(static_cast<size_t>(candidate_last - candidate_buffer.data()));
            for (ExtMove* it = candidate_buffer.data(); it != candidate_last; ++it) {
                moves.push_back(it->move);
            }
            append_ignored_unpromote_checks(pos, moves);

        }

        reorder_osl_numbered_check_moves(pos, moves, root_position, move_history, current_piece_numbers);

        // OSL Dfpn::generateCheck applies Dfpn::sort directly to the
        // AddEffectWithEffect raw order.  cshogi's generic CheckAll generator
        // has a different raw order, so normalize to the OSL phase / target /
        // piece-number order before applying the same segment sort.
        sort_moves(pos, pos.turn(), moves);
    }

    std::vector<Move> generate_check_moves(Position& pos, bool* has_pawn_checkmate) {
        std::vector<Move> moves;
        generate_check_moves(pos, moves, has_pawn_checkmate);
        return moves;
    }

    template <class MoveContainer>
    void generate_fixed_depth_check_moves_into(Position& pos, MoveContainer& moves, bool* has_pawn_checkmate,
        const OslPieceNumberState* current_piece_numbers) {
        moves.clear();
        if (has_pawn_checkmate) {
            *has_pawn_checkmate = has_pawn_drop_checkmate(pos, pos.turn());
        }

        // OSL FixedDepthSearcher::attack uses raw AddEffectWithEffect<Store>::generate<P,true>.
        // It does not call Dfpn::generateCheck, so do not append ignored unpromotes or Dfpn-sort here.
        // The raw generator is responsible for matching OSL's source
        // enumeration order; OSL long-range moves are still piece-number
        // ordered inside that raw AddEffectWithEffect traversal.
        std::array<ExtMove, kMoveBufferSize> buffer;
        ExtMove* last = generateMoves<CheckAllOslmateFixedRaw>(buffer.data(), pos);
        for (ExtMove* it = buffer.data(); it != last; ++it) {
            moves.push_back(it->move);
        }
        // OSL FixedDepthSearcher::attack consumes AddEffectWithEffect's raw
        // order, whose long-piece effects are enumerated by OSL piece number.
        reorder_osl_numbered_long_moves_impl(pos, moves, nullptr, nullptr, current_piece_numbers);
    }

    std::vector<Move> generate_fixed_depth_check_moves(Position& pos, bool* has_pawn_checkmate,
        const OslPieceNumberState* current_piece_numbers) {
        std::vector<Move> moves;
        moves.reserve(kMoveBufferSize);
        generate_fixed_depth_check_moves_into(pos, moves, has_pawn_checkmate, current_piece_numbers);
        return moves;
    }

    void generate_escape_moves(Position& pos, std::vector<Move>& moves, const bool need_full_width, const Square last_to,
        const OslPieceNumberState* current_piece_numbers) {
        const bool delay_node = is_delay_escape_node(pos, last_to);
        if (delay_node) {
            std::array<ExtMove, kMoveBufferSize> buffer;
            ExtMove* last = generateOslmateCheapKingEscapeMoves(buffer.data(), pos, false);
            moves.clear();
            moves.reserve(static_cast<size_t>(last - buffer.data()));
            for (ExtMove* it = buffer.data(); it != last; ++it) {
                if (it->move.to() == last_to) {
                    moves.push_back(it->move);
                }
            }
        }
        else {
            generate_cheap_escape_moves(pos, moves);
        }
        // OSL generateEscape feeds Dfpn::sort with moves generated in
        // PieceMask::takeOneBit() order.  Recreate that piece-number order
        // before sorting; this is not an additional OSL sort phase.
        reorder_osl_numbered_escape_target_moves(current_piece_numbers, moves);
        sort_moves(pos, pos.turn(), moves);
        if (need_full_width) {
            std::array<ExtMove, kMoveBufferSize> buffer;
            ExtMove* last = generateOslmateEscapeMoves(buffer.data(), pos, false, false);
            FixedMoveVector<kCheckOrEscapeMoveCapacity> others;
            for (ExtMove* it = buffer.data(); it != last; ++it) {
                others.push_back(it->move);
            }
            reorder_osl_numbered_escape_target_moves_impl(current_piece_numbers, others);
            sort_moves(pos, pos.turn(), others);

            const size_t original_size = moves.size();
            std::array<uint32_t, kCheckOrEscapeMoveCapacity> original_move_values{};
            assert(original_size <= original_move_values.size());
            for (size_t i = 0; i < original_size; ++i) {
                original_move_values[i] = moves[i].value();
            }
            for (const Move move : others) {
                bool found = false;
                const uint32_t move_value = move.value();
                for (size_t i = 0; i < original_size; ++i) {
                    if (original_move_values[i] == move_value) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    moves.push_back(move);
                }
            }

            const size_t full_width_size = moves.size();
            for (size_t i = 0; i < full_width_size; ++i) {
                const Move move = moves[i];
                if (!has_ignored_unpromote_escape(move, pos.turn())) {
                    continue;
                }

                const Move unpromote = unpromote_counterpart(pos, move);
                moves.push_back(unpromote);
            }
        }

    }

    std::vector<Move> generate_escape_moves(Position& pos, const bool need_full_width, const Square last_to,
        const OslPieceNumberState* current_piece_numbers) {
        std::vector<Move> moves;
        generate_escape_moves(pos, moves, need_full_width, last_to, current_piece_numbers);
        return moves;
    }

    bool oracle_traceable(const OracleState& oracle, const Color mover, const Move move) {
        if (!move.isDrop()) {
            return true;
        }
        return hand_count(oracle.stands[mover], move.handPieceDropped()) > 0;
    }

    bool oracle_adjust_move_is_almost_valid(const Position& pos, const Move move) {
        if (!move) {
            return false;
        }
        const Color us = pos.turn();
        if (move.isDrop()) {
            const PieceType pt = move.pieceTypeDropped();
            if (pos.piece(move.to()) != Empty) {
                return false;
            }
            if (!pos.hand(us).exists(pieceTypeToHandPiece(pt))) {
                return false;
            }
            if (pt == Pawn && (pos.bbOf(Pawn, us) & fileMask(makeFile(move.to())))) {
                return false;
            }
            return true;
        }

        const Square from = move.from();
        const Square to = move.to();
        const Piece from_piece = pos.piece(from);
        if (from_piece == Empty || pieceToColor(from_piece) != us) {
            return false;
        }
        const PieceType actual_from_type = pieceToPieceType(from_piece);
        if (move.isPromotion()) {
            if (actual_from_type != move.pieceTypeFrom() || (actual_from_type & PTPromote)) {
                return false;
            }
        }
        else if (actual_from_type != move.pieceTypeTo()) {
            return false;
        }
        const Piece to_piece = pos.piece(to);
        if (to_piece != Empty && pieceToColor(to_piece) == us) {
            return false;
        }
        if (pieceToPieceType(to_piece) != move.cap()) {
            return false;
        }
        Bitboard occupied = pos.occupiedBB();
        if (!Position::attacksFrom(actual_from_type, us, from, occupied).isSet(to)) {
            return false;
        }
        return true;
    }

    Move rebuild_oracle_move(const Position& pos, const Square from, const Square to, const PieceType from_type, const bool promotion) {
        Move move = makeCaptureMove(from_type, from, to, pos);
        if (promotion) {
            move |= promoteFlag();
        }
        return move;
    }

    bool same_long_move_ray(const Square to, const Square oracle_from, const Square candidate_from) {
        const int oracle_file = static_cast<int>(makeFile(oracle_from)) - static_cast<int>(makeFile(to));
        const int oracle_rank = static_cast<int>(makeRank(oracle_from)) - static_cast<int>(makeRank(to));
        const int candidate_file = static_cast<int>(makeFile(candidate_from)) - static_cast<int>(makeFile(to));
        const int candidate_rank = static_cast<int>(makeRank(candidate_from)) - static_cast<int>(makeRank(to));

        const int oracle_file_step = oracle_file == 0 ? 0 : (oracle_file > 0 ? 1 : -1);
        const int oracle_rank_step = oracle_rank == 0 ? 0 : (oracle_rank > 0 ? 1 : -1);
        const int candidate_file_step = candidate_file == 0 ? 0 : (candidate_file > 0 ? 1 : -1);
        const int candidate_rank_step = candidate_rank == 0 ? 0 : (candidate_rank > 0 ? 1 : -1);
        return oracle_file_step == candidate_file_step && oracle_rank_step == candidate_rank_step;
    }

    bool is_oracle_long_move_piece(const PieceType piece_type) {
        switch (piece_type) {
        case Lance:
        case Bishop:
        case Rook:
        case Horse:
        case Dragon:
            return true;
        default:
            return false;
        }
    }

    int osl_piece_number_for_adjust(const Position& pos, const Square square, const OslPieceNumberState* current_piece_numbers) {
        if (current_piece_numbers) {
            const int number = current_piece_numbers->number_of_square(square);
            if (number >= 0) {
                return number;
            }
        }
        return immediate_osl_piece_number_from_position(pos, square);
    }

    Move adjust_oracle_attack_move(Position& pos, const Move oracle_move,
        const OslPieceNumberState* current_piece_numbers = nullptr) {
        if (!oracle_move) {
            return Move::moveNone();
        }
        Move adjusted = oracle_move;
        if (!oracle_move.isDrop()) {
            if (pieceToPieceType(pos.piece(oracle_move.to())) == King) {
                return Move::moveNone();
            }

            adjusted = rebuild_oracle_move(pos, oracle_move.from(), oracle_move.to(), oracle_move.pieceTypeFrom(), oracle_move.isPromotion());
            const Piece from_piece = pos.piece(oracle_move.from());
            if (pieceToPieceType(from_piece) != oracle_move.pieceTypeFrom() && is_oracle_long_move_piece(oracle_move.pieceTypeTo())) {
                const Color mover = pos.turn();
                Bitboard candidates = allZeroBB();
                switch (unpromote_piece_type(oracle_move.pieceTypeTo())) {
                case Rook:
                    candidates = pos.bbOf(Rook, Dragon) & pos.bbOf(mover);
                    break;
                case Bishop:
                    candidates = pos.bbOf(Bishop, Horse) & pos.bbOf(mover);
                    break;
                case Lance:
                    candidates = pos.bbOf(Lance) & pos.bbOf(mover);
                    break;
                default:
                    return Move::moveNone();
                }

                const Bitboard occupied = pos.occupiedBB();
                Square selected_from = SquareNum;
                while (candidates.isAny()) {
                    const Square from = candidates.firstOneFromSQ11();
                    const PieceType candidate_type = pieceToPieceType(pos.piece(from));
                    if (!Position::attacksFrom(candidate_type, mover, from, occupied).isSet(oracle_move.to())) {
                        continue;
                    }
                    selected_from = from;
                    if (same_long_move_ray(oracle_move.to(), oracle_move.from(), from)) {
                        break;
                    }
                }

                if (isInSquare(selected_from)) {
                    const PieceType candidate_type = pieceToPieceType(pos.piece(selected_from));
                    if (candidate_type == oracle_move.pieceTypeFrom()) {
                        adjusted = rebuild_oracle_move(pos, selected_from, oracle_move.to(), candidate_type, oracle_move.isPromotion());
                    }
                    else if (candidate_type == oracle_move.pieceTypeTo()) {
                        adjusted = rebuild_oracle_move(pos, selected_from, oracle_move.to(), candidate_type, false);
                    }
                }
            }
        }

        if (!oracle_adjust_move_is_almost_valid(pos, adjusted)) {
            return Move::moveNone();
        }

        return adjusted;
    }

    struct AttackRepetition {
        bool handled = false;
        ProofDisproof pdp = ProofDisproof::Unknown();
    };

    AttackRepetition attack_repetition(const RepetitionType repetition) {
        (void)repetition;
        return {};
    }

    AttackRepetition defense_repetition(const RepetitionType repetition) {
        (void)repetition;
        return {};
    }

    bool fixed_defense(Position& pos, const int remaining_even);
    bool fixed_defense_exact(Position& pos, const Color attack_color, const int remaining_even, Hand* proof_pieces = nullptr);

    bool fixed_attack_pv(Position& pos, const int remaining_odd, std::vector<u32>& pv);
    bool fixed_defense_pv(Position& pos, const int remaining_even, std::vector<u32>& pv);

    bool fixed_attack_exact(Position& pos, const Color attack_color, const int remaining_odd,
        Move* best_move = nullptr, Hand* proof_pieces = nullptr) {
        if (remaining_odd <= 0 || (remaining_odd % 2) == 0) {
            return false;
        }
        if (best_move) {
            *best_move = Move::moveNone();
        }
        if (proof_pieces) {
            *proof_pieces = zero_hand();
        }

        if (!pos.inCheck()) {
            const Move mate1 = validated_mate_move_in_1(pos);
            if (mate1) {
                if (best_move) {
                    *best_move = mate1;
                }
                if (proof_pieces) {
                    *proof_pieces = proof_pieces_after_attack(
                        zero_hand(), complete_move_for_position(pos, mate1), pos.hand(attack_color));
                }
                return true;
            }
        }

        const Hand attack_stand = pos.hand(attack_color);
        const std::vector<Move> moves = generate_check_moves(pos);
        for (const Move move : moves) {
            StateInfo st;
            do_known_check_move(pos, move, st);

            Hand child_proof = zero_hand();
            const AttackRepetition rep = attack_repetition(NotRepetition);
            bool success = false;
            if (rep.handled) {
                success = rep.pdp.isCheckmateSuccess();
                if (success) {
                    child_proof = proof_pieces_leaf(pos, attack_color, pos.hand(attack_color));
                }
            }
            else if (remaining_odd == 1) {
                if (generate_escape_moves(pos).empty()) {
                    success = true;
                    child_proof = proof_pieces_leaf(pos, attack_color, pos.hand(attack_color));
                }
            }
            else {
                success = fixed_defense_exact(pos, attack_color, remaining_odd - 1, &child_proof);
            }

            pos.undoMove(move);
            if (!success) {
                continue;
            }

            if (best_move) {
                *best_move = move;
            }
            if (proof_pieces) {
                *proof_pieces = proof_pieces_after_attack(
                    child_proof, complete_move_for_position(pos, move), attack_stand);
            }
            return true;
        }
        return false;
    }

    bool fixed_defense_exact(Position& pos, const Color attack_color, const int remaining_even, Hand* proof_pieces) {
        if (remaining_even <= 0 || (remaining_even % 2) != 0) {
            return false;
        }
        if (proof_pieces) {
            *proof_pieces = zero_hand();
        }

        const std::vector<Move> moves = generate_escape_moves(pos);
        if (moves.empty()) {
            if (proof_pieces) {
                *proof_pieces = proof_pieces_leaf(pos, attack_color, pos.hand(attack_color));
            }
            return true;
        }

        Hand aggregate = zero_hand();
        for (const Move move : moves) {
            StateInfo st;
            do_known_check_move(pos, move, st);
            const AttackRepetition rep = defense_repetition(NotRepetition);
            if (rep.handled) {
                pos.undoMove(move);
                if (rep.pdp.isCheckmateFail()) {
                    return false;
                }
                continue;
            }

            Hand child_proof = zero_hand();
            Move child_best = Move::moveNone();
            const bool success = fixed_attack_exact(pos, attack_color, remaining_even - 1, &child_best, &child_proof);
            pos.undoMove(move);
            if (!success) {
                return false;
            }
            aggregate = hand_max(aggregate, child_proof);
        }

            if (proof_pieces) {
                *proof_pieces = aggregate;
                if (!is_unblockable_check(pos)) {
                    add_monopolized_pieces(pos.hand(attack_color), pos.hand(oppositeColor(attack_color)), pos.hand(attack_color), *proof_pieces);
                }
            }
        return true;
    }

    bool should_try_fixed_attack(const Position& pos, const DfpnRecord& record, const Color attack_color, const bool is_search_root) {
        if (!kEnableFixedDepthShortcut) {
            return false;
        }
        if (is_search_root) {
            return true;
        }
        if (record.proof_disproof != ProofDisproof(1, 1) || !pos.hand(attack_color).exists<HGold>()) {
            return false;
        }
        const Square king = pos.kingSquare(oppositeColor(attack_color));
        if (!king) {
            return false;
        }
        const int file = static_cast<int>(makeFile(king)) + 1;
        const int rank = static_cast<int>(makeRank(king)) + 1;
        const int rank_for_black = attack_color == Black ? rank : 10 - rank;
        return file <= 3 || file >= 7 || rank_for_black <= 3;
    }

    bool fixed_attack(Position& pos, const int remaining_odd, Move* best_move = nullptr) {
        if (remaining_odd <= 0 || (remaining_odd % 2) == 0) {
            return false;
        }

        const std::vector<Move> moves = generate_check_moves(pos);
        for (const Move move : moves) {
            StateInfo st;
            do_known_check_move(pos, move, st);
            const AttackRepetition rep = attack_repetition(NotRepetition);
            if (rep.handled) {
                pos.undoMove(move);
                if (rep.pdp.isCheckmateSuccess()) {
                    if (best_move) {
                        *best_move = move;
                    }
                    return true;
                }
                continue;
            }

            const bool success = (remaining_odd == 1)
                ? generate_escape_moves(pos).empty()
                : fixed_defense(pos, remaining_odd - 1);
            pos.undoMove(move);
            if (success) {
                if (best_move) {
                    *best_move = move;
                }
                return true;
            }
        }
        return false;
    }

    bool fixed_defense(Position& pos, const int remaining_even) {
        if (remaining_even <= 0 || (remaining_even % 2) != 0) {
            return false;
        }

        const std::vector<Move> moves = generate_escape_moves(pos);
        if (moves.empty()) {
            return true;
        }

        for (const Move move : moves) {
            StateInfo st;
            do_known_check_move(pos, move, st);
            const AttackRepetition rep = defense_repetition(NotRepetition);
            if (rep.handled) {
                pos.undoMove(move);
                if (rep.pdp.isCheckmateFail()) {
                    return false;
                }
                continue;
            }

            const bool success = fixed_attack(pos, remaining_even - 1);
            pos.undoMove(move);
            if (!success) {
                return false;
            }
        }
        return true;
    }

    bool fixed_defense_osl_shortcut(Position& pos, const Color attack_color, Hand* proof_pieces = nullptr) {
        return fixed_defense_exact(pos, attack_color, 2, proof_pieces);
    }

    ProofDisproof fixed_attack_may_unsafe_zero(Position& pos, const Color attack_color, Move* best_move = nullptr,
        Hand* proof_pieces = nullptr, bool* searched_attack_node = nullptr,
        OslPieceNumberState* current_piece_numbers = nullptr) {
        if (best_move) {
            *best_move = Move::moveNone();
        }
        if (proof_pieces) {
            *proof_pieces = zero_hand();
        }
        if (searched_attack_node) {
            *searched_attack_node = false;
        }

        const Color defense_color = oppositeColor(attack_color);
        const Square target_king = pos.kingSquare(defense_color);
        const bool fixed_probe_enabled = false;
        if (target_king && effect_has_at(pos, attack_color, target_king)) {
            return ProofDisproof::NoEscape();
        }

        if (searched_attack_node) {
            *searched_attack_node = true;
        }
        if (!pos.inCheck()) {
            const Move mate1 = immediate_mate_move_in_1_osl(pos, current_piece_numbers);
            if (mate1) {
                if (best_move) {
                    *best_move = mate1;
                }
                if (proof_pieces) {
                    *proof_pieces = fixed_attack_leaf_proof_pieces(mate1);
                }
                return ProofDisproof::Checkmate();
            }
        }

        const ProofDisproof estimate = fixed_attack_estimation_zero(pos, attack_color);
        return estimate;
    }

    ProofDisproof fixed_defense_estimation_zero(Position& pos, const Color attack_color,
        const Move last_attack_move, Hand* proof_pieces = nullptr) {
        if (proof_pieces) {
            *proof_pieces = zero_hand();
        }

        const Color defense_color = oppositeColor(attack_color);
        const Square target_king = pos.kingSquare(defense_color);
        const King8RuntimeInfo king8_info = king8_runtime_info_at(pos, attack_color);
        int count = king8_info.liberty_count;

        Bitboard checkers = pos.checkersBB();
        const int checker_count = checkers.popCount();
        if (checker_count != 1) {
            if (count > 0) {
                return ProofDisproof(static_cast<uint32_t>(count), 1);
            }
            return ProofDisproof::NoEscape();
        }

        const Square attack_from = checkers.firstOneFromSQ11();
        count += effect_count(pos, defense_color, attack_from);
        if (std::abs(static_cast<int>(makeFile(attack_from)) - static_cast<int>(makeFile(target_king))) <= 1
            && std::abs(static_cast<int>(makeRank(attack_from)) - static_cast<int>(makeRank(target_king))) <= 1) {
            --count;
        }

        const PieceType attacker_type = pieceToPieceType(pos.piece(attack_from));
        if (!has_unblockable_effect_to_king(attacker_type, attack_color, attack_from, target_king)) {
            ++count;
        }

        if (count == 0) {
            if (last_attack_move && last_attack_move.isDrop() && last_attack_move.pieceTypeDropped() == Pawn) {
                return ProofDisproof::PawnCheckmate();
            }
            if (proof_pieces) {
                *proof_pieces = proof_pieces_leaf(pos, attack_color, pos.hand(attack_color));
            }
            return ProofDisproof::NoEscape();
        }
        return ProofDisproof(static_cast<uint32_t>(count), 1);
    }

    ProofDisproof fixed_attack_may_unsafe_depth1(Position& pos, const Color attack_color,
        Move* best_move = nullptr, Hand* proof_pieces = nullptr, bool* searched_attack_node = nullptr,
        OslPieceNumberState* current_piece_numbers = nullptr) {
        if (best_move) {
            *best_move = Move::moveNone();
        }
        if (proof_pieces) {
            *proof_pieces = zero_hand();
        }
        if (searched_attack_node) {
            *searched_attack_node = false;
        }

        const Color defense_color = oppositeColor(attack_color);
        const Square target_king = pos.kingSquare(defense_color);
        if (target_king && effect_has_at(pos, attack_color, target_king)) {
            return ProofDisproof::NoEscape();
        }

        if (searched_attack_node) {
            *searched_attack_node = true;
        }
        if (!pos.inCheck()) {
            const Move mate1 = immediate_mate_move_in_1_osl(pos, current_piece_numbers);
            if (mate1) {
                if (best_move) {
                    *best_move = mate1;
                }
                if (proof_pieces) {
                    *proof_pieces = fixed_attack_leaf_proof_pieces(mate1);
                }
                return ProofDisproof::Checkmate();
            }
        }

        bool has_pawn_checkmate = false;
        FixedMoveVector<kCheckOrEscapeMoveCapacity> moves;
        {
            generate_fixed_depth_check_moves_into(pos, moves, &has_pawn_checkmate, current_piece_numbers);
        }
        if (moves.empty()) {
            return has_pawn_checkmate
                ? ProofDisproof::PawnCheckmate()
                : ProofDisproof::NoCheckmate();
        }

        uint32_t min_proof = ProofDisproof::PROOF_MAX;
        uint32_t sum_disproof = 0;
        if (has_pawn_checkmate) {
            min_proof = std::min(min_proof, ProofDisproof::PAWN_CHECK_MATE_PROOF);
        }

        const Hand attack_stand = pos.hand(attack_color);
        for (const Move move : moves) {
            OslPieceNumberState::Undo piece_undo;
            OslPieceNumberState* child_piece_numbers_ptr = current_piece_numbers;
            bool piece_number_applied = false;
            if (current_piece_numbers) {
                piece_number_applied = current_piece_numbers->apply_move(move, &piece_undo);
                if (!piece_number_applied) {
                    child_piece_numbers_ptr = nullptr;
                }
            }
            StateInfo st;
            do_known_check_move(pos, move, st);

            Hand child_proof_storage = zero_hand();
            Hand* child_proof = proof_pieces ? proof_pieces : &child_proof_storage;
            const ProofDisproof child = [&]() {
                return fixed_defense_estimation_zero(pos, attack_color, move, child_proof);
            }();

            pos.undoMove(move);
            if (piece_number_applied) {
                current_piece_numbers->undo_move(piece_undo);
            }

            if (child.proof < min_proof) {
                if (child.proof == 0) {
                    if (best_move) {
                        *best_move = move;
                    }
                    if (proof_pieces) {
                        *proof_pieces = proof_pieces_after_attack(
                            *child_proof, complete_move_for_position(pos, move), attack_stand);
                    }
                    return ProofDisproof::Checkmate();
                }
                min_proof = child.proof;
            }
            sum_disproof = saturate_sum(static_cast<uint64_t>(sum_disproof) + child.disproof, ProofDisproof::DISPROOF_MAX);
        }

        return ProofDisproof(min_proof, sum_disproof);
    }

    ProofDisproof fixed_attack_depth2_osl(Position& pos, const Color attack_color, Move* best_move = nullptr,
        Hand* proof_pieces = nullptr, OslPieceNumberState* current_piece_numbers = nullptr) {
        if (best_move) {
            *best_move = Move::moveNone();
        }
        if (proof_pieces) {
            *proof_pieces = zero_hand();
        }

        if (!pos.inCheck()) {
            const Move mate1 = immediate_mate_move_in_1_osl(pos, current_piece_numbers);
            if (mate1) {
                if (best_move) {
                    *best_move = mate1;
                }
                if (proof_pieces) {
                    *proof_pieces = fixed_attack_leaf_proof_pieces(mate1);
                }
                return ProofDisproof::Checkmate();
            }
        }

        bool has_pawn_checkmate = false;
        FixedMoveVector<kCheckOrEscapeMoveCapacity> moves;
        {
            generate_fixed_depth_check_moves_into(pos, moves, &has_pawn_checkmate, current_piece_numbers);
        }
        if (moves.empty()) {
            return has_pawn_checkmate
                ? ProofDisproof::PawnCheckmate()
                : ProofDisproof::NoCheckmate();
        }

        uint32_t min_proof = ProofDisproof::PROOF_MAX;
        uint32_t sum_disproof = 0;
        if (has_pawn_checkmate) {
            min_proof = std::min(min_proof, ProofDisproof::PAWN_CHECK_MATE_PROOF);
        }

        const Hand attack_stand = pos.hand(attack_color);
        for (const Move move : moves) {
            OslPieceNumberState::Undo piece_undo;
            OslPieceNumberState* child_piece_numbers_ptr = current_piece_numbers;
            bool piece_number_applied = false;
            if (current_piece_numbers) {
                piece_number_applied = current_piece_numbers->apply_move(move, &piece_undo);
                if (!piece_number_applied) {
                    child_piece_numbers_ptr = nullptr;
                }
            }
            StateInfo st;
            pos.doMove(move, st);

            Move child_best = Move::moveNone();
            Hand child_proof_storage = zero_hand();
            Hand* child_proof = proof_pieces ? proof_pieces : &child_proof_storage;
            // OSL FixedDepthSolverExt::hasCheckmateMove(2) uses the normal
            // FixedDepthSolverExt::SetProofPieces path, not attackMayUnsafe.
            // The may-unsafe variant is used by CHECKMATE_D2 hasEscapeByMove().
            const ProofDisproof child = fixed_has_escape_by_move_zero(
                pos, attack_color, &child_best, child_proof, move, false, false, child_piece_numbers_ptr);

            pos.undoMove(move);
            if (piece_number_applied) {
                current_piece_numbers->undo_move(piece_undo);
            }
            if (child.proof < min_proof) {
                if (child.proof == 0) {
                    if (best_move) {
                        *best_move = move;
                    }
                    if (proof_pieces) {
                        *proof_pieces = proof_pieces_after_attack(
                            *child_proof, complete_move_for_position(pos, move), attack_stand);
                    }
                    return ProofDisproof::Checkmate();
                }
                min_proof = child.proof;
            }
            sum_disproof = saturate_sum(static_cast<uint64_t>(sum_disproof) + child.disproof, ProofDisproof::DISPROOF_MAX);
        }

        return ProofDisproof(min_proof, sum_disproof);
    }

    ProofDisproof fixed_attack_depth2_pv_osl(Position& pos, const Color attack_color, Move* best_move = nullptr,
        OslPieceNumberState* current_piece_numbers = nullptr) {
        if (best_move) {
            *best_move = Move::moveNone();
        }
        if (!pos.inCheck()) {
            const Move mate1 = immediate_mate_move_in_1_osl(pos, current_piece_numbers);
            if (mate1) {
                if (best_move) {
                    *best_move = mate1;
                }
                return ProofDisproof::Checkmate();
            }
        }

        bool has_pawn_checkmate = false;
        FixedMoveVector<kCheckOrEscapeMoveCapacity> moves;
        {
            generate_fixed_depth_check_moves_into(pos, moves, &has_pawn_checkmate, current_piece_numbers);
        }
        if (moves.empty()) {
            return has_pawn_checkmate
                ? ProofDisproof::PawnCheckmate()
                : ProofDisproof::NoCheckmate();
        }

        uint32_t min_proof = ProofDisproof::PROOF_MAX;
        uint32_t sum_disproof = 0;
        if (has_pawn_checkmate) {
            min_proof = std::min(min_proof, ProofDisproof::PAWN_CHECK_MATE_PROOF);
        }

        for (const Move move : moves) {
            OslPieceNumberState::Undo piece_undo;
            OslPieceNumberState* child_piece_numbers_ptr = current_piece_numbers;
            bool piece_number_applied = false;
            if (current_piece_numbers) {
                piece_number_applied = current_piece_numbers->apply_move(move, &piece_undo);
                if (!piece_number_applied) {
                    child_piece_numbers_ptr = nullptr;
                }
            }
            StateInfo st;
            pos.doMove(move, st);

            Move child_best = Move::moveNone();
            const ProofDisproof child = fixed_has_escape_by_move_zero(
                pos, attack_color, &child_best, nullptr, move, true, false, child_piece_numbers_ptr);

            pos.undoMove(move);
            if (piece_number_applied) {
                current_piece_numbers->undo_move(piece_undo);
            }
            if (child.proof < min_proof) {
                if (child.proof == 0) {
                    if (best_move) {
                        *best_move = move;
                    }
                    return ProofDisproof::Checkmate();
                }
                min_proof = child.proof;
            }
            sum_disproof = saturate_sum(static_cast<uint64_t>(sum_disproof) + child.disproof, ProofDisproof::DISPROOF_MAX);
        }

        return ProofDisproof(min_proof, sum_disproof);
    }

    ProofDisproof fixed_attack_osl_shortcut(Position& pos, const Color attack_color, Move* best_move,
        Hand* proof_pieces, OslPieceNumberState* current_piece_numbers) {
        if (best_move) {
            *best_move = Move::moveNone();
        }
        if (proof_pieces) {
            *proof_pieces = zero_hand();
        }

        return fixed_attack_depth2_osl(pos, attack_color, best_move, proof_pieces, current_piece_numbers);
    }

    bool fixed_attack_pv(Position& pos, const int remaining_odd, std::vector<u32>& pv) {
        if (remaining_odd <= 0 || (remaining_odd % 2) == 0) {
            return false;
        }

        const std::vector<Move> moves = generate_check_moves(pos);
        for (const Move move : moves) {
            StateInfo st;
            pos.doMove(move, st);
            const AttackRepetition rep = attack_repetition(NotRepetition);
            if (rep.handled) {
                pos.undoMove(move);
                if (rep.pdp.isCheckmateSuccess()) {
                    pv.push_back(move.value());
                    return true;
                }
                continue;
            }

            bool success = false;
            std::vector<u32> suffix;
            if (remaining_odd == 1) {
                success = generate_escape_moves(pos).empty();
            }
            else {
                success = fixed_defense_pv(pos, remaining_odd - 1, suffix);
            }
            pos.undoMove(move);
            if (success) {
                pv.push_back(move.value());
                pv.insert(pv.end(), suffix.begin(), suffix.end());
                return true;
            }
        }
        return false;
    }

    bool fixed_defense_pv(Position& pos, const int remaining_even, std::vector<u32>& pv) {
        if (remaining_even <= 0 || (remaining_even % 2) != 0) {
            return false;
        }

        const std::vector<Move> moves = generate_escape_moves(pos);
        if (moves.empty()) {
            return true;
        }

        for (const Move move : moves) {
            StateInfo st;
            pos.doMove(move, st);
            const AttackRepetition rep = defense_repetition(NotRepetition);
            if (rep.handled) {
                pos.undoMove(move);
                if (rep.pdp.isCheckmateFail()) {
                    return false;
                }
                continue;
            }

            std::vector<u32> suffix;
            const bool success = fixed_attack_pv(pos, remaining_even - 1, suffix);
            pos.undoMove(move);
            if (!success) {
                return false;
            }
            pv.push_back(move.value());
            pv.insert(pv.end(), suffix.begin(), suffix.end());
            return true;
        }
        return false;
    }

}

struct DfPn::Impl {
    DfpnTable table;
    DfpnPathTable path_table;
    DfpnPathTable oracle_path_table;
    bool stop = false;
    uint32_t max_search_node = std::numeric_limits<uint32_t>::max();
    int max_depth = 1600;
    int draw_ply = INT_MAX;
    uint64_t reserved_hash_mb = 64;
    Color root_attack_color = Black;

    struct PvDepthEntry {
        int depth = 0;
        Move best_move = Move::moveNone();
        Hand black_stand = Hand(0);
    };

    class PvDepthTable {
    public:
        PvDepthTable() = default;

        void store(const Position& pos, const int depth, const Move best_move = Move::moveNone()) {
            store(board_index_key(pos), secondary_board_key(pos), pos.hand(Black), depth, best_move);
        }

        void store(const Key board_index, const uint64_t board_secondary, const Hand black_stand,
            const int depth, const Move best_move = Move::moveNone()) {
            const OslmateBoardKey board_key = make_oslmate_board_key(board_index, board_secondary);
            PvDepthEntry& entry = depth_table_[make_key(board_key, black_stand)];
            entry.depth = depth;
            entry.best_move = best_move;
            entry.black_stand = black_stand;
            depth_index_[board_key].push_front(&entry);
        }

        bool find(const Position& pos, int& depth, Move& best_move) const {
            return find(board_index_key(pos), secondary_board_key(pos), pos.hand(Black), depth, best_move);
        }

        bool find(const Key board_index, const uint64_t board_secondary, const Hand black_stand,
            int& depth, Move& best_move) const {
            const auto it = depth_table_.find(make_key(
                make_oslmate_board_key(board_index, board_secondary), black_stand));
            if (it == depth_table_.end()) {
                return false;
            }
            depth = it->second.depth;
            best_move = it->second.best_move;
            return true;
        }

        bool expect_more_depth(const Color attack_color, const Position& pos, const int depth) const {
            return expect_more_depth(attack_color, board_index_key(pos), secondary_board_key(pos), pos.hand(Black), depth, &pos);
        }

        bool expect_more_depth(const Color attack_color, const Key board_index, const uint64_t board_secondary, const Hand black_stand,
            const int depth, const Position* = nullptr) const {
            const auto index_it = depth_index_.find(make_oslmate_board_key(board_index, board_secondary));
            if (index_it == depth_index_.end()) {
                return true;
            }

            for (const PvDepthEntry* entry : index_it->second) {
                if (attack_color == Black) {
                    if (osl_stand_is_superior_or_equal(entry->black_stand, black_stand)) {
                        if (entry->depth >= depth) {
                            return true;
                        }
                    }
                    else if (osl_stand_is_superior_or_equal(black_stand, entry->black_stand)) {
                        if (entry->depth < depth) {
                            return false;
                        }
                    }
                }
                else {
                    if (osl_stand_is_superior_or_equal(entry->black_stand, black_stand)) {
                        if (entry->depth < depth) {
                            return false;
                        }
                    }
                    else if (osl_stand_is_superior_or_equal(black_stand, entry->black_stand)) {
                        if (entry->depth >= depth) {
                            return true;
                        }
                    }
                }
            }
            return true;
        }

    private:
        struct KeyWithBlackStand {
            OslmateBoardKey board_key;
            Hand black_stand = Hand(0);

            bool operator==(const KeyWithBlackStand& other) const {
                return board_key == other.board_key
                    && black_stand == other.black_stand;
            }
        };

        struct KeyWithBlackStandHash {
            size_t operator()(const KeyWithBlackStand& key) const {
                const uint64_t hand = static_cast<uint64_t>(key.black_stand.value());
                const size_t board_hash = OslmateBoardKeyHash{}(key.board_key);
                return static_cast<size_t>(
                    board_hash
                    ^ (hand + 0x9e3779b97f4a7c15ull + (board_hash << 6) + (board_hash >> 2)));
            }
        };

        static KeyWithBlackStand make_key(const Position& pos) {
            return { make_oslmate_board_key(pos), pos.hand(Black) };
        }

        static KeyWithBlackStand make_key(const OslmateBoardKey board_key, const Hand black_stand) {
            return { board_key, black_stand };
        }

        std::unordered_map<KeyWithBlackStand, PvDepthEntry, KeyWithBlackStandHash> depth_table_;
        std::unordered_map<OslmateBoardKey, std::forward_list<const PvDepthEntry*>, OslmateBoardKeyHash> depth_index_;
    };

    struct SearchContext {
        Impl& impl;
        std::vector<PathEncoding> path_encodings;
        std::vector<Move> move_history;
        std::vector<Threshold> threshold_history;
        std::vector<PathRecord*> path_records;
        std::unique_ptr<Position> root_position;
        std::optional<OslPieceNumberState> piece_numbers;
        std::vector<OslPieceNumberState::Undo> piece_number_undos;
        std::vector<Key> board_index_history;
        std::vector<uint64_t> board_secondary_history;
        std::vector<std::array<Hand, ColorNum>> stand_history;
        struct ActiveNode {
            DfpnRecord* record = nullptr;
            const std::vector<Move>* moves = nullptr;
            std::vector<ChildState>* children = nullptr;
            Move moved = Move::moveNone();
            Key board_index = 0;
            uint64_t board_secondary = 0;
            Hand black_stand = Hand(0);
            Hand white_stand = Hand(0);
            Threshold threshold;
        };
        std::vector<ActiveNode> active_nodes;
        std::vector<std::vector<Move>> move_scratch;
        std::vector<std::vector<ChildState>> child_scratch;
        uint32_t node_count = 0;
        uint32_t node_limit;
        int max_game_ply = INT_MAX;
        Color attack_color = Black;
        bool use_oracle_path_table = false;
        bool main_search = false;
        PathRecord* returned_path_record = nullptr;

        explicit SearchContext(Impl& owner) : impl(owner), node_limit(owner.max_search_node) {
            const size_t capacity = static_cast<size_t>(std::max(0, owner.max_depth)) + 1;
            path_encodings.reserve(capacity);
            move_history.reserve(capacity);
            threshold_history.reserve(capacity);
            path_records.reserve(capacity);
            piece_number_undos.reserve(capacity);
            board_index_history.reserve(capacity);
            board_secondary_history.reserve(capacity);
            stand_history.reserve(capacity);
            active_nodes.reserve(capacity);
            move_scratch.reserve(capacity);
            child_scratch.reserve(capacity);
        }

        std::vector<Move>& moves_for_current_depth() {
            const size_t depth = move_history.size();
            if (depth >= move_scratch.size()) {
                move_scratch.resize(depth + 1);
                move_scratch.back().reserve(kCheckOrEscapeMoveCapacity);
            }
            return move_scratch[depth];
        }

        std::vector<ChildState>& children_for_current_depth() {
            const size_t depth = move_history.size();
            if (depth >= child_scratch.size()) {
                child_scratch.resize(depth + 1);
                child_scratch.back().reserve(kCheckOrEscapeMoveCapacity);
            }
            return child_scratch[depth];
        }

        void set_root(const Position& pos) {
            root_position = std::make_unique<Position>(pos);
            piece_numbers = OslPieceNumberState::from_position(pos);
            piece_number_undos.clear();
        }

        void set_history_piece_numbers(const Position& root, const std::vector<Move>& history) {
            piece_numbers = OslPieceNumberState::from_history(root, history);
            piece_number_undos.clear();
            piece_number_undos.reserve(history.size() + static_cast<size_t>(64));
        }

        void push_move(const Move move) {
            if (piece_numbers) {
                OslPieceNumberState::Undo undo;
                if (piece_numbers->apply_move(move, &undo)) {
                    piece_number_undos.push_back(undo);
                }
                else {
                    piece_numbers.reset();
                    piece_number_undos.clear();
                }
            }
            move_history.push_back(move);
        }

        void pop_move() {
            if (piece_numbers && !piece_number_undos.empty()) {
                piece_numbers->undo_move(piece_number_undos.back());
                piece_number_undos.pop_back();
            }
            if (!move_history.empty()) {
                move_history.pop_back();
            }
        }

        bool aborted() const {
            return impl.stop || node_count >= node_limit;
        }

        bool stopped() const {
            return impl.stop;
        }

        bool exhausted() const {
            return node_count >= node_limit;
        }

        uint64_t current_board_secondary(const Position& pos) const {
            return board_secondary_history.empty() ? secondary_board_key(pos) : board_secondary_history.back();
        }

        Key current_board_index(const Position& pos) const {
            return board_index_history.empty() ? board_index_key(pos) : board_index_history.back();
        }

        std::array<Hand, ColorNum> current_stands(const Position& pos) const {
            return stand_history.empty() ? stand_pair(pos) : stand_history.back();
        }

        uint64_t child_board_secondary(const Position& pos, const Move move) const {
            return secondary_board_key_after_move(
                current_board_secondary(pos), pos.turn(), complete_move_for_position(pos, move));
        }

        Key child_board_index(const Position& pos, const Move move) const {
            return board_index_key_after_move(
                current_board_index(pos), pos.turn(), complete_move_for_position(pos, move));
        }

        std::array<Hand, ColorNum> child_stands(const Position& pos, const Move move) const {
            return stand_pair_after_move(
                current_stands(pos), pos.turn(), complete_move_for_position(pos, move));
        }
    };

    struct ReturnPathRecordScope {
        SearchContext& ctx;
        PathRecord* path_record;

        ~ReturnPathRecordScope() {
            ctx.returned_path_record = path_record;
        }
    };

    struct ActiveNodeScope {
        SearchContext& ctx;
        bool active = false;

        ActiveNodeScope(SearchContext& context, DfpnRecord& record, const std::vector<Move>& moves, const Position& pos)
            : ctx(context), active(true) {
            const Threshold threshold = ctx.threshold_history.empty()
                ? Threshold{ ProofDisproof::PROOF_MAX, ProofDisproof::DISPROOF_MAX }
                : ctx.threshold_history.back();
            SearchContext::ActiveNode active_node;
            active_node.record = &record;
            active_node.moves = &moves;
            active_node.moved = ctx.move_history.empty() ? Move::moveNone() : ctx.move_history.back();
            active_node.board_index = ctx.current_board_index(pos);
            active_node.board_secondary = ctx.current_board_secondary(pos);
            const std::array<Hand, ColorNum> current_stands = ctx.current_stands(pos);
            active_node.black_stand = current_stands[Black];
            active_node.white_stand = current_stands[White];
            active_node.threshold = threshold;
            ctx.active_nodes.push_back(std::move(active_node));
        }

        void set_children(std::vector<ChildState>& children) {
            if (active && !ctx.active_nodes.empty()) {
                ctx.active_nodes.back().children = &children;
            }
        }

        ~ActiveNodeScope() {
            if (active) {
                ctx.active_nodes.pop_back();
            }
        }
    };

    Impl() {
        reserve_by_hash(reserved_hash_mb);
    }

    void reserve_by_hash(const uint64_t hash_mb) {
        reserved_hash_mb = std::max<uint64_t>(1, hash_mb);
        const uint64_t bytes = reserved_hash_mb * 1024ull * 1024ull;
        const uint64_t approx_entry = std::max<uint64_t>(
            1024,
            bytes / kOslmateHashGrowthUnitSize);
        const size_t growth_limit = static_cast<size_t>(
            std::min<uint64_t>(approx_entry, std::numeric_limits<size_t>::max()));
        table.set_growth_limit(growth_limit);
        path_table.reserve(growth_limit);
        oracle_path_table.reserve(growth_limit / 4 + 1);
    }

    void run_gc_if_needed(SearchContext& ctx) {
        if (max_depth <= kEnableGCDepth) {
            return;
        }
        const bool table_gc_needed =
            table.size() >= table.growth_limit()
            || table.growth_limit() - table.size() < table.growth_limit() / 8;
        size_t removed = 0;
        if (table_gc_needed) {
            removed = table.run_gc();
        }
        const bool path_gc_needed = path_table.size() > table.growth_limit();
        if (path_gc_needed) {
            const size_t removed_path = path_table.run_gc();
            if (removed_path > 0) {
                for (SearchContext::ActiveNode& active : ctx.active_nodes) {
                    if (!active.children) {
                        continue;
                    }
                    for (ChildState& child : *active.children) {
                        child.path_record = nullptr;
                    }
                }
            }
        }
    }

    uint16_t remaining_depth(const Position& pos, const SearchContext& ctx) const {
        if (pos.gamePly() >= ctx.max_game_ply) {
            return 0;
        }
        return static_cast<uint16_t>(ctx.max_game_ply - pos.gamePly());
    }

    bool osl_tree_depth_limit_reached(const SearchContext& ctx) const {
        const size_t tree_depth = std::max({
            ctx.move_history.size(),
            ctx.active_nodes.size(),
            ctx.path_records.size(),
        });
        return tree_depth + 2 >= static_cast<size_t>(std::max(0, max_depth));
    }

    DfpnRecord probe(const Position& pos, const Color attack_color, const bool use_initial_dominance = true) const {
        return table.probe(pos, attack_color, use_initial_dominance);
    }

    DfpnRecord probe(const Position& pos, const uint64_t board_secondary,
        const Color attack_color, const bool use_initial_dominance = true) const {
        return table.probe(board_index_key(pos), board_secondary, stand_pair(pos), attack_color, use_initial_dominance);
    }

    DfpnRecord probe(const Position& pos, const Key board_index, const uint64_t board_secondary,
        const Color attack_color, const bool use_initial_dominance = true) const {
        return table.probe(board_index, board_secondary, stand_pair(pos), attack_color, use_initial_dominance);
    }

    DfpnRecord probe(const Key board_index, const uint64_t board_secondary,
        const std::array<Hand, ColorNum>& stands, const Color attack_color,
        const bool use_initial_dominance = true) const {
        return table.probe(board_index, board_secondary, stands, attack_color, use_initial_dominance);
    }

    DfpnRecord find_proof_oracle(const OracleState& oracle, const Color attack_color, const Move last_move) const {
        return table.findProofOracle(oracle.board_index, oracle.board_secondary, oracle.stands, attack_color, last_move);
    }

    PathRecord* path_state(const SearchContext& ctx, const Position& pos, const int depth, DfpnPathTable::LoopToDominance& loop) {
        return (ctx.use_oracle_path_table ? oracle_path_table : path_table).allocate(
            ctx.current_board_index(pos), ctx.current_board_secondary(pos), pos.hand(Black), ctx.attack_color, depth, loop);
    }

    const PathRecord* probe_path(const SearchContext& ctx, const Position& pos, const Hand& attack_stand) const {
        (void)attack_stand;
        return (ctx.use_oracle_path_table ? oracle_path_table : path_table).probe(
            ctx.current_board_index(pos), ctx.current_board_secondary(pos), pos.hand(Black));
    }

    const PathRecord* probe_child_path(const SearchContext& ctx,
        const Key board_index, const uint64_t board_secondary, const Hand& black_stand) const {
        return (ctx.use_oracle_path_table ? oracle_path_table : path_table).probe(
            board_index, board_secondary, black_stand);
    }

    void store(const Position& pos, const ProofDisproof pdp, const Move best_move, const uint32_t node_count, const uint16_t remaining) {
        DfpnRecord& entry = table.store(pos);
        entry.refresh_stands(pos);
        if (!entry.proof_disproof.isFinal() || pdp.isFinal() || remaining >= entry.remaining_depth) {
            entry.proof_disproof = pdp;
            entry.best_move = best_move;
            entry.node_count = node_count;
            entry.remaining_depth = remaining;
        }
    }

    DfpnRecord load_exact_record(const Position& pos) const {
        // OSL's attack/defense uses DfpnTable::probe directly.  That includes
        // same-stand records, proof/disproof dominance, and initial dominance.
        return table.probe(pos, root_attack_color);
    }

    DfpnRecord load_exact_record(const Position& pos, const uint64_t board_secondary) const {
        return table.probe(board_index_key(pos), board_secondary, stand_pair(pos), root_attack_color);
    }

    DfpnRecord load_exact_record(const Position& pos, const Key board_index, const uint64_t board_secondary) const {
        return table.probe(board_index, board_secondary, stand_pair(pos), root_attack_color);
    }

    DfpnRecord load_exact_record(const Key board_index, const uint64_t board_secondary,
        const std::array<Hand, ColorNum>& stands) const {
        return table.probe(board_index, board_secondary, stands, root_attack_color);
    }

        void commit_exact_record(const Position& pos, const uint64_t board_secondary, DfpnRecord& src, const char* reason = "") {
            commit_exact_record(pos, board_index_key(pos), board_secondary, src, reason);
        }

        void commit_exact_record(const Position& pos, const Key board_index,
            const uint64_t board_secondary, DfpnRecord& src, const char* reason = "") {
        table.store_exact_oslmate(pos, board_index, board_secondary, src);
    }

        void commit_exact_record(const Key board_index, const uint64_t board_secondary,
            const std::array<Hand, ColorNum>& stands, DfpnRecord& src, const char* reason = "") {
            (void)reason;
            table.store_exact_oslmate(board_index, board_secondary, stands, src);
    }

        void commit_exact_record(const Position& pos, DfpnRecord& src, const char* reason = "") {
            commit_exact_record(pos, secondary_board_key(pos), src, reason);
    }

        void commit_oracle_record(const Position& pos, const uint64_t board_secondary, DfpnRecord& src) {
            commit_oracle_record(pos, board_index_key(pos), board_secondary, src);
        }

        void commit_oracle_record(const Position& pos, const Key board_index,
            const uint64_t board_secondary, DfpnRecord& src) {
        if (src.exact) {
            table.store_exact_oslmate(pos, board_index, board_secondary, src);
        }
        else {
            table.store_nonexact_oslmate(pos, board_index, board_secondary, src);
        }
    }

        void commit_oracle_record(const Position& pos, DfpnRecord& src) {
            commit_oracle_record(pos, secondary_board_key(pos), src);
    }

    ProofDisproof attack(Position& pos, SearchContext& ctx, Threshold threshold, Move* best_move, DfpnRecord* out_exact = nullptr);
    ProofDisproof defense(Position& pos, SearchContext& ctx, Threshold threshold, DfpnRecord* out_exact = nullptr);
    ProofDisproof try_proof(Position& pos, SearchContext& ctx, uint32_t oracle_id, Move* best_move);
    ProofDisproof proof_oracle_attack(Position& pos, const OracleState& oracle, SearchContext& ctx, int proof_limit,
        Move* best_move, DfpnRecord* out_record = nullptr, bool use_table = true);
    ProofDisproof proof_oracle_defense(Position& pos, const OracleState& oracle, SearchContext& ctx,
        int proof_limit, DfpnRecord* out_record = nullptr, bool use_table = true);
    void blocking_simulation(Position& pos, SearchContext& ctx, int proof_limit, DfpnRecord& exact_record,
        const std::vector<Move>& moves, std::vector<ChildState>& children, size_t oracle_index);
    void grand_parent_simulation(Position& pos, SearchContext& ctx, DfpnRecord& exact_record,
        const std::vector<Move>& moves, std::vector<ChildState>& children, size_t child_index);
    void set_parent_link(DfpnRecord& record, const SearchContext& ctx) const;
    Position replay_position(const SearchContext& ctx, size_t ply) const;
    void find_dag_source(Position& pos, SearchContext& ctx, DfpnRecord& terminal_record, size_t offset, const char* call_site);
    void find_dag_source(Key terminal_board_index, uint64_t terminal_board_secondary,
        const std::array<Hand, ColorNum>& terminal_stands, Color terminal_turn,
        SearchContext& ctx, DfpnRecord& terminal_record, size_t offset, const char* call_site);
    ProofDisproof ensure_attack(Position& pos);
    ProofDisproof ensure_defense(Position& pos);
    bool linked_pv_attack_move(Position& pos, Move& best_move, Move incoming_move);
    bool linked_pv_defense_move(Position& pos, Move& best_move, Move incoming_move);
    int pv_attack(Position& pos, PvDepthTable& table, Move& best_move, int height, Move incoming_move,
        Key board_index, uint64_t board_secondary, OslPieceNumberState* piece_numbers);
    int pv_defense(Position& pos, PvDepthTable& table, Move& best_move, int height, Move incoming_move,
        Key board_index, uint64_t board_secondary, OslPieceNumberState* piece_numbers);
    void retrieve_pv(Position& pos, bool attack_node, std::vector<u32>& pv);
};

void DfPn::Impl::set_parent_link(DfpnRecord& record, const SearchContext& ctx) const {
    (void)record;
    (void)ctx;
}

Position DfPn::Impl::replay_position(const SearchContext& ctx, const size_t ply) const {
    assert(ctx.root_position);
    Position replay = *ctx.root_position;
    std::vector<StateInfo> states(ply);
    for (size_t i = 0; i < ply; ++i) {
        replay.doMove(ctx.move_history[i], states[i]);
    }
    return replay;
}

void DfPn::Impl::find_dag_source(Position& pos, SearchContext& ctx, DfpnRecord& terminal_record, const size_t offset, const char* call_site) {
    find_dag_source(board_index_key(pos), secondary_board_key(pos), stand_pair(pos), pos.turn(),
        ctx, terminal_record, offset, call_site);
}

void DfPn::Impl::find_dag_source(const Key terminal_board_index, const uint64_t terminal_board_secondary,
    const std::array<Hand, ColorNum>& terminal_stands, const Color terminal_turn,
    SearchContext& ctx, DfpnRecord& terminal_record, const size_t offset, const char* call_site) {
    if (!kEnableNagaiDagTest) {
        return;
    }
    if (!is_osl_normal_move(terminal_record.last_move)) {
        return;
    }
    Key current_board_index = terminal_board_index;
    uint64_t current_board_secondary = terminal_board_secondary;
    // OSL starts DAG backtracking from the child HashKey and child white stand
    // passed by the caller, not from the probed DfpnRecord::stands.
    std::array<Hand, ColorNum> current_stands = terminal_stands;
    DfpnRecord current = terminal_record;
    Color current_turn = terminal_turn;
    const int active_depth = static_cast<int>(ctx.active_nodes.size()) - 1;
    if (active_depth < 0) {
        return;
    }
    const size_t dag_limit = std::min(
        kMaxDagTraceDepth,
        static_cast<size_t>(std::max(0, max_depth)));
    for (size_t d = offset; d < dag_limit; ++d) {
        if (!is_osl_normal_move(current.last_move)) {
            return;
        }
        const Color mover = oppositeColor(current_turn);
        const OracleState parent_state = oracle_state_before_move(
            current_board_index, current_board_secondary, current_stands, current.last_move, mover);
        for (int i = active_depth - 4 - static_cast<int>(d % 2); i >= 0; i -= 2) {
            if (static_cast<size_t>(i) >= ctx.active_nodes.size()
                || static_cast<size_t>(i + 1) >= ctx.active_nodes.size()) {
                continue;
            }
            SearchContext::ActiveNode& ancestor_node = ctx.active_nodes[static_cast<size_t>(i)];
            const Move path_move = ctx.active_nodes[static_cast<size_t>(i + 1)].moved;
            // OSL findDagSource matches `parent_key == node.hash_key`.
            // HashKey equality includes board 96-bit and the black stand.
            // The white stand is used for table probing while backtracking,
            // but it is not part of HashKey equality.
            if (ancestor_node.board_index != parent_state.board_index
                || ancestor_node.board_secondary != parent_state.board_secondary
                || ancestor_node.black_stand != parent_state.stands[Black]
                || !ancestor_node.record
                || !ancestor_node.moves) {
                continue;
            }

            DfpnRecord& ancestor_record = *ancestor_node.record;
            const std::vector<Move>& ancestor_moves = *ancestor_node.moves;
            for (size_t m = 0; m < std::min<size_t>(ancestor_moves.size(), 64); ++m) {
                if (ancestor_moves[m] == path_move || ancestor_moves[m] == current.last_move) {
                    ancestor_record.dag_moves |= child_bit(static_cast<size_t>(m));
                }
            }
            terminal_record.dag_terminal = true;
            return;
        }

        current_board_index = parent_state.board_index;
        current_board_secondary = parent_state.board_secondary;
        current_stands = parent_state.stands;
        current_turn = mover;
        current = table.probe(current_board_index, current_board_secondary, current_stands, ctx.attack_color);
    }
}

ProofDisproof DfPn::Impl::attack(Position& pos, SearchContext& ctx, Threshold threshold, Move* best_move, DfpnRecord* out_exact) {
    if (best_move) {
        *best_move = Move::moveNone();
    }
    if (pos.gamePly() >= ctx.max_game_ply || osl_tree_depth_limit_reached(ctx)) {
        throw DepthLimitReached();
    }

    const Key key = pos.getKey();
    ctx.threshold_history.push_back(threshold);
    struct ThresholdHistoryPop {
        std::vector<Threshold>& threshold_history;
        ~ThresholdHistoryPop() {
            threshold_history.pop_back();
        }
    } threshold_history_pop{ ctx.threshold_history };

    const bool pushed_board_index = ctx.board_index_history.empty();
    if (pushed_board_index) {
        ctx.board_index_history.push_back(board_index_key(pos));
        ctx.board_secondary_history.push_back(secondary_board_key(pos));
        ctx.stand_history.push_back(stand_pair(pos));
    }
    struct BoardHistoryPop {
        std::vector<Key>& board_index_history;
        std::vector<uint64_t>& board_secondary_history;
        std::vector<std::array<Hand, ColorNum>>& stand_history;
        bool active;
        ~BoardHistoryPop() {
            if (active) {
                board_index_history.pop_back();
                board_secondary_history.pop_back();
                stand_history.pop_back();
            }
        }
    } board_history_pop{ ctx.board_index_history, ctx.board_secondary_history, ctx.stand_history, pushed_board_index };

    const PathEncoding current_path = ctx.path_encodings.empty() ? PathEncoding(ctx.attack_color) : ctx.path_encodings.back();
    const bool pushed_path = ctx.path_encodings.empty();
    if (pushed_path) {
        ctx.path_encodings.push_back(current_path);
    }
    struct PathEncodingPop {
        std::vector<PathEncoding>& path_encodings;
        bool active;
        ~PathEncodingPop() {
            if (active) {
                path_encodings.pop_back();
            }
        }
    } path_hash_pop{ ctx.path_encodings, pushed_path };

    const int my_distance = ctx.path_records.empty()
        ? static_cast<int>(ctx.move_history.size())
        : ctx.path_records.back()->distance + 1;
    DfpnPathTable::LoopToDominance loop = DfpnPathTable::LoopToDominance::NoLoop;
    PathRecord* path_record = path_state(ctx, pos, my_distance, loop);
    ReturnPathRecordScope return_path_record_scope{ ctx, path_record };
    VisitLock visit_lock(path_record);
    if (loop == DfpnPathTable::LoopToDominance::BadAttackLoop) {
        DfpnRecord loop_record;
        set_loop_detection_record(loop_record, path_record, current_path, out_exact);
        return loop_record.proof_disproof;
    }
    ctx.path_records.push_back(path_record);
    struct PathRecordStackPop {
        std::vector<PathRecord*>& path_records;
        ~PathRecordStackPop() { path_records.pop_back(); }
    } path_record_stack_pop{ ctx.path_records };

    const uint32_t node_count_org = ctx.node_count;
    ++ctx.node_count;
    const auto store_out_exact = [&](const DfpnRecord& value) {
        if (out_exact) {
            *out_exact = value;
        }
    };

    // OSL has a one-ply mate shortcut here only when CHECKMATE_D2 is not defined.
    // The oslmate build used as the reference defines CHECKMATE_D2, so main DFPN
    // attack must not take this shortcut.

    const Key current_board_index = ctx.current_board_index(pos);
    const uint64_t current_secondary = ctx.current_board_secondary(pos);
    const std::array<Hand, ColorNum> current_stands = ctx.current_stands(pos);
    const DfpnRecord record = probe(current_board_index, current_secondary, current_stands, ctx.attack_color);
    const auto current_exact_record = [&](const DfpnRecord* fallback = nullptr) {
        (void)fallback;
        DfpnRecord current = load_exact_record(current_board_index, current_secondary, current_stands);
        current.refresh_stands(current_secondary, current_stands);
        return current;
    };
    if (record.proof_disproof.isFinal())
    {
        store_out_exact(record);
        if (best_move) {
            *best_move = record.best_move;
        }
        return record.proof_disproof;
    }
    DfpnRecord exact_record = record;
    exact_record.refresh_stands(current_secondary, current_stands);
    exact_record.exact = true;
    const bool entered_full_width = exact_record.need_full_width != 0;
    const Move current_parent_move = ctx.move_history.empty() ? Move::moveNone() : ctx.move_history.back();
    const Move recorded_last_move = exact_record.last_move;
    if (!exact_record.proof_disproof.isFinal()) {
        exact_record.proof_pieces_set = ProofPiecesType::Unset;
    }

    if (ctx.move_history.empty()
        && ctx.node_limit <= 50
        && record.node_count >= ctx.node_limit) {
        store_out_exact(record);
        if (best_move) {
            *best_move = record.best_move;
        }
        return record.proof_disproof;
    }


    // OSL has CHECKMATE_D2 and CHECKMATE_A3_GOLD enabled.  D2 is used from
    // defense child probing, while A3_GOLD still runs this attack shortcut at
    // the root and at unknown gold-in-hand edge-king nodes.
    const bool try_fixed_attack = should_try_fixed_attack(pos, record, ctx.attack_color, ctx.move_history.empty());
    if (try_fixed_attack) {
        Move fixed_best = Move::moveNone();
        Hand fixed_proof_pieces = zero_hand();
        const ProofDisproof fixed_pdp = fixed_attack_osl_shortcut(pos, ctx.attack_color, &fixed_best, &fixed_proof_pieces,
            ctx.piece_numbers ? &*ctx.piece_numbers : nullptr);
        ++ctx.node_count;
        const bool fixed_success = fixed_pdp.isCheckmateSuccess();
        if (fixed_success) {
            if (best_move) {
                *best_move = fixed_best;
            }
            exact_record.proof_disproof = fixed_pdp;
            exact_record.best_move = fixed_best;
            exact_record.last_move = current_parent_move;
            set_parent_link(exact_record, ctx);
            exact_record.node_count = saturate_sum(static_cast<uint64_t>(record.node_count) + 1, UINT32_MAX);
            exact_record.remaining_depth = remaining_depth(pos, ctx);
            exact_record.proof_pieces_set = ProofPiecesType::Unset;
            exact_record.setProofPieces(fixed_proof_pieces);
            commit_exact_record(pos, ctx.current_board_index(pos), current_secondary, exact_record, "attack-fixed-success");
            store_out_exact(exact_record);
            return exact_record.proof_disproof;
        }
    }

    ctx.impl.run_gc_if_needed(ctx);

    bool has_pawn_checkmate = false;
    std::vector<Move>& moves = ctx.moves_for_current_depth();
    generate_check_moves(pos, moves, &has_pawn_checkmate, ctx.root_position.get(), &ctx.move_history,
        ctx.piece_numbers ? &*ctx.piece_numbers : nullptr);
    if (moves.empty()) {
        exact_record.proof_disproof = has_pawn_checkmate
            ? ProofDisproof::PawnCheckmate()
            : ProofDisproof::NoCheckmate();
        set_parent_link(exact_record, ctx);
        exact_record.remaining_depth = remaining_depth(pos, ctx);
        exact_record.proof_pieces_set = ProofPiecesType::Unset;
        exact_record.setDisproofPieces(disproof_pieces_leaf(pos, ctx.attack_color, exact_record.stands[oppositeColor(ctx.attack_color)]));
        store_out_exact(exact_record);
        return exact_record.proof_disproof;
    }
    ActiveNodeScope active_node(ctx, exact_record, moves, pos);
    const Square defender_king = pos.kingSquare(oppositeColor(ctx.attack_color));
    const King8RuntimeInfo king8_info = reset_edge_from_liberty_runtime(defender_king, ctx.attack_color, king8_runtime_info_at(pos, ctx.attack_color));
    std::vector<ChildState>& children = ctx.children_for_current_depth();
    children.clear();
    children.resize(moves.size());
    active_node.set_children(children);
    uint32_t sum_frontier_proof = 0;
    uint32_t frontier_count = 0;
        for (size_t i = 0; i < moves.size(); ++i) {
            children[i].reset_for_move(moves[i]);
            if (exact_record.solved & child_bit(i)) {
                continue;
            }
            const Move current_move_with_capture = moves[i];
            Key child_board_index = 0;
            uint64_t child_board_secondary = 0;
            std::array<Hand, ColorNum> child_stands{ Hand(0), Hand(0) };
            {
                const OslBoardKeyParts child_keys = board_keys_after_move(
                    current_board_index, current_secondary, pos.turn(), current_move_with_capture);
                child_board_index = child_keys.board_index;
                child_board_secondary = child_keys.board_secondary;
                child_stands = stand_pair_after_move(
                    current_stands, pos.turn(), current_move_with_capture);
            }
            children[i].board_index = child_board_index;
            children[i].board_secondary = child_board_secondary;
            children[i].stands = child_stands;
            DfpnRecord child_record = table.probe(
                child_board_index, child_board_secondary, child_stands, ctx.attack_color);
            const bool estimate_child = child_record.proof_disproof == ProofDisproof(1, 1);
            const Color defender = oppositeColor(ctx.attack_color);
            int attack_support_base = 0;
            int defense_support_base = 0;
            int attack_support_with_drop = 0;
            if (estimate_child || !moves[i].isCapture()) {
                const Square move_to = moves[i].to();
                attack_support_base = effect_count(pos, ctx.attack_color, move_to);
                defense_support_base = effect_count(pos, defender, move_to);
                attack_support_with_drop = attack_support_base + (moves[i].isDrop() ? 1 : 0);
            }
            const int8_t estimated_attack_proof_cost = moves[i].isCapture()
                ? 0
                : static_cast<int8_t>(attack_proof_cost_with_support(
                    pos, ctx.attack_color, moves[i], attack_support_with_drop, defense_support_base));
            const ProofDisproof estimated_child_pdp = estimate_child
                ? estimate_attack_pdp_with_support(
                    pos, ctx.attack_color, king8_info, moves[i], attack_support_base, defense_support_base)
                : child_record.proof_disproof;
            bool pawn_drop_no_escape_child = false;
            {
                {
                    children[i].pdp = child_record.proof_disproof;
                    if (estimate_child) {
                        children[i].pdp = estimated_child_pdp;
                    }
                    pawn_drop_no_escape_child = is_pawn_drop_no_escape(moves[i], children[i].pdp);
                    if (pawn_drop_no_escape_child) {
                        children[i].pdp = ProofDisproof::PawnCheckmate();
                        exact_record.solved |= child_bit(i);
                        exact_record.min_pdp = std::min(exact_record.min_pdp, ProofDisproof::PAWN_CHECK_MATE_PROOF);
                    }
                    children[i].best_reply = child_record.best_move;
                    children[i].last_move = child_record.last_move;
                    children[i].proof_pieces_type = child_record.proof_pieces_set;
                    children[i].proof_pieces = child_record.proof_pieces;
                    children[i].node_count = child_record.node_count;
                    children[i].need_full_width = child_record.need_full_width;
                    copy_record_aux_to_child(children[i], child_record);
                }
        }
        if (children[i].pdp.isCheckmateFail() && !pawn_drop_no_escape_child) {
            set_no_checkmate_child_in_attack(exact_record, children[i], i);
        }
        else if (children[i].pdp.isCheckmateSuccess()) {
            exact_record.proof_disproof = children[i].pdp;
            exact_record.best_move = moves[i];
            exact_record.last_move = current_parent_move;
            set_parent_link(exact_record, ctx);
            accumulate_record_node_count(exact_record, node_count_org, ctx.node_count);
            exact_record.remaining_depth = remaining_depth(pos, ctx);
            exact_record.proof_pieces_set = ProofPiecesType::Unset;
            exact_record.setProofPieces(proof_pieces_after_attack(
                children[i].proof_pieces,
                current_move_with_capture, exact_record.stands[ctx.attack_color]));
            commit_exact_record(pos, ctx.current_board_index(pos), current_secondary, exact_record, "attack-init-child-success");
            store_out_exact(exact_record);
            set_path_node_count(path_record, 0);
            if (best_move) {
                *best_move = exact_record.best_move;
            }
            return exact_record.proof_disproof;
        }
        else if (children[i].node_count == 0) {
            ++frontier_count;
            sum_frontier_proof += children[i].pdp.proof;
        }
        else if (!children[i].pdp.isFinal()
            && is_osl_normal_move(children[i].last_move)
            && children[i].last_move != moves[i]
            && std::max(children[i].pdp.proof, children[i].pdp.disproof) >= kDagFindThreshold2) {
            DfpnRecord child_exact_mut(child_board_secondary, child_stands);
            child_exact_mut.proof_disproof = children[i].pdp;
            child_exact_mut.best_move = children[i].best_reply;
            child_exact_mut.last_move = children[i].last_move;
            child_exact_mut.proof_pieces = children[i].proof_pieces;
            child_exact_mut.proof_pieces_set = children[i].proof_pieces_type;
            child_exact_mut.node_count = children[i].node_count;
            child_exact_mut.need_full_width = children[i].need_full_width;
            copy_child_aux_to_record(child_exact_mut, children[i]);
            child_exact_mut.exact = true;
            find_dag_source(child_board_index, child_board_secondary, child_stands, oppositeColor(ctx.attack_color),
                ctx, child_exact_mut, 0, "attack-init-child");
            children[i].pdp = child_exact_mut.proof_disproof;
            children[i].best_reply = child_exact_mut.best_move;
            children[i].last_move = child_exact_mut.last_move;
            children[i].proof_pieces = child_exact_mut.proof_pieces;
            children[i].proof_pieces_type = child_exact_mut.proof_pieces_set;
            children[i].node_count = child_exact_mut.node_count;
            children[i].need_full_width = child_exact_mut.need_full_width;
            copy_record_aux_to_child(children[i], child_exact_mut);
        }
        children[i].path_record = probe_child_path(ctx, child_board_index, child_board_secondary, child_stands[Black]);
        children[i].proof_cost = estimated_attack_proof_cost;
    }
    ProofDisproof result = exact_record.proof_disproof;
    Move result_best = exact_record.best_move;
    // OSL defines PROOF_AVERAGE in dfpn.cc, so the widening adjustment uses
    // the average proof of frontier children.
    // OSL updates last_move after child probing/DAG2 setup and before the main loop.
    exact_record.last_move = current_parent_move;
    const uint32_t proof_average = frontier_count ? sum_frontier_proof / frontier_count : 1;
    if (ctx.move_history.empty()) {
        const uint32_t root_children_limit_filter = (UINT32_MAX);
    }
    size_t final_next_index = moves.size();
    for (int loop_iteration = 0;; ++loop_iteration) {
        uint32_t min_proof = exact_record.min_pdp;
        uint32_t second_proof = exact_record.min_pdp;
        uint64_t sum_disproof64 = 0;
        uint64_t max_disproof_dag = 0;
        uint64_t max_drop_disproof_rook = 0;
        uint64_t max_drop_disproof_bishop = 0;
        uint64_t max_drop_disproof_lance = 0;
        int max_children_depth = 0;
        size_t next_index = moves.size();

        for (size_t i = 0; i < children.size(); ++i) {
            if (exact_record.solved & child_bit(i)) {
                continue;
            }
            if (i > 0
                && min_proof < ProofDisproof::PROOF_LIMIT
                && moves[i].fromAndTo() == moves[i - 1].fromAndTo()
                && !moves[i].isDrop()) {
                exact_record.dag_moves |= child_bit(i) | child_bit(i - 1);
                if (threshold.proof < kNoPromoteIgnoreProofThreshold
                    && threshold.disproof < kNoPromoteIgnoreDisproofThreshold) {
                    continue;
                }
            }
            ProofDisproof child = children[i].pdp;
            const ChildLoopReason loop_reason = child_loop_reason(children[i], current_path);
            if (loop_reason != ChildLoopReason::None) {
                child = ProofDisproof::LoopDetection();
                children[i].pdp = child;
            }
            else if (children[i].path_record && !child.isFinal()) {
                max_children_depth = std::max(max_children_depth, children[i].path_record->distance);
            }
            else if (!children[i].path_record && path_record) {
                max_children_depth = path_record->distance + 1;
            }
            uint64_t child_proof = child.proof;
            const uint32_t child_disproof = child.disproof;
            if (child_proof != 0 && child_disproof != 0) {
                child_proof += children[i].proof_cost;
            }
            uint64_t disproof = child_disproof;
            if (children[i].path_record && !child.isFinal()) {
                if (kEnableNagaiDagTest && (exact_record.dag_moves & child_bit(i)) != 0) {
                    max_disproof_dag = std::max(max_disproof_dag, disproof);
                    disproof = 0;
                }
                else {
                    const LongDropClass long_drop_class = long_drop_attack_class(pos, ctx.attack_color, moves[i], defender_king);
                    if (long_drop_class != LongDropClass::None) {
                        uint64_t* target = &max_drop_disproof_lance;
                        if (long_drop_class == LongDropClass::Rook) {
                            target = &max_drop_disproof_rook;
                        }
                        else if (long_drop_class == LongDropClass::Bishop) {
                            target = &max_drop_disproof_bishop;
                        }
                        *target = std::max(*target, disproof);
                        disproof = kLongDropCount;
                    }
                }
            }

            if (child_proof < min_proof
                || (child_proof == min_proof
                    && disproof != 0
                    && (next_index == moves.size() || disproof < children[next_index].pdp.disproof))) {
                second_proof = min_proof;
                min_proof = static_cast<uint32_t>(child_proof);
                next_index = i;
            }
            else if (child_proof < second_proof) {
                second_proof = static_cast<uint32_t>(child_proof);
            }

            sum_disproof64 += disproof;
        }

        uint64_t sum_disproof_with_delays =
            sum_disproof64 + max_drop_disproof_rook + max_drop_disproof_bishop + max_drop_disproof_lance + max_disproof_dag;
        if (kLongDropCount != 0) {
            if (max_drop_disproof_rook != 0) {
                sum_disproof_with_delays -= kLongDropCount;
            }
            if (max_drop_disproof_bishop != 0) {
                sum_disproof_with_delays -= kLongDropCount;
            }
            if (max_drop_disproof_lance != 0) {
                sum_disproof_with_delays -= kLongDropCount;
            }
        }
        uint64_t sum_disproof = sum_disproof_with_delays;
        if (path_record && path_record->distance >= max_children_depth) {
            path_record->distance = max_children_depth - 1;
        }
        if (kEnableKishimotoWidenThreshold
            && loop_iteration == 0
            && sum_disproof >= threshold.disproof
            && sum_disproof > kIgnoreUpwardDisproofThreshold) {
            threshold.disproof = static_cast<uint32_t>(sum_disproof + 1);
            if (!ctx.active_nodes.empty()) {
                ctx.active_nodes.back().threshold = threshold;
            }
        }
        if (sum_disproof < kRootDisproofTolerance
            && min_proof > 0
            && sum_disproof > static_cast<uint64_t>(min_proof) * kAdHocSumScale) {
            const uint64_t scaled_min = static_cast<uint64_t>(min_proof) * kAdHocSumScale;
            sum_disproof = scaled_min + slow_increase(static_cast<uint32_t>(sum_disproof - scaled_min));
        }
        result = { min_proof, static_cast<uint32_t>(sum_disproof) };
        const bool node_limit_reached = saturate_sum(
            static_cast<uint64_t>(ctx.node_count) + min_proof,
            ctx.node_limit) >= ctx.node_limit;
        if (next_index == moves.size()
            || min_proof >= threshold.proof
            || sum_disproof >= threshold.disproof
            || node_limit_reached)
        {
            final_next_index = next_index;
            exact_record.proof_disproof = result;
            if (best_move) {
                *best_move = result_best;
            }
            if (result.isLoopDetection()) {
                exact_record.proof_disproof = ProofDisproof::Unknown();
                set_parent_link(exact_record, ctx);
                accumulate_record_node_count(exact_record, node_count_org, ctx.node_count);
                exact_record.remaining_depth = remaining_depth(pos, ctx);
                commit_exact_record(pos, ctx.current_board_index(pos), current_secondary, exact_record, "attack-final-loop");
                store_out_exact(exact_record);
                set_path_node_count(path_record, exact_record.node_count);
                return mark_loop(path_record, current_path);
            }
            if (result.isCheckmateFail()) {
                Hand pieces = exact_record.proof_pieces_candidate;
                add_monopolized_pieces(pos.hand(oppositeColor(ctx.attack_color)), pos.hand(ctx.attack_color),
                    exact_record.stands[oppositeColor(ctx.attack_color)], pieces);
                exact_record.proof_pieces_set = ProofPiecesType::Unset;
                exact_record.setDisproofPieces(pieces);
            }
            else if (!result.isFinal()) {
                if (is_osl_normal_move(recorded_last_move)
                    && recorded_last_move != current_parent_move
                    && std::max(result.proof, result.disproof) >= kDagFindThreshold) {
                    find_dag_source(pos, ctx, exact_record, 1, "attack-final-self");
                }
                if (final_next_index < children.size()
                    && std::max(children[final_next_index].pdp.proof, children[final_next_index].pdp.disproof) >= kDagFindThreshold
                    && is_osl_normal_move(children[final_next_index].last_move)
                    && children[final_next_index].last_move != moves[final_next_index]) {
                    const Key child_board_index = children[final_next_index].board_index;
                    const uint64_t child_board_secondary = children[final_next_index].board_secondary;
                    const std::array<Hand, ColorNum> child_stands = children[final_next_index].stands;
                    DfpnRecord child_exact(child_board_secondary, child_stands);
                    child_exact.proof_disproof = children[final_next_index].pdp;
                    child_exact.best_move = children[final_next_index].best_reply;
                    child_exact.last_move = children[final_next_index].last_move;
                    child_exact.proof_pieces = children[final_next_index].proof_pieces;
                    child_exact.proof_pieces_set = children[final_next_index].proof_pieces_type;
                    child_exact.node_count = children[final_next_index].node_count;
                    child_exact.need_full_width = children[final_next_index].need_full_width;
                    copy_child_aux_to_record(child_exact, children[final_next_index]);
                    child_exact.exact = true;
                    find_dag_source(child_board_index, child_board_secondary, child_stands, oppositeColor(ctx.attack_color),
                        ctx, child_exact, 0, "attack-final-child");
                    child_exact.last_move = moves[final_next_index];
                    commit_exact_record(child_board_index, child_board_secondary, child_stands, child_exact, "attack-final-child-dag");
                    children[final_next_index].pdp = child_exact.proof_disproof;
                    children[final_next_index].best_reply = child_exact.best_move;
                    children[final_next_index].last_move = child_exact.last_move;
                    children[final_next_index].proof_pieces = child_exact.proof_pieces;
                    children[final_next_index].proof_pieces_type = child_exact.proof_pieces_set;
                    children[final_next_index].node_count = child_exact.node_count;
                    children[final_next_index].need_full_width = child_exact.need_full_width;
                    copy_record_aux_to_child(children[final_next_index], child_exact);
                }
            }
            set_parent_link(exact_record, ctx);
            accumulate_record_node_count(exact_record, node_count_org, ctx.node_count);
            exact_record.proof_disproof = result;
            exact_record.remaining_depth = remaining_depth(pos, ctx);
            commit_exact_record(pos, ctx.current_board_index(pos), current_secondary, exact_record, "attack-final");
            store_out_exact(exact_record);
            set_path_node_count(path_record, exact_record.node_count);
            return result;
        }

        result_best = moves[next_index];
        exact_record.best_move = result_best;

        const Threshold child_threshold{
            oslmate_attack_child_proof_threshold(
                threshold.proof,
                second_proof,
                proof_average,
                static_cast<uint32_t>(children[next_index].proof_cost)),
            oslmate_attack_child_disproof_threshold(
                threshold.disproof,
                sum_disproof,
                children[next_index].pdp.disproof),
        };
        const Key child_board_index = children[next_index].board_index;
        const uint64_t child_board_secondary = children[next_index].board_secondary;
        const std::array<Hand, ColorNum> child_stands = children[next_index].stands;
        StateInfo st;
        do_known_check_move(pos, moves[next_index], st);
        PathEncoding child_path_encoding = current_path;
        child_path_encoding.pushMove(moves[next_index]);
        ctx.path_encodings.push_back(child_path_encoding);
        ctx.board_index_history.push_back(child_board_index);
        ctx.board_secondary_history.push_back(child_board_secondary);
        ctx.stand_history.push_back(child_stands);
        bool pawn_drop_no_escape_child = false;
        try {
            DfpnRecord child_exact(child_board_secondary, child_stands);
            ctx.push_move(moves[next_index]);
            if ((false || false)
                && ctx.move_history.size() == 5
                && ctx.move_history[0].toUSI() == "S*5d"
                && ctx.move_history[1].toUSI() == "6e7e"
                && ctx.move_history[2].toUSI() == "7i8g"
                && ctx.move_history[3].toUSI() == "7e8f"
                && ctx.move_history[4].toUSI() == "5a9e+") {
                const DfpnRecord direct_probe = probe(pos, ctx.attack_color);
            }
            ProofDisproof child_result = ProofDisproof::Unknown();
            {
                child_result = defense(pos, ctx, child_threshold, &child_exact);
            }
            if ((false || false)
                && ctx.move_history.size() == 5
                && ctx.move_history[0].toUSI() == "S*5d"
                && ctx.move_history[1].toUSI() == "6e7e"
                && ctx.move_history[2].toUSI() == "7i8g"
                && ctx.move_history[3].toUSI() == "7e8f"
                && ctx.move_history[4].toUSI() == "5a9e+") {
                const DfpnRecord direct_probe_after = probe(pos, ctx.attack_color);
            }
            (void)child_result;
            pawn_drop_no_escape_child = is_pawn_drop_no_escape(moves[next_index], child_exact.proof_disproof);
            // OSL uses next.record from the recursive node directly, not the threshold return value.
            children[next_index].pdp = child_exact.proof_disproof;
            if (pawn_drop_no_escape_child) {
                children[next_index].pdp = ProofDisproof::PawnCheckmate();
            }
            children[next_index].best_reply = child_exact.best_move;
            children[next_index].last_move = child_exact.last_move;
            children[next_index].proof_pieces_type = child_exact.proof_pieces_set;
            children[next_index].proof_pieces = child_exact.proof_pieces;
            children[next_index].node_count = child_exact.node_count;
            children[next_index].need_full_width = child_exact.need_full_width;
            copy_record_aux_to_child(children[next_index], child_exact);
            children[next_index].path_record = ctx.returned_path_record;
            ctx.pop_move();
        }
        catch (const DepthLimitReached&) {
            if (!ctx.move_history.empty() && ctx.move_history.back() == moves[next_index]) {
                ctx.pop_move();
            }
            ctx.stand_history.pop_back();
            ctx.board_secondary_history.pop_back();
            ctx.board_index_history.pop_back();
            ctx.path_encodings.pop_back();
            pos.undoMove(moves[next_index]);
            throw;
        }
        if (children[next_index].pdp.isCheckmateSuccess()) {
            result = children[next_index].pdp;
            result_best = moves[next_index];
            ctx.stand_history.pop_back();
            ctx.board_secondary_history.pop_back();
            ctx.board_index_history.pop_back();
            ctx.path_encodings.pop_back();
            pos.undoMove(moves[next_index]);

            exact_record.proof_disproof = result;
            exact_record.best_move = result_best;
            exact_record.last_move = current_parent_move;
            set_parent_link(exact_record, ctx);
            accumulate_record_node_count(exact_record, node_count_org, ctx.node_count);
            exact_record.remaining_depth = remaining_depth(pos, ctx);
            exact_record.proof_pieces_set = ProofPiecesType::Unset;
            exact_record.setProofPieces(proof_pieces_after_attack(
                children[next_index].proof_pieces,
                complete_move_for_position(pos, moves[next_index]), exact_record.stands[ctx.attack_color]));
            commit_exact_record(pos, ctx.current_board_index(pos), current_secondary, exact_record, "attack-child-success");
            store_out_exact(exact_record);
            set_path_node_count(path_record, 0);
            if (best_move) {
                *best_move = result_best;
            }
            return result;
        }
        else if (children[next_index].pdp.isCheckmateFail()
            && !children[next_index].pdp.isLoopDetection()
            && !pawn_drop_no_escape_child) {
            set_no_checkmate_child_in_attack(exact_record, children[next_index], next_index);
        }
        ctx.stand_history.pop_back();
        ctx.board_secondary_history.pop_back();
        ctx.board_index_history.pop_back();
        ctx.path_encodings.pop_back();
        pos.undoMove(moves[next_index]);

        min_proof = std::min(second_proof, children[next_index].pdp.proof);
        if (min_proof < ProofDisproof::PROOF_LIMIT
            && saturate_sum(static_cast<uint64_t>(ctx.node_count) + min_proof, ctx.node_limit) >= ctx.node_limit) {
            result = ProofDisproof(min_proof, static_cast<uint32_t>(sum_disproof));
            exact_record.proof_disproof = result;
            set_parent_link(exact_record, ctx);
            accumulate_record_node_count(exact_record, node_count_org, ctx.node_count);
            exact_record.remaining_depth = remaining_depth(pos, ctx);
            commit_exact_record(pos, ctx.current_board_index(pos), current_secondary, exact_record, "attack-node-limit-after-child");
            store_out_exact(exact_record);
            set_path_node_count(path_record, exact_record.node_count);
            if (best_move) {
                *best_move = result_best;
            }
            return result;
        }

        continue;
    }

    if (result.isLoopDetection()) {
        exact_record.proof_disproof = ProofDisproof::Unknown();
        set_parent_link(exact_record, ctx);
        accumulate_record_node_count(exact_record, node_count_org, ctx.node_count);
        exact_record.remaining_depth = remaining_depth(pos, ctx);
        commit_exact_record(pos, ctx.current_board_index(pos), current_secondary, exact_record, "attack-loop");
        store_out_exact(exact_record);
        set_path_node_count(path_record, exact_record.node_count);
        if (best_move) {
            *best_move = exact_record.best_move;
        }
        return mark_loop(path_record, current_path);
    }

    exact_record.proof_disproof = result;
    set_parent_link(exact_record, ctx);
    accumulate_record_node_count(exact_record, node_count_org, ctx.node_count);
    exact_record.proof_disproof = result;
    exact_record.remaining_depth = remaining_depth(pos, ctx);
    if (result.isCheckmateFail()) {
        Hand pieces = exact_record.proof_pieces_candidate;
        add_monopolized_pieces(pos.hand(oppositeColor(ctx.attack_color)), pos.hand(ctx.attack_color), exact_record.stands[oppositeColor(ctx.attack_color)], pieces);
        exact_record.proof_pieces_set = ProofPiecesType::Unset;
        exact_record.setDisproofPieces(pieces);
    }
    commit_exact_record(pos, ctx.current_board_index(pos), current_secondary, exact_record, "attack-return");
    store_out_exact(exact_record);
    set_path_node_count(path_record, exact_record.node_count);
    if (best_move) {
        *best_move = result_best;
    }
    return result;
}

    ProofDisproof DfPn::Impl::defense(Position& pos, SearchContext& ctx, Threshold threshold, DfpnRecord* out_exact) {
    if (pos.gamePly() >= ctx.max_game_ply) {
        throw DepthLimitReached();
    }
    const Key key = pos.getKey();
    ctx.threshold_history.push_back(threshold);
    struct ThresholdHistoryPop {
        std::vector<Threshold>& threshold_history;
        ~ThresholdHistoryPop() {
            threshold_history.pop_back();
        }
    } threshold_history_pop{ ctx.threshold_history };

    const bool pushed_board_index = ctx.board_index_history.empty();
    if (pushed_board_index) {
        ctx.board_index_history.push_back(board_index_key(pos));
        ctx.board_secondary_history.push_back(secondary_board_key(pos));
        ctx.stand_history.push_back(stand_pair(pos));
    }
    struct BoardHistoryPop {
        std::vector<Key>& board_index_history;
        std::vector<uint64_t>& board_secondary_history;
        std::vector<std::array<Hand, ColorNum>>& stand_history;
        bool active;
        ~BoardHistoryPop() {
            if (active) {
                board_index_history.pop_back();
                board_secondary_history.pop_back();
                stand_history.pop_back();
            }
        }
    } board_history_pop{ ctx.board_index_history, ctx.board_secondary_history, ctx.stand_history, pushed_board_index };

    const PathEncoding current_path = ctx.path_encodings.empty() ? PathEncoding(ctx.attack_color) : ctx.path_encodings.back();
    const bool pushed_path = ctx.path_encodings.empty();
    if (pushed_path) {
        ctx.path_encodings.push_back(current_path);
    }
    struct PathEncodingPop {
        std::vector<PathEncoding>& path_encodings;
        bool active;
        ~PathEncodingPop() {
            if (active) {
                path_encodings.pop_back();
            }
        }
    } path_hash_pop{ ctx.path_encodings, pushed_path };

    const int my_distance = ctx.path_records.empty()
        ? static_cast<int>(ctx.move_history.size())
        : ctx.path_records.back()->distance + 1;
    DfpnPathTable::LoopToDominance loop = DfpnPathTable::LoopToDominance::NoLoop;
    PathRecord* path_record = path_state(ctx, pos, my_distance, loop);
    ReturnPathRecordScope return_path_record_scope{ ctx, path_record };
    VisitLock visit_lock(path_record);
    if (loop == DfpnPathTable::LoopToDominance::BadAttackLoop) {
        DfpnRecord loop_record;
        set_loop_detection_record(loop_record, path_record, current_path, out_exact);
        return loop_record.proof_disproof;
    }
    ctx.path_records.push_back(path_record);
    struct PathRecordStackPop {
        std::vector<PathRecord*>& path_records;
        ~PathRecordStackPop() { path_records.pop_back(); }
    } path_record_stack_pop{ ctx.path_records };

    const uint32_t node_count_org = ctx.node_count;
    ++ctx.node_count;
    if (!pos.inCheck()) {
        if (out_exact) {
            DfpnRecord current(pos, ctx.current_board_secondary(pos));
            current.proof_disproof = ProofDisproof::NoCheckmate();
            *out_exact = current;
        }
        return ProofDisproof::NoCheckmate();
    }

    const Key current_board_index = ctx.current_board_index(pos);
    const uint64_t current_secondary = ctx.current_board_secondary(pos);
    const std::array<Hand, ColorNum> current_stands = ctx.current_stands(pos);
    const DfpnRecord record = probe(current_board_index, current_secondary, current_stands, ctx.attack_color);
    const auto store_out_exact = [&](const DfpnRecord& value) {
        if (out_exact) {
            *out_exact = value;
        }
    };
    const auto current_exact_record = [&](const DfpnRecord* fallback = nullptr) {
        (void)fallback;
        DfpnRecord current = load_exact_record(current_board_index, current_secondary, current_stands);
        current.refresh_stands(current_secondary, current_stands);
        return current;
    };
    if (record.proof_disproof.isFinal())
    {
        store_out_exact(record);
        return record.proof_disproof;
    }
    DfpnRecord exact_record = record;
    exact_record.refresh_stands(current_secondary, current_stands);
    exact_record.exact = true;
    const bool entered_full_width = exact_record.need_full_width != 0;
    const Move current_parent_move = ctx.move_history.empty() ? Move::moveNone() : ctx.move_history.back();
    if (!exact_record.proof_disproof.isFinal()) {
        exact_record.proof_pieces_set = ProofPiecesType::Unset;
    }

    if (exact_record.last_to == SquareNum) {
        exact_record.last_to = grand_parent_simulation_suitable(ctx.move_history)
            ? ctx.move_history.back().to()
            : pos.kingSquare(pos.turn());
    }
    const Square delayed_to = exact_record.last_to != pos.kingSquare(pos.turn()) ? exact_record.last_to : SquareNum;
    std::vector<Move>& moves = ctx.moves_for_current_depth();
    generate_escape_moves(pos, moves, exact_record.need_full_width != 0, delayed_to,
        ctx.piece_numbers ? &*ctx.piece_numbers : nullptr);
    if (moves.empty() && exact_record.need_full_width == 0) {
        exact_record.need_full_width = 1;
        generate_escape_moves(pos, moves, true, delayed_to,
            ctx.piece_numbers ? &*ctx.piece_numbers : nullptr);
    }
    if (moves.empty()) {
        exact_record.proof_disproof = ProofDisproof::NoEscape();
        set_parent_link(exact_record, ctx);
        exact_record.remaining_depth = remaining_depth(pos, ctx);
        exact_record.proof_pieces_set = ProofPiecesType::Unset;
        exact_record.setProofPieces(proof_pieces_leaf(pos, ctx.attack_color, exact_record.stands[ctx.attack_color]));
        store_out_exact(exact_record);
        return ProofDisproof::NoEscape();
    }
    ActiveNodeScope active_node(ctx, exact_record, moves, pos);
    std::vector<ChildState>& children = ctx.children_for_current_depth();
    children.clear();
    children.resize(moves.size());
    active_node.set_children(children);
    uint32_t sum_frontier_disproof = 0;
    uint32_t frontier_count = 0;
        for (size_t i = 0; i < moves.size(); ++i) {
            const Move current_move = moves[i];
            const Move current_move_with_capture = current_move;
            children[i].reset_for_move(current_move);
            if (exact_record.solved & child_bit(i)) {
            continue;
        }
        Key child_board_index = 0;
        uint64_t child_board_secondary = 0;
        std::array<Hand, ColorNum> child_stands{ Hand(0), Hand(0) };
        {
            const OslBoardKeyParts child_keys = board_keys_after_move(
                current_board_index, current_secondary, pos.turn(), current_move_with_capture);
            child_board_index = child_keys.board_index;
            child_board_secondary = child_keys.board_secondary;
            child_stands = stand_pair_after_move(
                current_stands, pos.turn(), current_move_with_capture);
        }
        children[i].board_index = child_board_index;
        children[i].board_secondary = child_board_secondary;
        children[i].stands = child_stands;
        StateInfo st;
        bool child_position_made = false;
        const auto ensure_child_position = [&]() {
            if (!child_position_made) {
                pos.doMove(current_move, st);
                assert(child_board_secondary == secondary_board_key(pos));
                child_position_made = true;
            }
        };
        const auto undo_child_position = [&]() {
            if (child_position_made) {
                pos.undoMove(current_move);
                child_position_made = false;
            }
        };
        PathEncoding child_path_encoding = current_path;
        child_path_encoding.pushMove(current_move);
        {
            DfpnRecord child_record = table.probe(
                child_board_index, child_board_secondary, child_stands, ctx.attack_color);
            children[i].pdp = child_record.proof_disproof;
            children[i].best_reply = child_record.best_move;
            children[i].last_move = child_record.last_move;
            children[i].proof_pieces_type = child_record.proof_pieces_set;
            children[i].proof_pieces = child_record.proof_pieces;
            children[i].node_count = child_record.node_count;
            children[i].need_full_width = child_record.need_full_width;
            copy_record_aux_to_child(children[i], child_record);
            if (children[i].pdp.isCheckmateSuccess()) {
                set_checkmate_child_in_defense(exact_record, children[i], i);
            }
            if (children[i].pdp == ProofDisproof(1, 1)) {
                OslPieceNumberState::Undo fixed_piece_undo;
                const OslPieceNumberState* fixed_piece_numbers = nullptr;
                bool fixed_piece_number_applied = false;
                if (ctx.piece_numbers) {
                    fixed_piece_number_applied = ctx.piece_numbers->apply_move(current_move, &fixed_piece_undo);
                    if (fixed_piece_number_applied) {
                        fixed_piece_numbers = &*ctx.piece_numbers;
                    }
                }
                ensure_child_position();
                Move check_move = Move::moveNone();
                Hand proof_pieces = zero_hand();
                const ProofDisproof fixed_pdp = fixed_escape_by_move_zero(
                    pos, ctx.attack_color, &check_move, &proof_pieces, false, true, fixed_piece_numbers);
                if (fixed_piece_number_applied) {
                    ctx.piece_numbers->undo_move(fixed_piece_undo);
                }
                ++ctx.node_count;
                if (fixed_pdp.isCheckmateSuccess()) {
                    children[i].pdp = fixed_pdp;
                    children[i].best_reply = check_move;
                    children[i].proof_pieces_type = ProofPiecesType::Proof;
                    children[i].proof_pieces = proof_pieces;
                    ++children[i].node_count;
                    children[i].need_full_width = child_record.need_full_width;
                    set_checkmate_child_in_defense(exact_record, children[i], i);
                }
                else if (fixed_pdp.isCheckmateFail()) {
                    children[i].pdp = ProofDisproof(1, 1);
                    if (i != 0) {
                        moves[0] = moves[i];
                        children[0] = children[i];
                        moves.resize(1);
                        children.resize(1);
                    }
                    undo_child_position();
                    break;
                }
                else {
                    // OSL's CHECKMATE_D2 assigns hasEscapeByMove() to the child
                    // before checking finality, so non-final D2 estimates are kept.
                    children[i].pdp = fixed_pdp;
                    ++frontier_count;
                    sum_frontier_disproof += children[i].pdp.disproof;
                }
            }
            if (!children[i].pdp.isCheckmateFail()) {
                children[i].path_record = probe_child_path(ctx, child_board_index, child_board_secondary, child_stands[Black]);
                if (child_is_loop(children[i], current_path)) {
                    undo_child_position();
                    set_loop_detection_record(exact_record, path_record, current_path, out_exact);
                    return exact_record.proof_disproof;
                }
                if (kEnableGrandParentSimulation && children[i].pdp == ProofDisproof(1, 1)) {
                    undo_child_position();
                    grand_parent_simulation(pos, ctx, exact_record, moves, children, i);
                    ensure_child_position();
                    if (children[i].pdp.isCheckmateSuccess()) {
                        set_checkmate_child_in_defense(exact_record, children[i], i);
                    }
                }
            }
        }
        if (children[i].pdp.isCheckmateFail()) {
            exact_record.proof_disproof = children[i].pdp;
            exact_record.best_move = moves[i];
            set_parent_link(exact_record, ctx);
            exact_record.remaining_depth = remaining_depth(pos, ctx);
            exact_record.proof_pieces_set = ProofPiecesType::Unset;
            exact_record.setDisproofPieces(disproof_pieces_after_defense(
                children[i].proof_pieces,
                current_move_with_capture, exact_record.stands[oppositeColor(ctx.attack_color)]));
            undo_child_position();
            commit_exact_record(pos, ctx.current_board_index(pos), current_secondary, exact_record, "defense-init-child-fail");
            store_out_exact(exact_record);
            return exact_record.proof_disproof;
        }
        if (!children[i].pdp.isFinal()
            && is_osl_normal_move(children[i].last_move)
            && children[i].last_move != moves[i]
            && std::max(children[i].pdp.proof, children[i].pdp.disproof) >= kDagFindThreshold2) {
            DfpnRecord child_exact_mut(child_board_secondary, child_stands);
            child_exact_mut.proof_disproof = children[i].pdp;
            child_exact_mut.best_move = children[i].best_reply;
            child_exact_mut.last_move = children[i].last_move;
            child_exact_mut.proof_pieces = children[i].proof_pieces;
            child_exact_mut.proof_pieces_set = children[i].proof_pieces_type;
            child_exact_mut.node_count = children[i].node_count;
            child_exact_mut.need_full_width = children[i].need_full_width;
            copy_child_aux_to_record(child_exact_mut, children[i]);
            child_exact_mut.exact = true;
            find_dag_source(child_board_index, child_board_secondary, child_stands, ctx.attack_color,
                ctx, child_exact_mut, 0, "defense-init-child");
            children[i].pdp = child_exact_mut.proof_disproof;
            children[i].best_reply = child_exact_mut.best_move;
            children[i].last_move = child_exact_mut.last_move;
            children[i].proof_pieces = child_exact_mut.proof_pieces;
            children[i].proof_pieces_type = child_exact_mut.proof_pieces_set;
            children[i].node_count = child_exact_mut.node_count;
            children[i].need_full_width = child_exact_mut.need_full_width;
            copy_record_aux_to_child(children[i], child_exact_mut);
        }
        undo_child_position();
    }
    if (kEnableBlockingSimulation && exact_record.need_full_width == 1) {
        ++exact_record.need_full_width;
        for (size_t i = 0; i < children.size(); ++i) {
            const bool solved_in_record = (exact_record.solved & child_bit(i)) != 0;
            const bool solved_by_child = i >= 64 && children[i].pdp.isCheckmateSuccess();
            if ((solved_in_record || solved_by_child) && moves[i].isDrop()) {
                blocking_simulation(pos, ctx, threshold.proof, exact_record, moves, children, i);
            }
        }
    }
    const Move recorded_last_move = current_parent_move;
    exact_record.last_move = current_parent_move;
    // OSL defines DISPROOF_AVERAGE in dfpn.cc, so the widening adjustment uses
    // the average disproof of frontier children.
    const uint32_t disproof_average = frontier_count ? sum_frontier_disproof / frontier_count : 1;
    ProofDisproof result = exact_record.proof_disproof;
    Move result_best = exact_record.best_move;
    size_t final_next_index = moves.size();
    std::array<char, kMoveBufferSize> target{};
    for (int loop_iteration = 0;; ++loop_iteration) {
        std::fill(target.begin(), target.begin() + static_cast<std::ptrdiff_t>(children.size()), 0);
        uint32_t min_disproof = exact_record.min_pdp;
        uint32_t second_disproof = exact_record.min_pdp;
        uint64_t sum_proof64 = 0;
        uint64_t max_proof = 0;
        uint64_t max_drop_proof = 0;
        uint64_t max_proof_dag = 0;
        int max_children_depth = 0;
        bool false_branch_candidate = !exact_record.false_branch;
        size_t next_index = moves.size();

        for (size_t i = 0; i < children.size(); ++i) {
            if (exact_record.solved & child_bit(i)) {
                continue;
            }
            if (i > 0
                && min_disproof < ProofDisproof::DISPROOF_LIMIT
                && moves[i].fromAndTo() == moves[i - 1].fromAndTo()
                && !moves[i].isDrop()) {
                continue;
            }
            const ProofDisproof child = children[i].pdp;
            const uint32_t child_proof = child.proof;
            const uint32_t child_disproof = child.disproof;
            if (child.isCheckmateFail()) {
                result = child;
                exact_record.proof_disproof = result;
                exact_record.best_move = moves[i];
                set_parent_link(exact_record, ctx);
                exact_record.remaining_depth = remaining_depth(pos, ctx);
                exact_record.proof_pieces_set = ProofPiecesType::Unset;
                exact_record.setDisproofPieces(disproof_pieces_after_defense(
                    children[i].proof_pieces,
                    complete_move_for_position(pos, moves[i]), exact_record.stands[oppositeColor(ctx.attack_color)]));
                commit_exact_record(pos, ctx.current_board_index(pos), current_secondary, exact_record, "defense-child-fail");
                store_out_exact(exact_record);
                return result;
            }
            if (child_is_loop(children[i], current_path)) {
                set_loop_detection_record(exact_record, path_record, current_path, out_exact);
                return exact_record.proof_disproof;
            }
            if (children[i].path_record && !child.isFinal()) {
                max_children_depth = std::max(max_children_depth, children[i].path_record->distance);
            }
            else if (!children[i].path_record && path_record) {
                max_children_depth = path_record->distance + 1;
            }

            uint64_t proof = child_proof;
            if (children[i].path_record && !child.isFinal()) {
                if (children[i].path_record->distance <= path_record->distance
                    && (exact_record.need_full_width == 0 || min_disproof < ProofDisproof::DISPROOF_LIMIT)
                    && child_proof >= threshold.proof
                    && threshold.proof > kIgnoreUpwardProofThreshold) {
                    false_branch_candidate = false;
                    continue;
                }
                else if (kEnableNagaiDagTest && (exact_record.dag_moves & child_bit(i)) != 0) {
                    max_proof_dag = std::max(max_proof_dag, proof);
                    proof = 0;
                }
                else if (is_unattacked_drop_escape(pos, ctx.attack_color, moves[i])) {
                    max_drop_proof = std::max(max_drop_proof, proof);
                    proof = kSacrificeBlockCount;
                }
            }
            target[i] = 1;
            const PieceType raw_escape_ptype = osl_move_ptype(pos, moves[i]);
            const bool false_branch_before = false_branch_candidate;
            const bool false_branch_reject = false_branch_candidate && !child.isFinal()
                && (children[i].node_count == 0
                    || !is_osl_normal_move(children[i].best_reply)
                    || !(raw_escape_ptype == King && !moves[i].isCapture()));
            if (false_branch_reject) {
                false_branch_candidate = false;
            }

            if (child_disproof < min_disproof
                || (child_disproof == min_disproof
                    && proof != 0
                    && (next_index == moves.size() || proof < children[next_index].pdp.proof))) {
                second_disproof = min_disproof;
                min_disproof = child_disproof;
                next_index = i;
            }
            else if (child_disproof < second_disproof) {
                second_disproof = child_disproof;
            }
            max_proof = std::max(max_proof, proof);
            sum_proof64 += proof;
        }

        uint64_t sum_proof = sum_proof64;
        if (false_branch_candidate) {
            exact_record.false_branch = true;
            std::optional<OslmatePositionKey> goal;
            for (size_t i = 0; i < children.size(); ++i) {
                if (!target[i]) {
                    continue;
                }

                const Move best_reply = children[i].best_reply;
                const Key child_board_index = children[i].board_index;
                const uint64_t child_board_secondary = children[i].board_secondary;
                const std::array<Hand, ColorNum> child_stands = children[i].stands;
                const OslmatePositionKey child_goal{
                    static_cast<Key>(board_index_key_after_move(child_board_index, ctx.attack_color, best_reply)),
                    secondary_board_key_after_move(child_board_secondary, ctx.attack_color, best_reply),
                    stand_pair_after_move(child_stands, ctx.attack_color, best_reply)[Black]
                };

                if (!goal) {
                    goal = child_goal;
                }
                else if (*goal != child_goal) {
                    exact_record.false_branch = false;
                    break;
                }
            }
        }
        if (exact_record.false_branch) {
            sum_proof = max_proof;
        }
        sum_proof += max_drop_proof + max_proof_dag;
        if (kSacrificeBlockCount != 0 && max_drop_proof != 0) {
            sum_proof -= kSacrificeBlockCount;
        }

        if (path_record && path_record->distance >= max_children_depth) {
            path_record->distance = max_children_depth - 1;
        }

        if (min_disproof >= ProofDisproof::DISPROOF_MAX) {
            exact_record.need_full_width = 1;
            exact_record.proof_disproof = ProofDisproof(1, 1);
            commit_exact_record(pos, ctx.current_board_index(pos), current_secondary, exact_record,
                "defense-full-width-reset");
            store_out_exact(exact_record);
            return exact_record.proof_disproof;
        }

        if (kEnableKishimotoWidenThreshold
            && loop_iteration == 0
            && sum_proof >= threshold.proof
            && sum_proof > kIgnoreUpwardProofThreshold) {
            threshold.proof = static_cast<uint32_t>(sum_proof + 1);
            if (!ctx.active_nodes.empty()) {
                ctx.active_nodes.back().threshold = threshold;
            }
        }
        if (sum_proof < kRootProofTolerance
            && min_disproof > 0
            && sum_proof > static_cast<uint64_t>(min_disproof) * kAdHocSumScale) {
            const uint64_t scaled_min = static_cast<uint64_t>(min_disproof) * kAdHocSumScale;
            sum_proof = scaled_min + slow_increase(static_cast<uint32_t>(sum_proof - scaled_min));
        }

        result = { static_cast<uint32_t>(sum_proof), min_disproof };
        const bool node_limit_reached = saturate_sum(
            static_cast<uint64_t>(ctx.node_count) + sum_proof,
            ctx.node_limit) >= ctx.node_limit;
        if (next_index == moves.size()
            || sum_proof >= threshold.proof
            || min_disproof >= threshold.disproof
            || node_limit_reached)
        {
            final_next_index = next_index;
            exact_record.proof_disproof = result;
            exact_record.proof_disproof = result;
            if (result.isLoopDetection()) {
                exact_record.proof_disproof = ProofDisproof::Unknown();
                set_parent_link(exact_record, ctx);
                accumulate_record_node_count(exact_record, node_count_org, ctx.node_count);
                exact_record.remaining_depth = remaining_depth(pos, ctx);
                commit_exact_record(pos, ctx.current_board_index(pos), current_secondary, exact_record, "defense-final-loop");
                store_out_exact(exact_record);
                set_path_node_count(path_record, exact_record.node_count);
                return mark_loop(path_record, current_path);
            }
            if (result.isCheckmateSuccess()) {
                if (kEnableBlockingVerify && exact_record.need_full_width == 0) {
                    exact_record.need_full_width = 1;
                    exact_record.proof_disproof = ProofDisproof(1, 1);
                    set_parent_link(exact_record, ctx);
                    exact_record.remaining_depth = remaining_depth(pos, ctx);
                    commit_exact_record(pos, ctx.current_board_index(pos), current_secondary, exact_record,
                        "defense-final-blocking-verify");
                    store_out_exact(exact_record);
                    return exact_record.proof_disproof;
                }
                Hand pieces = exact_record.proof_pieces_candidate;
                if (!is_unblockable_check(pos)) {
                    add_monopolized_pieces(pos.hand(ctx.attack_color), pos.hand(oppositeColor(ctx.attack_color)),
                        exact_record.stands[ctx.attack_color], pieces);
                }
                exact_record.proof_pieces_set = ProofPiecesType::Unset;
                exact_record.setProofPieces(pieces);
                exact_record.proof_disproof = ProofDisproof::Checkmate();
                result = exact_record.proof_disproof;
            }
            else if (!result.isFinal()) {
                if (is_osl_normal_move(recorded_last_move)
                    && recorded_last_move != current_parent_move
                    && std::max(result.proof, result.disproof) >= kDagFindThreshold) {
                    find_dag_source(pos, ctx, exact_record, 1, "defense-final-self");
                }
                if (final_next_index < children.size()
                    && std::max(children[final_next_index].pdp.proof, children[final_next_index].pdp.disproof) >= kDagFindThreshold
                    && is_osl_normal_move(children[final_next_index].last_move)
                    && children[final_next_index].last_move != moves[final_next_index]) {
                    const Key child_board_index = children[final_next_index].board_index;
                    const uint64_t child_board_secondary = children[final_next_index].board_secondary;
                    const std::array<Hand, ColorNum> child_stands = children[final_next_index].stands;
                    DfpnRecord child_exact(child_board_secondary, child_stands);
                    child_exact.proof_disproof = children[final_next_index].pdp;
                    child_exact.best_move = children[final_next_index].best_reply;
                    child_exact.last_move = children[final_next_index].last_move;
                    child_exact.proof_pieces = children[final_next_index].proof_pieces;
                    child_exact.proof_pieces_set = children[final_next_index].proof_pieces_type;
                    child_exact.node_count = children[final_next_index].node_count;
                    child_exact.need_full_width = children[final_next_index].need_full_width;
                    copy_child_aux_to_record(child_exact, children[final_next_index]);
                    child_exact.exact = true;
                    find_dag_source(child_board_index, child_board_secondary, child_stands, ctx.attack_color,
                        ctx, child_exact, 0, "defense-final-child");
                    child_exact.last_move = moves[final_next_index];
                    commit_exact_record(child_board_index, child_board_secondary, child_stands, child_exact, "defense-final-child-dag");
                    children[final_next_index].pdp = child_exact.proof_disproof;
                    children[final_next_index].best_reply = child_exact.best_move;
                    children[final_next_index].last_move = child_exact.last_move;
                    children[final_next_index].proof_pieces = child_exact.proof_pieces;
                    children[final_next_index].proof_pieces_type = child_exact.proof_pieces_set;
                    children[final_next_index].node_count = child_exact.node_count;
                    children[final_next_index].need_full_width = child_exact.need_full_width;
                    copy_record_aux_to_child(children[final_next_index], child_exact);
                }
            }
            set_parent_link(exact_record, ctx);
            accumulate_record_node_count(exact_record, node_count_org, ctx.node_count);
            exact_record.proof_disproof = result;
            exact_record.remaining_depth = remaining_depth(pos, ctx);
            commit_exact_record(pos, ctx.current_board_index(pos), current_secondary, exact_record, "defense-final");
            store_out_exact(exact_record);
            set_path_node_count(path_record, exact_record.node_count);
            return result;
        }

        result_best = moves[next_index];
        exact_record.best_move = result_best;

        const Threshold child_threshold{
            oslmate_defense_child_proof_threshold(
                threshold.proof,
                sum_proof,
                children[next_index].pdp.proof),
            static_cast<uint32_t>(std::min<uint64_t>(
                threshold.disproof,
                static_cast<uint64_t>(second_disproof) + disproof_average)),
        };
        const Key child_board_index = children[next_index].board_index;
        const uint64_t child_board_secondary = children[next_index].board_secondary;
        const std::array<Hand, ColorNum> child_stands = children[next_index].stands;
        StateInfo st;
        pos.doMove(moves[next_index], st);
        PathEncoding child_path_encoding = current_path;
        child_path_encoding.pushMove(moves[next_index]);
        ctx.path_encodings.push_back(child_path_encoding);
        ctx.board_index_history.push_back(child_board_index);
        ctx.board_secondary_history.push_back(child_board_secondary);
        ctx.stand_history.push_back(child_stands);
        bool pawn_drop_no_escape_child = false;
        try {
            DfpnRecord child_exact(child_board_secondary, child_stands);
            ctx.push_move(moves[next_index]);
            ProofDisproof child_result = ProofDisproof::Unknown();
            {
                child_result = attack(pos, ctx, child_threshold, nullptr, &child_exact);
            }
            (void)child_result;
            // OSL uses next.record from the recursive node directly, not the threshold return value.
            children[next_index].pdp = child_exact.proof_disproof;
            children[next_index].best_reply = child_exact.best_move;
            children[next_index].last_move = child_exact.last_move;
            children[next_index].proof_pieces_type = child_exact.proof_pieces_set;
            children[next_index].proof_pieces = child_exact.proof_pieces;
            children[next_index].node_count = child_exact.node_count;
            children[next_index].need_full_width = child_exact.need_full_width;
            copy_record_aux_to_child(children[next_index], child_exact);
            children[next_index].path_record = ctx.returned_path_record;
                ctx.pop_move();
        }
        catch (const DepthLimitReached&) {
            if (!ctx.move_history.empty() && ctx.move_history.back() == moves[next_index]) {
                ctx.pop_move();
            }
            ctx.stand_history.pop_back();
            ctx.board_secondary_history.pop_back();
            ctx.board_index_history.pop_back();
            ctx.path_encodings.pop_back();
            pos.undoMove(moves[next_index]);
            throw;
        }
        ctx.stand_history.pop_back();
        ctx.board_secondary_history.pop_back();
        ctx.board_index_history.pop_back();
        ctx.path_encodings.pop_back();
        pos.undoMove(moves[next_index]);
        if (children[next_index].pdp.isCheckmateFail()) {
            if (exact_record.proof_disproof.isLoopDetection()) {
                exact_record.proof_disproof = ProofDisproof::Unknown();
                set_parent_link(exact_record, ctx);
                accumulate_record_node_count(exact_record, node_count_org, ctx.node_count);
                exact_record.remaining_depth = remaining_depth(pos, ctx);
                commit_exact_record(pos, ctx.current_board_index(pos), current_secondary, exact_record, "defense-child-parent-loop-update");
                store_out_exact(exact_record);
                set_path_node_count(path_record, exact_record.node_count);
                return mark_loop(path_record, current_path);
            }
            result = children[next_index].pdp;
            exact_record.proof_disproof = result;
            exact_record.best_move = moves[next_index];
            set_parent_link(exact_record, ctx);
            accumulate_record_node_count(exact_record, node_count_org, ctx.node_count);
            exact_record.remaining_depth = remaining_depth(pos, ctx);
            exact_record.proof_pieces_set = ProofPiecesType::Unset;
            exact_record.setDisproofPieces(disproof_pieces_after_defense(
                children[next_index].proof_pieces,
                complete_move_for_position(pos, moves[next_index]), exact_record.stands[oppositeColor(ctx.attack_color)]));
            commit_exact_record(pos, ctx.current_board_index(pos), current_secondary, exact_record, "defense-child-fail-update");
            store_out_exact(exact_record);
            set_path_node_count(path_record, exact_record.node_count);
            return result;
        }
        if (children[next_index].pdp.isCheckmateSuccess()) {
            set_checkmate_child_in_defense(exact_record, children[next_index], next_index);
        }
        if (ctx.node_count >= ctx.node_limit) {
            result = ProofDisproof(static_cast<uint32_t>(sum_proof), min_disproof);
            exact_record.proof_disproof = result;
            set_parent_link(exact_record, ctx);
            accumulate_record_node_count(exact_record, node_count_org, ctx.node_count);
            exact_record.remaining_depth = remaining_depth(pos, ctx);
            commit_exact_record(pos, ctx.current_board_index(pos), current_secondary, exact_record, "defense-node-limit-after-child");
            store_out_exact(exact_record);
            set_path_node_count(path_record, exact_record.node_count);
            return result;
        }
        if (moves[next_index].isDrop() && children[next_index].pdp.isCheckmateSuccess()) {
            blocking_simulation(pos, ctx, threshold.proof, exact_record, moves, children, next_index);
        }

        continue;
    }

    exact_record.proof_disproof = result;
    if (result.isLoopDetection()) {
        exact_record.proof_disproof = ProofDisproof::Unknown();
        set_parent_link(exact_record, ctx);
        accumulate_record_node_count(exact_record, node_count_org, ctx.node_count);
        exact_record.remaining_depth = remaining_depth(pos, ctx);
        commit_exact_record(pos, ctx.current_board_index(pos), current_secondary, exact_record, "defense-loop");
        store_out_exact(exact_record);
        set_path_node_count(path_record, exact_record.node_count);
        return mark_loop(path_record, current_path);
    }
    set_parent_link(exact_record, ctx);
    accumulate_record_node_count(exact_record, node_count_org, ctx.node_count);
    exact_record.proof_disproof = result;
    exact_record.remaining_depth = remaining_depth(pos, ctx);
    if (result.isCheckmateSuccess()) {
        Hand pieces = exact_record.proof_pieces_candidate;
        if (!is_unblockable_check(pos)) {
            add_monopolized_pieces(pos.hand(ctx.attack_color), pos.hand(oppositeColor(ctx.attack_color)), exact_record.stands[ctx.attack_color], pieces);
        }
        exact_record.proof_pieces_set = ProofPiecesType::Unset;
        exact_record.setProofPieces(pieces);
        exact_record.proof_disproof = ProofDisproof::Checkmate();
        result = exact_record.proof_disproof;
    }
    commit_exact_record(pos, ctx.current_board_index(pos), current_secondary, exact_record, "defense-return");
    store_out_exact(exact_record);
    set_path_node_count(path_record, exact_record.node_count);
    return result;
}

ProofDisproof DfPn::Impl::try_proof(Position& pos, SearchContext& ctx, const uint32_t oracle_id, Move* best_move) {
    const DfpnRecord entry = probe(pos, ctx.current_board_index(pos), ctx.current_board_secondary(pos), ctx.attack_color);
    if (entry.proof_disproof.isFinal() || entry.tried_oracle > oracle_id) {
        if (best_move) {
            *best_move = entry.best_move;
        }
        return entry.proof_disproof;
    }

    const OracleState oracle(pos);
    ProofDisproof result = ProofDisproof::Unknown();
    DfpnRecord root_record = entry;
    bool completed = false;
    const PathEncoding root_path = ctx.path_encodings.empty()
        ? PathEncoding(ctx.attack_color)
        : ctx.path_encodings.back();
    try {
        result = proof_oracle_attack(pos, oracle, ctx, kProofSimulationTolerance, best_move, &root_record, true);
        completed = true;
    }
    catch (const DepthLimitReached&) {
        result = ProofDisproof::Unknown();
    }

    if (completed) {
        if (ctx.returned_path_record && ctx.returned_path_record->hasTwin(root_path)) {
            return ProofDisproof::LoopDetection();
        }
        root_record.last_move = ctx.move_history.empty() ? Move::moveNone() : ctx.move_history.back();
        commit_oracle_record(pos, ctx.current_board_index(pos), ctx.current_board_secondary(pos), root_record);
    }
    if (best_move) {
        *best_move = root_record.best_move;
    }
    root_record.tried_oracle = oracle_id + 1;
    return result;
}

ProofDisproof DfPn::Impl::proof_oracle_attack(Position& pos, const OracleState& oracle, SearchContext& ctx,
    int proof_limit, Move* best_move, DfpnRecord* out_record, const bool use_table) {
    if (best_move) {
        *best_move = Move::moveNone();
    }
    const auto store_out_record = [&](const DfpnRecord& value) {
        if (out_record) {
            *out_record = value;
        }
    };
    if (pos.gamePly() >= ctx.max_game_ply || osl_tree_depth_limit_reached(ctx)) {
        throw DepthLimitReached();
    }

    const Key key = pos.getKey();
    const bool pushed_board_index = ctx.board_index_history.empty();
    if (pushed_board_index) {
        ctx.board_index_history.push_back(board_index_key(pos));
        ctx.board_secondary_history.push_back(secondary_board_key(pos));
        ctx.stand_history.push_back(stand_pair(pos));
    }
    struct BoardHistoryPop {
        std::vector<Key>& board_index_history;
        std::vector<uint64_t>& board_secondary_history;
        std::vector<std::array<Hand, ColorNum>>& stand_history;
        bool active;
        ~BoardHistoryPop() {
            if (active) {
                board_index_history.pop_back();
                board_secondary_history.pop_back();
                stand_history.pop_back();
            }
        }
    } board_history_pop{ ctx.board_index_history, ctx.board_secondary_history, ctx.stand_history, pushed_board_index };

    const PathEncoding current_path = ctx.path_encodings.empty() ? PathEncoding(ctx.attack_color) : ctx.path_encodings.back();
    const bool pushed_path = ctx.path_encodings.empty();
    if (pushed_path) {
        ctx.path_encodings.push_back(current_path);
    }
    struct PathEncodingPop {
        std::vector<PathEncoding>& path_encodings;
        bool active;
        ~PathEncodingPop() {
            if (active) {
                path_encodings.pop_back();
            }
        }
    } path_hash_pop{ ctx.path_encodings, pushed_path };

    const int my_distance = (use_table && !ctx.path_records.empty())
        ? ctx.path_records.back()->distance + 1
        : 0;
    DfpnPathTable::LoopToDominance loop = DfpnPathTable::LoopToDominance::NoLoop;
    PathRecord* path_record = use_table ? path_state(ctx, pos, my_distance, loop) : nullptr;
    ReturnPathRecordScope return_path_record_scope{ ctx, path_record };
    VisitLock visit_lock(path_record);
    if (use_table && loop == DfpnPathTable::LoopToDominance::BadAttackLoop) {
        DfpnRecord loop_record;
        set_loop_detection_record(loop_record, path_record, current_path, nullptr);
        store_out_record(loop_record);
        return loop_record.proof_disproof;
    }
    if (use_table) {
        ctx.path_records.push_back(path_record);
    }
    struct PathRecordStackPop {
        std::vector<PathRecord*>& path_records;
        bool active;
        ~PathRecordStackPop() {
            if (active) {
                path_records.pop_back();
            }
        }
    } path_record_stack_pop{ ctx.path_records, use_table };

    const uint32_t node_count_org = ctx.node_count;
    ++ctx.node_count;
    if (ctx.use_oracle_path_table && ctx.node_count > kOracleNodeLimit) {
        throw DepthLimitReached();
    }

    const Key oracle_current_board_index = ctx.current_board_index(pos);
    const uint64_t oracle_current_secondary = ctx.current_board_secondary(pos);
    const DfpnRecord record = probe(pos, oracle_current_board_index, oracle_current_secondary, ctx.attack_color);
    if (record.proof_disproof.isFinal()) {
        store_out_record(record);
        if (best_move) {
            *best_move = record.best_move;
        }
        return record.proof_disproof;
    }

    if (kEnableProofOracleAttackFixedDepthShortcut && record.node_count == 0) {
        Move fixed_best = Move::moveNone();
        Hand fixed_proof_pieces = zero_hand();
        const bool fixed_probe_enabled = false
            || (false
                && ctx.move_history.size() >= 8
                && ctx.move_history[0].toUSI() == "N*7g"
                && ctx.move_history[1].toUSI() == "8e8f"
                && ctx.move_history[2].toUSI() == "G*9g"
                && ctx.move_history[3].toUSI() == "8f7f"
                && ctx.move_history[4].toUSI() == "1e1f"
                && ctx.move_history[5].toUSI() == "P*3f"
                && ctx.move_history[6].toUSI() == "1f3f");
        const ProofDisproof fixed_pdp = fixed_attack_osl_shortcut(pos, ctx.attack_color, &fixed_best, &fixed_proof_pieces,
            ctx.piece_numbers ? &*ctx.piece_numbers : nullptr);
        ++ctx.node_count;
        const bool fixed_success = fixed_pdp.isCheckmateSuccess();
        if (fixed_success) {
            DfpnRecord fixed_record = record;
            fixed_record.refresh_stands(pos, oracle_current_secondary);
            fixed_record.proof_disproof = fixed_pdp;
            fixed_record.best_move = fixed_best;
            fixed_record.node_count = saturate_sum(static_cast<uint64_t>(record.node_count) + 1, UINT32_MAX);
            fixed_record.proof_pieces_set = ProofPiecesType::Unset;
            fixed_record.setProofPieces(fixed_proof_pieces);
            store_out_record(fixed_record);
            if (best_move) {
                *best_move = fixed_best;
            }
            return fixed_record.proof_disproof;
        }
    }

    const Move current_parent_move = ctx.move_history.empty() ? Move::moveNone() : ctx.move_history.back();
    DfpnRecord oracle_record;
    {
        oracle_record = find_proof_oracle(oracle, ctx.attack_color, current_parent_move);
    }
    if (!oracle_record.proof_disproof.isCheckmateSuccess() || !is_osl_normal_move(oracle_record.best_move)) {
        store_out_record(record);
        return ProofDisproof::Unknown();
    }

    Move check_move = Move::moveNone();
    {
        check_move = adjust_oracle_attack_move(pos, oracle_record.best_move,
            ctx.piece_numbers ? &*ctx.piece_numbers : nullptr);
    }
    if (!check_move) {
        store_out_record(record);
        return ProofDisproof::Unknown();
    }
    const Color mover = pos.turn();
    if (!is_osl_normal_move(check_move) || !oracle_traceable(oracle, mover, check_move)) {
        store_out_record(record);
        return ProofDisproof::Unknown();
    }

    DfpnRecord oracle_node_record = record;
    std::vector<Move>& oracle_moves = ctx.moves_for_current_depth();
    oracle_moves.clear();
    oracle_moves.push_back(check_move);
    std::vector<ChildState>& oracle_children = ctx.children_for_current_depth();
    oracle_children.clear();
    oracle_children.resize(1);
    ActiveNodeScope oracle_active_node(ctx, oracle_node_record, oracle_moves, pos);
    oracle_active_node.set_children(oracle_children);
    const auto set_oracle_child_from_record = [&](const DfpnRecord& child_record, const PathRecord* child_path_record) {
        ChildState& child_state = oracle_children[0];
        child_state.reset_for_move(check_move);
        child_state.pdp = child_record.proof_disproof;
        child_state.best_reply = child_record.best_move;
        child_state.last_move = child_record.last_move;
        child_state.proof_pieces = child_record.proof_pieces;
        child_state.proof_pieces_type = child_record.proof_pieces_set;
        child_state.node_count = child_record.node_count;
        child_state.need_full_width = child_record.need_full_width;
        child_state.path_record = child_path_record;
        copy_record_aux_to_child(child_state, child_record);
    };

    Key child_board_index = 0;
    uint64_t child_board_secondary = 0;
    std::array<Hand, ColorNum> child_stands{ Hand(0), Hand(0) };
    {
        child_board_index = ctx.child_board_index(pos, check_move);
        child_board_secondary = ctx.child_board_secondary(pos, check_move);
        child_stands = ctx.child_stands(pos, check_move);
    }
    oracle_children[0].board_index = child_board_index;
    oracle_children[0].board_secondary = child_board_secondary;
    oracle_children[0].stands = child_stands;
    StateInfo current_state;
    do_known_check_move(pos, check_move, current_state);
    const OracleState child_oracle = oracle_state_after_move(oracle, mover, check_move);

    DfpnRecord child_exact;
    ProofDisproof child = ProofDisproof::Unknown();
    const PathRecord* child_path = nullptr;
    {
        child_exact = use_table
            ? probe(child_board_index, child_board_secondary, child_stands, ctx.attack_color)
            : DfpnRecord();
        child_path = use_table ? probe_child_path(ctx, child_board_index, child_board_secondary, child_stands[Black]) : nullptr;
    }
    set_oracle_child_from_record(child_exact, child_path);
    if (child_is_loop(oracle_children[0], current_path)) {
        pos.undoMove(check_move);
        store_out_record(record);
        return ProofDisproof::Unknown();
    }

    bool recursed_child = false;
    if (child_exact.proof_disproof.isFinal()) {
        child = child_exact.proof_disproof;
    }
    else {
        recursed_child = true;
        ctx.push_move(check_move);
        PathEncoding child_path_encoding = current_path;
        child_path_encoding.pushMove(check_move);
        ctx.path_encodings.push_back(child_path_encoding);
        ctx.board_index_history.push_back(child_board_index);
        ctx.board_secondary_history.push_back(child_board_secondary);
        ctx.stand_history.push_back(child_stands);

        try {
            DfpnRecord defense_record;
            {
                child = proof_oracle_defense(pos, child_oracle, ctx, proof_limit, &defense_record, use_table);
            }
            // OSL assigns the recursive node record to node.children[0].
            // The threshold return value is not used as a child record.
            child_exact = defense_record;
            set_oracle_child_from_record(child_exact, ctx.returned_path_record);
            child = child_exact.proof_disproof;
            if (best_move && child.isCheckmateSuccess()) {
                *best_move = check_move;
            }
        }
        catch (...) {
            ctx.stand_history.pop_back();
            ctx.board_secondary_history.pop_back();
            ctx.board_index_history.pop_back();
            ctx.path_encodings.pop_back();
            ctx.pop_move();
            pos.undoMove(check_move);
            throw;
        }

        ctx.stand_history.pop_back();
        ctx.board_secondary_history.pop_back();
        ctx.board_index_history.pop_back();
        ctx.path_encodings.pop_back();
        ctx.pop_move();
    }

    pos.undoMove(check_move);

    const auto return_oracle_attack_non_success = [&]() {
        DfpnRecord non_success_record = oracle_node_record;
        if (use_table
            && is_osl_normal_move(non_success_record.last_move)
            && non_success_record.last_move != current_parent_move
            && std::max(non_success_record.proof(), non_success_record.disproof()) >= 128) {
            find_dag_source(pos, ctx, non_success_record, 1, "oracle-attack-self");
        }
        if (use_table) {
            non_success_record.last_move = current_parent_move;
        }
        store_out_record(non_success_record);
        return ProofDisproof::Unknown();
    };

    if (!child.isCheckmateSuccess()) {
        return return_oracle_attack_non_success();
    }
    if (recursed_child) {
        normalize_pawn_drop_no_escape(check_move, child_exact.proof_disproof);
        child = child_exact.proof_disproof;
        set_oracle_child_from_record(child_exact, oracle_children[0].path_record);
    }
    if (!child.isCheckmateSuccess()) {
        return return_oracle_attack_non_success();
    }

    if (false && !ctx.move_history.empty()) {
        const std::string root_move = ctx.move_history.front().toUSI();
    }
    DfpnRecord exact_record = oracle_node_record;
    exact_record.refresh_stands(pos, oracle_current_secondary);
    exact_record.exact = true;
    exact_record.proof_disproof = child_exact.proof_disproof;
    exact_record.best_move = check_move;
    if (use_table || ctx.node_count - node_count_org > 32) {
        exact_record.last_move = ctx.move_history.empty() ? Move::moveNone() : ctx.move_history.back();
    }
    set_parent_link(exact_record, ctx);
    accumulate_record_node_count(exact_record, node_count_org, ctx.node_count);
    exact_record.remaining_depth = remaining_depth(pos, ctx);
    exact_record.proof_pieces_set = ProofPiecesType::Unset;
    exact_record.setProofPieces(proof_pieces_after_attack(
        child_exact.proof_pieces,
        complete_move_for_position(pos, check_move), exact_record.stands[ctx.attack_color]));
    if (use_table || ctx.node_count - node_count_org > 32) {
        commit_oracle_record(pos, oracle_current_board_index, oracle_current_secondary, exact_record);
    }
    store_out_record(exact_record);
    if (best_move) {
        *best_move = check_move;
    }
    return exact_record.proof_disproof;
}

ProofDisproof DfPn::Impl::proof_oracle_defense(Position& pos, const OracleState& oracle, SearchContext& ctx,
    int proof_limit, DfpnRecord* out_record, const bool use_table) {
    const auto store_out_record = [&](const DfpnRecord& value) {
        if (out_record) {
            *out_record = value;
        }
    };
    if (pos.gamePly() >= ctx.max_game_ply || osl_tree_depth_limit_reached(ctx)) {
        throw DepthLimitReached();
    }

    const Key key = pos.getKey();
    const bool pushed_board_index = ctx.board_index_history.empty();
    if (pushed_board_index) {
        ctx.board_index_history.push_back(board_index_key(pos));
        ctx.board_secondary_history.push_back(secondary_board_key(pos));
        ctx.stand_history.push_back(stand_pair(pos));
    }
    struct BoardHistoryPop {
        std::vector<Key>& board_index_history;
        std::vector<uint64_t>& board_secondary_history;
        std::vector<std::array<Hand, ColorNum>>& stand_history;
        bool active;
        ~BoardHistoryPop() {
            if (active) {
                board_index_history.pop_back();
                board_secondary_history.pop_back();
                stand_history.pop_back();
            }
        }
    } board_history_pop{ ctx.board_index_history, ctx.board_secondary_history, ctx.stand_history, pushed_board_index };

    const PathEncoding current_path = ctx.path_encodings.empty() ? PathEncoding(ctx.attack_color) : ctx.path_encodings.back();
    const bool pushed_path = ctx.path_encodings.empty();
    if (pushed_path) {
        ctx.path_encodings.push_back(current_path);
    }
    struct PathEncodingPop {
        std::vector<PathEncoding>& path_encodings;
        bool active;
        ~PathEncodingPop() {
            if (active) {
                path_encodings.pop_back();
            }
        }
    } path_encoding_pop{ ctx.path_encodings, pushed_path };

    const int my_distance = (use_table && !ctx.path_records.empty())
        ? ctx.path_records.back()->distance + 1
        : 0;
    DfpnPathTable::LoopToDominance loop = DfpnPathTable::LoopToDominance::NoLoop;
    PathRecord* path_record = use_table ? path_state(ctx, pos, my_distance, loop) : nullptr;
    ReturnPathRecordScope return_path_record_scope{ ctx, path_record };
    VisitLock visit_lock(path_record);
    if (use_table && loop == DfpnPathTable::LoopToDominance::BadAttackLoop) {
        DfpnRecord loop_record;
        set_loop_detection_record(loop_record, path_record, current_path, nullptr);
        store_out_record(loop_record);
        return loop_record.proof_disproof;
    }
    if (use_table) {
        ctx.path_records.push_back(path_record);
    }
    struct PathRecordStackPop {
        std::vector<PathRecord*>& path_records;
        bool active;
        ~PathRecordStackPop() {
            if (active) {
                path_records.pop_back();
            }
        }
    } path_record_stack_pop{ ctx.path_records, use_table };

    if (!use_table && ctx.board_index_history.size() >= 5) {
        const size_t current = ctx.board_index_history.size() - 1;
        const auto same_hash_key = [&](const size_t ancestor) {
            return ctx.board_index_history[ancestor] == ctx.board_index_history[current]
                && ctx.board_secondary_history[ancestor] == ctx.board_secondary_history[current]
                && ctx.stand_history[ancestor][Black] == ctx.stand_history[current][Black];
        };
        if (same_hash_key(current - 4)
            || (current >= 6 && same_hash_key(current - 6))) {
            DfpnRecord empty_record;
            store_out_record(empty_record);
            return ProofDisproof::Unknown();
        }
    }

    const uint32_t node_count_org = ctx.node_count;
    ++ctx.node_count;
    const Square attacker_king = pos.kingSquare(ctx.attack_color);
    if (!pos.inCheck()
        || (attacker_king != SquareNum && effect_has_at(pos, pos.turn(), attacker_king))) {
        DfpnRecord no_check_record;
        no_check_record.proof_disproof = ProofDisproof::NoCheckmate();
        store_out_record(no_check_record);
        return no_check_record.proof_disproof;
    }

    const Key oracle_current_board_index = ctx.current_board_index(pos);
    const uint64_t oracle_current_secondary = ctx.current_board_secondary(pos);
    const DfpnRecord record = probe(pos, oracle_current_board_index, oracle_current_secondary, ctx.attack_color);
    if (record.proof_disproof.isFinal()) {
        store_out_record(record);
        return record.proof_disproof;
    }

    if (proof_limit > static_cast<int>(kProofSimulationTolerance)) {
        proof_limit = static_cast<int>(kProofSimulationTolerance);
    }

    DfpnRecord exact_record = record;
    exact_record.refresh_stands(pos, oracle_current_secondary);
    exact_record.exact = true;
    const Move current_parent_move = ctx.move_history.empty() ? Move::moveNone() : ctx.move_history.back();
    const Move recorded_last_move = exact_record.last_move;
    if (!exact_record.proof_disproof.isFinal()) {
        exact_record.proof_pieces_set = ProofPiecesType::Unset;
    }

    if (exact_record.last_to == SquareNum) {
        exact_record.last_to = grand_parent_simulation_suitable(ctx.move_history)
            ? ctx.move_history.back().to()
            : pos.kingSquare(pos.turn());
    }

    const Square delayed_to = exact_record.last_to != pos.kingSquare(pos.turn()) ? exact_record.last_to : SquareNum;
    std::vector<Move>& moves = ctx.moves_for_current_depth();
    generate_escape_moves(pos, moves, true, delayed_to,
        ctx.piece_numbers ? &*ctx.piece_numbers : nullptr);
    if (moves.empty()) {
        exact_record.proof_disproof = ProofDisproof::NoEscape();
        set_parent_link(exact_record, ctx);
        exact_record.remaining_depth = remaining_depth(pos, ctx);
        exact_record.proof_pieces_set = ProofPiecesType::Unset;
        exact_record.setProofPieces(proof_pieces_leaf(pos, ctx.attack_color, exact_record.stands[ctx.attack_color]));
        store_out_record(exact_record);
        return exact_record.proof_disproof;
    }

    std::vector<ChildState>& children = ctx.children_for_current_depth();
    children.clear();
    children.resize(moves.size());
    ActiveNodeScope active_node(ctx, exact_record, moves, pos);
    active_node.set_children(children);
    for (size_t i = 0; i < moves.size(); ++i) {
        children[i].reset_for_move(moves[i]);
        if (exact_record.solved & child_bit(i)) {
            continue;
        }
        Key child_board_index = 0;
        uint64_t child_board_secondary = 0;
        std::array<Hand, ColorNum> child_stands{ Hand(0), Hand(0) };
        {
            child_board_index = ctx.child_board_index(pos, moves[i]);
            child_board_secondary = ctx.child_board_secondary(pos, moves[i]);
            child_stands = ctx.child_stands(pos, moves[i]);
        }
        children[i].board_index = child_board_index;
        children[i].board_secondary = child_board_secondary;
        children[i].stands = child_stands;
        OslPieceNumberState::Undo piece_undo;
        const OslPieceNumberState* child_piece_numbers = nullptr;
        bool piece_number_applied = false;
        if (ctx.piece_numbers) {
            piece_number_applied = ctx.piece_numbers->apply_move(moves[i], &piece_undo);
            if (piece_number_applied) {
                child_piece_numbers = &*ctx.piece_numbers;
            }
        }
        StateInfo st;
        {
            pos.doMove(moves[i], st);
            assert(child_board_secondary == secondary_board_key(pos));
        }
        DfpnRecord child_record;
        {
            child_record = use_table
                ? table.probe(child_board_index, child_board_secondary, child_stands, ctx.attack_color)
                : DfpnRecord();
        }
        {
            children[i].pdp = child_record.proof_disproof;
            children[i].best_reply = child_record.best_move;
            children[i].last_move = child_record.last_move;
            children[i].proof_pieces_type = child_record.proof_pieces_set;
            children[i].proof_pieces = child_record.proof_pieces;
            children[i].node_count = child_record.node_count;
            children[i].need_full_width = child_record.need_full_width;
            copy_record_aux_to_child(children[i], child_record);
        }
        if (children[i].pdp.isCheckmateSuccess()) {
            set_checkmate_child_in_defense(exact_record, children[i], i);
        }
        else if (record.node_count == 0 && child_record.node_count == 0) {
            Move check_move = Move::moveNone();
            Hand proof_pieces = zero_hand();
            {
                children[i].pdp = fixed_escape_by_move_zero(
                    pos, ctx.attack_color, &check_move, &proof_pieces, false, true, child_piece_numbers);
            }
            ++ctx.node_count;
            if (children[i].pdp.isCheckmateSuccess()) {
                children[i].best_reply = check_move;
                children[i].proof_pieces_type = ProofPiecesType::Proof;
                children[i].proof_pieces = proof_pieces;
                children[i].need_full_width = child_record.need_full_width;
                set_checkmate_child_in_defense(exact_record, children[i], i);
            }
            else if (children[i].pdp.isCheckmateFail()) {
                children[i].pdp = ProofDisproof(1, 1);
            }
        }
        children[i].path_record = use_table ? probe_child_path(ctx, child_board_index, child_board_secondary, child_stands[Black]) : nullptr;

        if (children[i].pdp.isCheckmateFail()) {
            if (piece_number_applied) {
                ctx.piece_numbers->undo_move(piece_undo);
            }
            pos.undoMove(moves[i]);
            exact_record.proof_disproof = children[i].pdp;
            exact_record.best_move = moves[i];
            set_parent_link(exact_record, ctx);
            exact_record.remaining_depth = remaining_depth(pos, ctx);
            exact_record.proof_pieces_set = ProofPiecesType::Unset;
            exact_record.setDisproofPieces(disproof_pieces_after_defense(
                children[i].proof_pieces,
                complete_move_for_position(pos, moves[i]), exact_record.stands[oppositeColor(ctx.attack_color)]));
            if (use_table) {
                commit_oracle_record(pos, oracle_current_board_index, oracle_current_secondary, exact_record);
            }
            store_out_record(exact_record);
            return exact_record.proof_disproof;
        }

        {
            pos.undoMove(moves[i]);
        }
        if (piece_number_applied) {
            ctx.piece_numbers->undo_move(piece_undo);
        }
    }

    for (size_t i = 0; i < moves.size(); ++i) {
        if (exact_record.solved & child_bit(i)) {
            continue;
        }
        if (use_table && child_is_loop(children[i], current_path)) {
            set_loop_detection_record(exact_record, path_record, current_path, out_record);
            return exact_record.proof_disproof;
        }
    }

    uint32_t sum_proof = 0;
    uint32_t min_disproof = exact_record.min_pdp;
    for (size_t next_i = 0; next_i < moves.size(); ++next_i) {
        const Move move = moves[next_i];
        if (exact_record.solved & child_bit(next_i)) {
            continue;
        }
        if (children[next_i].pdp.isCheckmateSuccess()) {
            min_disproof = std::min(min_disproof, children[next_i].pdp.disproof);
            continue;
        }
        const Color mover = pos.turn();
        bool traceable = false;
        {
            traceable = oracle_traceable(oracle, mover, move);
        }
        if (!traceable) {
            sum_proof = saturate_sum(static_cast<uint64_t>(sum_proof) + 1, ProofDisproof::PROOF_MAX);
            min_disproof = 1;
            if (!use_table) {
                break;
            }
            continue;
        }
        const Square next_to = move.to();
        if (sum_proof) {
            bool skip_by_effect = false;
            {
                const Bitboard attack_effect = effect_set_at(pos, ctx.attack_color, next_to);
                if (attack_effect.isAny()) {
                    const Bitboard defense_effect = effect_set_at(pos, pos.turn(), next_to);
                    skip_by_effect = !defense_effect.isAny()
                        || (defense_effect.popCount() == 1 && !move.isDrop());
                }
            }
            if (skip_by_effect) {
                continue;
            }
        }
        const Key child_board_index = ctx.child_board_index(pos, move);
        const uint64_t child_board_secondary = ctx.child_board_secondary(pos, move);
        const std::array<Hand, ColorNum> child_stands = ctx.child_stands(pos, move);
        StateInfo current_state;
        const OracleState child_oracle = oracle_state_after_move(oracle, mover, move);
        pos.doMove(move, current_state);
        ctx.push_move(move);
        PathEncoding child_path_encoding = current_path;
        child_path_encoding.pushMove(move);
        ctx.path_encodings.push_back(child_path_encoding);
        ctx.board_index_history.push_back(child_board_index);
        ctx.board_secondary_history.push_back(child_board_secondary);
        ctx.stand_history.push_back(child_stands);

        ProofDisproof child = ProofDisproof::Unknown();
        DfpnRecord child_exact;
        const PathRecord* child_path_record = nullptr;
        try {
            Move child_best = Move::moveNone();
            DfpnRecord attack_record;
            {
                child = proof_oracle_attack(pos, child_oracle, ctx,
                    proof_limit - static_cast<int>(sum_proof), &child_best, &attack_record, use_table);
            }
            // OSL assigns next.record to node.children[next_i] regardless of
            // the proofOracleAttack threshold return value.
            child_exact = attack_record;
            child_path_record = ctx.returned_path_record;
        }
        catch (...) {
            ctx.stand_history.pop_back();
            ctx.board_secondary_history.pop_back();
            ctx.board_index_history.pop_back();
            ctx.path_encodings.pop_back();
            ctx.pop_move();
            pos.undoMove(move);
            throw;
        }

        ctx.stand_history.pop_back();
        ctx.board_secondary_history.pop_back();
        ctx.board_index_history.pop_back();
        ctx.path_encodings.pop_back();
        ctx.pop_move();
        pos.undoMove(move);

        ProofDisproof child_record_pdp = child_exact.proof_disproof;
        if (child.isLoopDetection()) {
            child_record_pdp = child;
        }

        if (child_record_pdp.isLoopDetection()) {
            set_loop_detection_record(exact_record, path_record, current_path, out_record);
            return exact_record.proof_disproof;
        }

        if (child_record_pdp.isCheckmateFail()) {
            exact_record.proof_disproof = child_record_pdp;
            exact_record.best_move = move;
            set_parent_link(exact_record, ctx);
            accumulate_record_node_count(exact_record, node_count_org, ctx.node_count);
            exact_record.remaining_depth = remaining_depth(pos, ctx);
            exact_record.proof_pieces_set = ProofPiecesType::Unset;
            exact_record.setDisproofPieces(disproof_pieces_after_defense(
                child_exact.proof_pieces,
                complete_move_for_position(pos, move), exact_record.stands[oppositeColor(ctx.attack_color)]));
            if (use_table) {
                commit_oracle_record(pos, oracle_current_board_index, oracle_current_secondary, exact_record);
            }
            store_out_record(exact_record);
            return exact_record.proof_disproof;
        }

        children[next_i].pdp = child_record_pdp;
        children[next_i].best_reply = child_exact.best_move;
        children[next_i].last_move = child_exact.last_move;
        children[next_i].proof_pieces_type = child_exact.proof_pieces_set;
        children[next_i].proof_pieces = child_exact.proof_pieces;
        children[next_i].node_count = child_exact.node_count;
        children[next_i].need_full_width = child_exact.need_full_width;
        copy_record_aux_to_child(children[next_i], child_exact);
        children[next_i].path_record = child_path_record;

        if (child_record_pdp.isCheckmateSuccess()) {
            set_checkmate_child_in_defense(exact_record, children[next_i], next_i);
        }
        const uint32_t child_proof = child_record_pdp.proof;
        const uint32_t child_disproof = child_record_pdp.disproof;
        sum_proof = saturate_sum(static_cast<uint64_t>(sum_proof) + child_proof, ProofDisproof::PROOF_MAX);
        min_disproof = std::min(min_disproof, child_disproof);
        if ((sum_proof != 0 && !use_table) || static_cast<int>(sum_proof) > proof_limit) {
            break;
        }
    }

    if (sum_proof == 0) {
        exact_record.proof_disproof = ProofDisproof(0, std::max<uint32_t>(1, min_disproof));
        set_parent_link(exact_record, ctx);
        exact_record.remaining_depth = remaining_depth(pos, ctx);
        Hand pieces = exact_record.proof_pieces_candidate;
        if (!is_unblockable_check(pos)) {
            add_monopolized_pieces(pos.hand(ctx.attack_color), pos.hand(oppositeColor(ctx.attack_color)), exact_record.stands[ctx.attack_color], pieces);
        }
        exact_record.proof_pieces_set = ProofPiecesType::Unset;
        exact_record.setProofPieces(pieces);
        exact_record.proof_disproof = ProofDisproof::Checkmate();
        store_out_record(exact_record);
        return exact_record.proof_disproof;
    }
    if (use_table
        && is_osl_normal_move(recorded_last_move)
        && recorded_last_move != current_parent_move
        && std::max(exact_record.proof(), exact_record.disproof()) >= 128) {
        find_dag_source(pos, ctx, exact_record, 1, "oracle-defense-self");
    }
    if (use_table) {
        exact_record.last_move = current_parent_move;
    }
    set_parent_link(exact_record, ctx);
    exact_record.remaining_depth = remaining_depth(pos, ctx);
    store_out_record(exact_record);
    return ProofDisproof::Unknown();
}

    void DfPn::Impl::blocking_simulation(Position& pos, SearchContext& ctx, const int proof_limit, DfpnRecord& exact_record,
        const std::vector<Move>& moves, std::vector<ChildState>& children, const size_t oracle_index) {
        if (oracle_index >= moves.size()) {
            return;
        }
        const bool oracle_solved = (exact_record.solved & child_bit(oracle_index)) != 0;
        if (!oracle_solved && !children[oracle_index].pdp.isCheckmateSuccess()) {
            return;
        }

        const PathEncoding current_path = ctx.path_encodings.empty() ? PathEncoding(ctx.attack_color) : ctx.path_encodings.back();
        const Square target = moves[oracle_index].to();
        const Color mover = pos.turn();
        const std::array<Hand, ColorNum> current_stands =
            ctx.stand_history.empty() ? stand_pair(pos) : ctx.stand_history.back();
        const OracleState current_oracle = oracle_state_from_current_node(
            ctx.current_board_index(pos), ctx.current_board_secondary(pos), current_stands);
        const OracleState oracle = oracle_state_after_move(current_oracle, mover, moves[oracle_index]);
        for (size_t i = 0; i < moves.size(); ++i) {
        if (exact_record.solved & child_bit(i)) {
            continue;
        }
        if (child_is_loop(children[i], current_path)) {
            break;
        }
            if (children[i].pdp.isFinal() || moves[i].to() != target) {
                continue;
            }
            if (!oracle_traceable(oracle, mover, moves[i])) {
                continue;
            }
            const Key child_board_index = children[i].board_index;
            const uint64_t child_board_secondary = children[i].board_secondary;
            const std::array<Hand, ColorNum> child_stands = children[i].stands;
            StateInfo current_state;
            pos.doMove(moves[i], current_state);
            Move child_best = Move::moveNone();
            ctx.push_move(moves[i]);
            PathEncoding child_path = ctx.path_encodings.empty() ? PathEncoding(ctx.attack_color) : ctx.path_encodings.back();
            child_path.pushMove(moves[i]);
            ctx.path_encodings.push_back(child_path);
            ctx.board_index_history.push_back(child_board_index);
            ctx.board_secondary_history.push_back(child_board_secondary);
            ctx.stand_history.push_back(child_stands);
            try {
                DfpnRecord child_exact;
                (void)proof_oracle_attack(pos, oracle, ctx, proof_limit, &child_best, &child_exact);
                children[i].pdp = child_exact.proof_disproof;
                children[i].best_reply = child_exact.best_move;
                children[i].last_move = child_exact.last_move;
                children[i].proof_pieces_type = child_exact.proof_pieces_set;
                children[i].proof_pieces = child_exact.proof_pieces;
                children[i].node_count = child_exact.node_count;
                children[i].need_full_width = child_exact.need_full_width;
                copy_record_aux_to_child(children[i], child_exact);
                children[i].path_record = ctx.returned_path_record;
            }
            catch (const DepthLimitReached&) {
                ctx.stand_history.pop_back();
                ctx.board_secondary_history.pop_back();
                ctx.board_index_history.pop_back();
                ctx.path_encodings.pop_back();
                ctx.pop_move();
                pos.undoMove(moves[i]);
                throw;
            }
            ctx.stand_history.pop_back();
            ctx.board_secondary_history.pop_back();
            ctx.board_index_history.pop_back();
            ctx.path_encodings.pop_back();
            ctx.pop_move();
            pos.undoMove(moves[i]);
            if (children[i].pdp.isCheckmateSuccess()) {
                set_checkmate_child_in_defense(exact_record, children[i], i);
        }
    }
}

void DfPn::Impl::grand_parent_simulation(Position& pos, SearchContext& ctx, DfpnRecord& exact_record,
    const std::vector<Move>& moves, std::vector<ChildState>& children, const size_t child_index) {
    if (!ctx.root_position || !grand_parent_simulation_suitable(ctx.move_history) || ctx.move_history.size() < 3) {
        return;
    }
    if (child_index >= moves.size() || child_index >= children.size()) {
        return;
    }
    if (children[child_index].pdp.isFinal()
        || children[child_index].pdp.proof != 1
        || children[child_index].pdp.disproof != 1) {
        return;
    }

    if (ctx.active_nodes.size() < 3) {
        return;
    }
    const SearchContext::ActiveNode& grandparent_node = ctx.active_nodes[ctx.active_nodes.size() - 3];
    if (!grandparent_node.record || !grandparent_node.moves || !grandparent_node.children) {
        return;
    }
    const std::vector<Move>& grandparent_moves = *grandparent_node.moves;
    const std::vector<ChildState>& grandparent_children = *grandparent_node.children;
    const uint32_t grandparent_proof_limit = grandparent_node.threshold.proof;
    const DfpnRecord& grandparent_record = *grandparent_node.record;

    const size_t i = child_index;
    const auto grandparent_it = std::find(grandparent_moves.begin(), grandparent_moves.end(), moves[i]);
    if (grandparent_it == grandparent_moves.end()) {
        return;
    }

    const size_t grandparent_index = static_cast<size_t>(grandparent_it - grandparent_moves.begin());
    if (grandparent_index >= grandparent_children.size()) {
        return;
    }
    const Move grandparent_move = grandparent_moves[grandparent_index];
    const bool grandparent_solved =
        (grandparent_record.solved & child_bit(grandparent_index)) != 0;
    const bool grandparent_success = grandparent_solved
        || grandparent_children[grandparent_index].pdp.isCheckmateSuccess();
    if (!grandparent_success) {
        return;
    }

    const std::array<Hand, ColorNum> grandparent_stands{ grandparent_node.black_stand, grandparent_node.white_stand };
    const Color mover = pos.turn();
    const OracleState grandparent_oracle = oracle_state_from_current_node(
        grandparent_node.board_index, grandparent_node.board_secondary, grandparent_stands);
    const OracleState oracle = grandparent_solved
        ? oracle_state_after_move(grandparent_oracle, mover, grandparent_move)
        : oracle_state_from_current_node(
            board_index_key_after_move(grandparent_node.board_index, mover, grandparent_move),
            secondary_board_key_after_move(grandparent_node.board_secondary, mover, grandparent_move),
            stand_pair_after_move(grandparent_stands, mover, grandparent_move));

    const Key child_board_index = children[i].board_index;
    const uint64_t child_board_secondary = children[i].board_secondary;
    const std::array<Hand, ColorNum> child_stands = children[i].stands;
    StateInfo current_state;
    pos.doMove(moves[i], current_state);
        ctx.push_move(moves[i]);
        PathEncoding child_path = ctx.path_encodings.empty() ? PathEncoding(ctx.attack_color) : ctx.path_encodings.back();
        child_path.pushMove(moves[i]);
        ctx.path_encodings.push_back(child_path);
        ctx.board_index_history.push_back(child_board_index);
        ctx.board_secondary_history.push_back(child_board_secondary);
        ctx.stand_history.push_back(child_stands);
        try {
            DfpnRecord child_exact;
            (void)proof_oracle_attack(pos, oracle, ctx, grandparent_proof_limit, nullptr, &child_exact);
            children[i].pdp = child_exact.proof_disproof;
            children[i].best_reply = child_exact.best_move;
            children[i].last_move = child_exact.last_move;
            children[i].proof_pieces_type = child_exact.proof_pieces_set;
            children[i].proof_pieces = child_exact.proof_pieces;
            children[i].node_count = child_exact.node_count;
            children[i].need_full_width = child_exact.need_full_width;
            copy_record_aux_to_child(children[i], child_exact);
            children[i].path_record = ctx.returned_path_record;
        }
        catch (const DepthLimitReached&) {
            ctx.stand_history.pop_back();
            ctx.board_secondary_history.pop_back();
            ctx.board_index_history.pop_back();
            ctx.path_encodings.pop_back();
            ctx.pop_move();
            pos.undoMove(moves[i]);
            throw;
        }
        ctx.stand_history.pop_back();
        ctx.board_secondary_history.pop_back();
        ctx.board_index_history.pop_back();
        ctx.path_encodings.pop_back();
        ctx.pop_move();
    pos.undoMove(moves[i]);
}

ProofDisproof DfPn::Impl::ensure_attack(Position& pos) {
    const DfpnRecord entry = probe(pos, root_attack_color);
    if (entry.proof_disproof.isFinal()) {
        return entry.proof_disproof;
    }
    SearchContext ctx(*this);
    ctx.set_root(pos);
    ctx.max_game_ply = std::min(pos.gamePly() + max_depth, draw_ply);
    ctx.attack_color = root_attack_color;
    Move best = Move::moveNone();
    try {
        return attack(pos, ctx, { kRootProofTolerance, kRootDisproofTolerance }, &best);
    }
    catch (const DepthLimitReached&) {
        return ProofDisproof::Unknown();
    }
}

ProofDisproof DfPn::Impl::ensure_defense(Position& pos) {
    const DfpnRecord entry = probe(pos, root_attack_color);
    if (entry.proof_disproof.isFinal()) {
        return entry.proof_disproof;
    }
    SearchContext ctx(*this);
    ctx.set_root(pos);
    ctx.max_game_ply = std::min(pos.gamePly() + max_depth, draw_ply);
    ctx.attack_color = root_attack_color;
    try {
        return defense(pos, ctx, { kRootProofTolerance, kRootDisproofTolerance });
    }
    catch (const DepthLimitReached&) {
        return ProofDisproof::Unknown();
    }
}

bool DfPn::Impl::linked_pv_attack_move(Position& pos, Move& best_move, const Move incoming_move) {
    best_move = Move::moveNone();

    const Move mate1 = immediate_mate_move_in_1_osl(pos);
    if (mate1) {
        best_move = mate1;
        return true;
    }

    if (kEnableFixedDepthShortcut) {
        Hand fixed_proof_pieces = zero_hand();
        std::optional<OslPieceNumberState> piece_numbers = OslPieceNumberState::from_position(pos);
        if (fixed_attack_osl_shortcut(
            pos, root_attack_color, &best_move, &fixed_proof_pieces,
            piece_numbers ? &*piece_numbers : nullptr).isCheckmateSuccess()) {
            return true;
        }
    }

    std::vector<Move> moves = generate_check_moves(pos);
    if (moves.empty()) {
        return true;
    }

    const auto has_move = [&](const Move candidate) {
        return candidate.isAny() && std::find(moves.begin(), moves.end(), candidate) != moves.end();
    };

    const DfpnRecord exact_record = load_exact_record(pos);
    if (has_move(exact_record.best_move)) {
        best_move = exact_record.best_move;
        return true;
    }

    const DfpnRecord record = probe(pos, root_attack_color);
    if (record.proof_disproof.isCheckmateSuccess() && has_move(record.best_move)) {
        best_move = record.best_move;
        return true;
    }

    if (incoming_move.isAny()) {
        const DfpnRecord oracle = find_proof_oracle(OracleState(pos), root_attack_color, incoming_move);
        const Move oracle_move = oracle.proof_disproof.isCheckmateSuccess()
            ? adjust_oracle_attack_move(pos, oracle.best_move)
            : Move::moveNone();
        if (has_move(oracle_move)) {
            best_move = oracle_move;
            return true;
        }
    }

    if (!record.proof_disproof.isCheckmateSuccess()) {
        return true;
    }

    return false;
}

bool DfPn::Impl::linked_pv_defense_move(Position& pos, Move& best_move, const Move incoming_move) {
    best_move = Move::moveNone();
    if (!pos.inCheck()) {
        return true;
    }

    std::vector<Move> moves = generate_escape_moves(pos, true, SquareNum);
    if (moves.empty()) {
        return true;
    }

    const auto has_move = [&](const Move candidate) {
        return candidate.isAny() && std::find(moves.begin(), moves.end(), candidate) != moves.end();
    };

    const DfpnRecord exact_record = load_exact_record(pos);
    if (has_move(exact_record.best_move)) {
        best_move = exact_record.best_move;
        return true;
    }

    if (incoming_move.isAny()) {
        const DfpnRecord oracle = find_proof_oracle(OracleState(pos), root_attack_color, incoming_move);
        if (oracle.proof_disproof.isCheckmateSuccess() && has_move(oracle.best_move)) {
            best_move = oracle.best_move;
            return true;
        }
    }

    const DfpnRecord record = probe(pos, root_attack_color);
    if (has_move(record.best_move)) {
        best_move = record.best_move;
        return true;
    }
    if (record.proof_disproof.isCheckmateFail()) {
        return true;
    }

    return false;
}

int DfPn::Impl::pv_attack(Position& pos, PvDepthTable& table, Move& best_move, const int height, const Move incoming_move,
    const Key board_index, const uint64_t board_secondary, OslPieceNumberState* piece_numbers) {
    if (height >= max_depth) {
        best_move = Move::moveNone();
        return -1;
    }
    best_move = Move::moveNone();
    if (!pos.inCheck()) {
        const Move mate1 = immediate_mate_move_in_1_osl(pos);
        if (mate1) {
            table.store(board_index, board_secondary, pos.hand(Black), 1, mate1);
            best_move = mate1;
            return 1;
        }
    }
    if (kEnableFixedDepthShortcut) {
        const ProofDisproof fixed_pdp = fixed_attack_depth2_pv_osl(pos, root_attack_color, &best_move, piece_numbers);
        if (fixed_pdp.isCheckmateSuccess()) {
            table.store(board_index, board_secondary, pos.hand(Black), 3, best_move);
            return 3;
        }
    }

    DfpnRecord record = this->table.probe(board_index, board_secondary, stand_pair(pos), root_attack_color);
    if (!record.proof_disproof.isCheckmateSuccess()) {
        table.store(board_index, board_secondary, pos.hand(Black), 5, Move::moveNone());
        return 5;
    }

    int recorded = 0;
    if (table.find(board_index, board_secondary, pos.hand(Black), recorded, best_move)) {
        return recorded;
    }

    table.store(board_index, board_secondary, pos.hand(Black), -1, Move::moveNone());

    if (!record.best_move) {
        table.store(board_index, board_secondary, pos.hand(Black), 1, Move::moveNone());
    }

    if (record.best_move) {
        const Color mover = pos.turn();
        const Key child_board_index = board_index_key_after_move(board_index, mover, record.best_move);
        const uint64_t child_board_secondary = secondary_board_key_after_move(board_secondary, mover, record.best_move);
        const std::array<Hand, ColorNum> child_stands =
            stand_pair_after_move(stand_pair(pos), mover, record.best_move);
        OslPieceNumberState::Undo piece_undo;
        const bool piece_ok = piece_numbers && piece_numbers->apply_move(record.best_move, &piece_undo);
        if (piece_numbers && !piece_ok) {
            piece_numbers = nullptr;
        }
        StateInfo st;
        pos.doMove(record.best_move, st);
        assert(child_board_secondary == secondary_board_key(pos));
        DfpnRecord child_record = this->table.probe(child_board_index, child_board_secondary, child_stands, root_attack_color);
        if (!child_record.proof_disproof.isCheckmateSuccess()) {
            child_record = find_proof_oracle(OracleState(pos), root_attack_color, record.best_move);
        }
        if (child_record.proof_disproof.isCheckmateSuccess()) {
            Move child_best = Move::moveNone();
            int depth = pv_defense(pos, table, child_best, height + 1, record.best_move,
                child_board_index, child_board_secondary, piece_numbers);
            pos.undoMove(record.best_move);
            if (piece_ok) {
                piece_numbers->undo_move(piece_undo);
            }
            if (depth >= 0) {
                best_move = record.best_move;
                table.store(board_index, board_secondary, pos.hand(Black), depth + 1, best_move);
                return depth + 1;
            }
            best_move = Move::moveNone();
            return 0;
        }
        pos.undoMove(record.best_move);
        if (piece_ok) {
            piece_numbers->undo_move(piece_undo);
        }
    }
    best_move = Move::moveNone();
    return 0;
}

int DfPn::Impl::pv_defense(Position& pos, PvDepthTable& table, Move& best_move, const int height, const Move incoming_move,
    const Key board_index, const uint64_t board_secondary, OslPieceNumberState* piece_numbers) {
    if (height >= max_depth) {
        best_move = Move::moveNone();
        return -1;
    }
    best_move = Move::moveNone();
    int recorded = 0;
    if (table.find(board_index, board_secondary, pos.hand(Black), recorded, best_move)) {
        return recorded;
    }
    table.store(board_index, board_secondary, pos.hand(Black), -1, Move::moveNone());
    auto moves = std::make_unique<std::vector<Move>>(generate_escape_moves(pos, true, SquareNum, piece_numbers));
    if (moves->empty()) {
        table.store(board_index, board_secondary, pos.hand(Black), 0, Move::moveNone());
        return 0;
    }

    int result = 0;
    for (size_t i = 0; i < moves->size(); ++i) {
        const Move move = (*moves)[i];
        const Color mover = pos.turn();
        const Key child_board_index = board_index_key_after_move(board_index, mover, move);
        const uint64_t child_board_secondary = secondary_board_key_after_move(board_secondary, mover, move);
        OslPieceNumberState::Undo piece_undo;
        OslPieceNumberState* child_piece_numbers = piece_numbers;
        const bool piece_ok = child_piece_numbers && child_piece_numbers->apply_move(move, &piece_undo);
        if (child_piece_numbers && !piece_ok) {
            child_piece_numbers = nullptr;
        }
        StateInfo st;
        pos.doMove(move, st);
        assert(child_board_secondary == secondary_board_key(pos));
        const bool expect_more = (i == 0) ? true
            : table.expect_more_depth(oppositeColor(mover), child_board_index, child_board_secondary, pos.hand(Black), result, &pos);
        if (!expect_more) {
            pos.undoMove(move);
            if (piece_ok) {
                piece_numbers->undo_move(piece_undo);
            }
            continue;
        }
        Move child_best = Move::moveNone();
        int depth = pv_attack(pos, table, child_best, height + 1, move,
            child_board_index, child_board_secondary, child_piece_numbers);
        pos.undoMove(move);
        if (piece_ok) {
            piece_numbers->undo_move(piece_undo);
        }
        if (depth < 0) {
            return depth;
        }
        if (result < depth + 1) {
            result = depth + 1;
            best_move = move;
        }
    }
    table.store(board_index, board_secondary, pos.hand(Black), result, best_move);
    return result;
}

void DfPn::Impl::retrieve_pv(Position& pos, const bool attack_node, std::vector<u32>& pv) {
    PvDepthTable table;
    pv.clear();
    std::vector<Move> applied;
    std::vector<StateInfo> states;
    states.reserve(static_cast<size_t>(max_depth));

    const auto undo_applied = [&]() {
        while (!applied.empty()) {
            pos.undoMove(applied.back());
            applied.pop_back();
        }
    };

    Key current_board_index = board_index_key(pos);
    uint64_t current_board_secondary = secondary_board_key(pos);
    std::optional<OslPieceNumberState> piece_numbers = OslPieceNumberState::from_position(pos);
    for (int i = 0; i < max_depth; ++i) {
        Move next = Move::moveNone();
        const bool attack_turn = attack_node ^ ((i % 2) != 0);
        if (attack_turn) {
            pv_attack(pos, table, next, 0, Move::moveNone(), current_board_index, current_board_secondary,
                piece_numbers ? &*piece_numbers : nullptr);
        }
        else {
            pv_defense(pos, table, next, 0, Move::moveNone(), current_board_index, current_board_secondary,
                piece_numbers ? &*piece_numbers : nullptr);
        }
        if (!next) {
            undo_applied();
            return;
        }
        pv.push_back(next.value());
        const Color mover = pos.turn();
        const Key child_board_index = board_index_key_after_move(current_board_index, mover, next);
        const uint64_t child_board_secondary = secondary_board_key_after_move(current_board_secondary, mover, next);
        if (piece_numbers && !piece_numbers->apply_move(next)) {
            piece_numbers.reset();
        }
        states.emplace_back();
        pos.doMove(next, states.back());
        assert(child_board_secondary == secondary_board_key(pos));
        assert(child_board_index == board_index_key(pos));
        current_board_index = child_board_index;
        current_board_secondary = child_board_secondary;
        applied.push_back(next);
    }

    undo_applied();
}

DfPn::DfPn() : impl_(std::make_unique<Impl>()) {}

DfPn::DfPn(const DfPn& dfpn) : impl_(std::make_unique<Impl>()) {
    reset_config_from(dfpn);
}

DfPn::DfPn(const int max_depth, const uint32_t max_search_node, const int draw_ply)
    : impl_(std::make_unique<Impl>()) {
    set_maxdepth(max_depth);
    set_max_search_node(max_search_node);
    set_draw_ply(draw_ply);
}

DfPn::~DfPn() = default;

DfPn& DfPn::operator=(const DfPn& r) {
    if (this != &r) {
        reset_config_from(r);
    }
    return *this;
}

void DfPn::reset_config_from(const DfPn& other) {
    set_maxdepth(other.impl_->max_depth);
    set_max_search_node(other.impl_->max_search_node);
    impl_->draw_ply = other.impl_->draw_ply;
    impl_->reserve_by_hash(other.impl_->reserved_hash_mb);
    impl_->table.clear();
    impl_->path_table.clear();
    searchedNode = 0;
}

bool DfPn::dfpn(Position& pos) {
    impl_->stop = false;
    impl_->root_attack_color = pos.turn();
    searchedNode = 0;
    if (impl_->max_search_node == 0) {
        return false;
    }

    ProofDisproof result = ProofDisproof::Unknown();
    Move best = Move::moveNone();

    const PathEncoding root_path(pos.turn());
    uint32_t limit = std::min(kRootSearchInitialNodeLimit, impl_->max_search_node);

    while (true) {
        impl_->path_table.clear();

        Impl::SearchContext ctx(*impl_);
        ctx.main_search = true;
        ctx.node_limit = limit;
        ctx.set_root(pos);
        ctx.max_game_ply = std::min(pos.gamePly() + impl_->max_depth, impl_->draw_ply);
        ctx.attack_color = impl_->root_attack_color;

        try {
            result = impl_->attack(pos, ctx, { kRootProofTolerance, kRootDisproofTolerance }, &best);
        }
        catch (const DepthLimitReached&) {
            result = ProofDisproof::Unknown();
        }
        if (ctx.returned_path_record && ctx.returned_path_record->hasTwin(root_path)) {
            result = ProofDisproof::LoopDetection();
        }

        searchedNode = saturate_sum(static_cast<uint64_t>(searchedNode) + ctx.node_count, ProofDisproof::PROOF_MAX);
        const uint32_t lite_children_filter = (UINT32_MAX);
        if (lite_children_filter != UINT32_MAX
            && (lite_children_filter == 0 || lite_children_filter == ctx.node_limit)) {
            std::vector<Move> root_moves = generate_check_moves(pos);
            for (size_t i = 0; i < root_moves.size(); ++i) {
                StateInfo st;
                pos.doMove(root_moves[i], st);
                const Key child_board_index = board_index_key(pos);
                const uint64_t child_board_secondary = secondary_board_key(pos);
                DfpnRecord child = impl_->load_exact_record(pos, child_board_index, child_board_secondary);
                pos.undoMove(root_moves[i]);
            }
        }

        const uint32_t stop_after_limit = (0);
        if (stop_after_limit != 0 && ctx.node_limit == stop_after_limit) {
            break;
        }
        if (result.isFinal() || result.isLoopDetection() || limit >= impl_->max_search_node) {
            break;
        }
        limit = next_root_search_limit(limit, impl_->max_search_node);
    }
    return result.isCheckmateSuccess();
}

bool DfPn::dfpn_with_history(Position& root, const std::vector<Move>& history) {
    return dfpn_with_history(root, history, { kRootProofTolerance, kRootDisproofTolerance });
}

bool DfPn::dfpn_with_history(Position& root, const std::vector<Move>& history, ProofDisproof threshold) {
    impl_->stop = false;
    searchedNode = 0;

    if (impl_->max_search_node == 0) {
        return false;
    }

    Position pos(root);
    std::vector<StateInfo> states(history.size());
    PathEncoding path(pos.turn());

    std::vector<Key> board_index_history;
    std::vector<uint64_t> board_secondary_history;
    std::vector<std::array<Hand, ColorNum>> stand_history;
    std::vector<PathEncoding> path_encodings;
    std::vector<Move> move_history;
    board_index_history.reserve(history.size() + 1);
    board_secondary_history.reserve(history.size() + 1);
    stand_history.reserve(history.size() + 1);
    path_encodings.reserve(history.size());
    move_history.reserve(history.size());
    board_index_history.push_back(board_index_key(pos));
    board_secondary_history.push_back(secondary_board_key(pos));
    stand_history.push_back(stand_pair(pos));

    for (size_t i = 0; i < history.size(); ++i) {
        const uint64_t child_board_secondary =
            secondary_board_key_after_move(board_secondary_history.back(), pos.turn(), history[i]);
        pos.doMove(history[i], states[i]);
        path.pushMove(history[i]);
        move_history.push_back(history[i]);
        path_encodings.push_back(path);
        board_index_history.push_back(board_index_key_after_move(
            board_index_history.back(), oppositeColor(pos.turn()), history[i]));
        board_secondary_history.push_back(child_board_secondary);
        stand_history.push_back(stand_pair(pos));
    }

    impl_->root_attack_color = root.turn();

    Move best = Move::moveNone();
    ProofDisproof result = ProofDisproof::Unknown();

    for (int attempt = 0; attempt < 2; ++attempt) {
        impl_->path_table.clear();

        Impl::SearchContext ctx(*impl_);
        ctx.node_limit = impl_->max_search_node;
        ctx.set_root(root);
        ctx.max_game_ply = std::min(pos.gamePly() + impl_->max_depth, impl_->draw_ply);
        ctx.board_index_history = board_index_history;
        ctx.board_secondary_history = board_secondary_history;
        ctx.stand_history = stand_history;
        ctx.path_encodings = path_encodings;
        ctx.move_history = move_history;
        ctx.set_history_piece_numbers(root, history);
        ctx.attack_color = impl_->root_attack_color;

        try {
            if (pos.turn() == ctx.attack_color) {
                result = impl_->attack(pos, ctx, { threshold.proof, threshold.disproof }, &best);
            }
            else {
                best = Move::moveNone();
                result = impl_->defense(pos, ctx, { threshold.proof, threshold.disproof });
            }
        }
        catch (const DepthLimitReached&) {
            result = ProofDisproof::Unknown();
        }
        if (ctx.returned_path_record && ctx.returned_path_record->hasTwin(path)) {
            result = ProofDisproof::LoopDetection();
        }

        searchedNode += ctx.node_count;
        const DfpnRecord* root_record = impl_->table.exact(pos);
        if (pos.turn() == impl_->root_attack_color
            || !(root_record && root_record->need_full_width)
            || attempt == 1) {
            return result.isCheckmateSuccess();
        }
    }
    return result.isCheckmateSuccess();
}

bool DfPn::dfpn_andnode(Position& pos) {
    impl_->stop = false;
    impl_->root_attack_color = oppositeColor(pos.turn());
    ProofDisproof result = ProofDisproof::Unknown();
    impl_->path_table.clear();
    uint32_t searched_nodes = 0;
    try {
        Impl::SearchContext ctx(*impl_);
        ctx.set_root(pos);
        ctx.max_game_ply = std::min(pos.gamePly() + impl_->max_depth, impl_->draw_ply);
        ctx.attack_color = impl_->root_attack_color;
        result = impl_->defense(pos, ctx, { kRootProofTolerance, kRootDisproofTolerance });
        searched_nodes = saturate_sum(static_cast<uint64_t>(searched_nodes) + ctx.node_count, ProofDisproof::PROOF_MAX);
        const DfpnRecord* root = impl_->table.exact(pos);
        if (root && root->need_full_width) {
            // OSL hasEscapeMove clears only the root tree node before retrying
            // a root defense that requested full-width escape generation.
            Impl::SearchContext retry_ctx(*impl_);
            retry_ctx.set_root(pos);
            retry_ctx.max_game_ply = std::min(pos.gamePly() + impl_->max_depth, impl_->draw_ply);
            retry_ctx.attack_color = impl_->root_attack_color;
            result = impl_->defense(pos, retry_ctx, { kRootProofTolerance, kRootDisproofTolerance });
            searched_nodes = saturate_sum(static_cast<uint64_t>(searched_nodes) + retry_ctx.node_count, ProofDisproof::PROOF_MAX);
        }
    }
    catch (const DepthLimitReached&) {
        result = ProofDisproof::Unknown();
    }
    searchedNode = searched_nodes;
    return result.isCheckmateSuccess();
}

void DfPn::dfpn_stop(const bool stop) {
    impl_->stop = stop;
}

Move DfPn::dfpn_move(Position& pos) {
    return impl_->probe(pos, impl_->root_attack_color).best_move;
}

ProofDisproof DfPn::dfpn_probe(Position& pos, Move* best_move) {
    const DfpnRecord record = impl_->probe(pos, impl_->root_attack_color);
    if (best_move) {
        *best_move = record.best_move;
    }
    return record.proof_disproof;
}

ns_dfpn::ProbeRecord DfPn::dfpn_probe_record(Position& pos) {
    const DfpnRecord record = impl_->probe(pos, impl_->root_attack_color);
    ns_dfpn::ProbeRecord out;
    out.proof_disproof = record.proof_disproof;
    out.best_move = record.best_move;
    out.last_move = record.last_move;
    out.last_to = record.last_to;
    out.black_stand = record.stands[Black];
    out.white_stand = record.stands[White];
    out.proof_pieces = record.proof_pieces;
    out.board_key = board_index_key(pos);
    out.board_secondary = secondary_board_key(pos);
    out.proof_pieces_set = static_cast<int>(record.proof_pieces_set);
    out.node_count = record.node_count;
    out.min_pdp = record.min_pdp;
    out.solved = record.solved;
    out.dag_moves = record.dag_moves;
    out.need_full_width = record.need_full_width;
    out.false_branch = record.false_branch;
    out.dag_terminal = record.dag_terminal;
    return out;
}

ns_dfpn::ProbeRecord DfPn::dfpn_probe_exact_record(Position& pos) {
    const DfpnRecord* exact = impl_->table.exact(pos);
    DfpnRecord record(pos);
    if (exact) {
        record = *exact;
    }
    ns_dfpn::ProbeRecord out;
    out.proof_disproof = record.proof_disproof;
    out.best_move = record.best_move;
    out.last_move = record.last_move;
    out.last_to = record.last_to;
    out.black_stand = record.stands[Black];
    out.white_stand = record.stands[White];
    out.proof_pieces = record.proof_pieces;
    out.board_key = board_index_key(pos);
    out.board_secondary = secondary_board_key(pos);
    out.proof_pieces_set = static_cast<int>(record.proof_pieces_set);
    out.node_count = record.node_count;
    out.min_pdp = record.min_pdp;
    out.solved = record.solved;
    out.dag_moves = record.dag_moves;
    out.need_full_width = record.need_full_width;
    out.false_branch = record.false_branch;
    out.dag_terminal = record.dag_terminal;
    return out;
}

void DfPn::get_pv(Position& pos, std::vector<u32>& pv) {
    impl_->retrieve_pv(pos, true, pv);
}

void DfPn::get_pv(Position& pos, const bool is_or_node, std::vector<u32>& pv) {
    impl_->retrieve_pv(pos, is_or_node, pv);
}

int DfPn::pv_depth(Position& pos, const bool is_or_node) {
    DfPn::Impl::PvDepthTable table;
    Move best = Move::moveNone();
    const Key board_index = board_index_key(pos);
    const uint64_t board_secondary = secondary_board_key(pos);
    std::optional<OslPieceNumberState> piece_numbers = OslPieceNumberState::from_position(pos);
    return is_or_node
        ? impl_->pv_attack(pos, table, best, 0, Move::moveNone(), board_index, board_secondary,
            piece_numbers ? &*piece_numbers : nullptr)
        : impl_->pv_defense(pos, table, best, 0, Move::moveNone(), board_index, board_secondary,
            piece_numbers ? &*piece_numbers : nullptr);
}

void DfPn::set_draw_ply(const int draw_ply) {
    impl_->draw_ply = draw_ply + 1;
}

void DfPn::set_maxdepth(const int depth) {
    impl_->max_depth = depth;
}

void DfPn::set_max_search_node(const uint32_t max_search_node) {
    impl_->max_search_node = max_search_node;
}

void DfPn::set_hash(const uint64_t hashMB) {
    impl_->reserve_by_hash(hashMB);
    impl_->table.clear();
}






