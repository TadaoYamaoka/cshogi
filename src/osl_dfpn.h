#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "move.hpp"
#include "position.hpp"

namespace ns_dfpn {
    struct ProofDisproof {
        static constexpr uint32_t PROOF_MAX = 0xffffffffu / 16;
        static constexpr uint32_t DISPROOF_MAX = 0xffffffffu / 16;
        static constexpr uint32_t NO_ESCAPE_DISPROOF = DISPROOF_MAX - 1;
        static constexpr uint32_t CHECK_MATE_DISPROOF = DISPROOF_MAX - 2;
        static constexpr uint32_t NO_CHECK_MATE_PROOF = PROOF_MAX - 1;
        static constexpr uint32_t PAWN_CHECK_MATE_PROOF = PROOF_MAX - 2;
        static constexpr uint32_t LOOP_DETECTION_PROOF = PROOF_MAX - 3;
        static constexpr uint32_t ATTACK_BACK_PROOF = PROOF_MAX - 4;
        static constexpr uint32_t DISPROOF_LIMIT = DISPROOF_MAX - 3;
        static constexpr uint32_t PROOF_LIMIT = PROOF_MAX - 5;

        uint32_t proof = 1;
        uint32_t disproof = 1;

        constexpr ProofDisproof() = default;
        constexpr ProofDisproof(uint32_t pn, uint32_t dn) : proof(pn), disproof(dn) {}

        static constexpr ProofDisproof Unknown() { return { 1, 1 }; }
        static constexpr ProofDisproof Checkmate() { return { 0, CHECK_MATE_DISPROOF }; }
        static constexpr ProofDisproof NoEscape() { return { 0, NO_ESCAPE_DISPROOF }; }
        static constexpr ProofDisproof NoCheckmate() { return { NO_CHECK_MATE_PROOF, 0 }; }
        static constexpr ProofDisproof PawnCheckmate() { return { PAWN_CHECK_MATE_PROOF, 0 }; }
        static constexpr ProofDisproof LoopDetection() { return { LOOP_DETECTION_PROOF, 0 }; }

        bool operator==(const ProofDisproof& other) const { return proof == other.proof && disproof == other.disproof; }
        bool operator!=(const ProofDisproof& other) const { return !(*this == other); }
        bool isCheckmateSuccess() const { return proof == 0; }
        bool isCheckmateFail() const { return disproof == 0; }
        bool isFinal() const { return isCheckmateSuccess() || isCheckmateFail(); }
        bool isLoopDetection() const { return proof == LOOP_DETECTION_PROOF && disproof == 0; }
    };

    struct ProbeRecord {
        ProofDisproof proof_disproof;
        Move best_move;
        Move last_move;
        Square last_to = SquareNum;
        Hand black_stand = Hand(0);
        Hand white_stand = Hand(0);
        Hand proof_pieces = Hand(0);
        uint64_t board_key = 0;
        uint64_t board_secondary = 0;
        int proof_pieces_set = 0;
        uint32_t node_count = 0;
        uint32_t min_pdp = ProofDisproof::PROOF_MAX;
        uint64_t solved = 0;
        uint64_t dag_moves = 0;
        uint8_t need_full_width = 0;
        bool false_branch = false;
        bool dag_terminal = false;
    };

}

class OslDfPn {
public:
    OslDfPn();
    OslDfPn(const OslDfPn& dfpn);
    OslDfPn(const int max_depth, const uint32_t max_search_node, const int draw_ply);
    ~OslDfPn();

    OslDfPn& operator=(const OslDfPn& r);

    bool dfpn(Position& pos);
    bool dfpn_andnode(Position& pos);
    void dfpn_stop(const bool stop);
    Move dfpn_move(Position& pos);
    ns_dfpn::ProofDisproof dfpn_probe(Position& pos, Move* best_move = nullptr);
    ns_dfpn::ProbeRecord dfpn_probe_record(Position& pos);
    ns_dfpn::ProbeRecord dfpn_probe_exact_record(Position& pos);
    void get_pv(Position& pos, std::vector<u32>& pv);
    void get_pv(Position& pos, bool is_or_node, std::vector<u32>& pv);
    int pv_depth(Position& pos, bool is_or_node);

    void set_draw_ply(const int draw_ply);
    void set_maxdepth(const int depth);
    void set_max_search_node(const uint32_t max_search_node);
    void set_hash(const uint64_t hashMB);

    uint32_t searchedNode = 0;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    void reset_config_from(const OslDfPn& other);
};

