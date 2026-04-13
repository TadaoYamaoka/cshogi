#pragma once

#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "move.hpp"
#include "position.hpp"

namespace ns_dfpn {
    constexpr int kInfinitePnDn = 100000000;
    constexpr uint16_t kAllDepth = std::numeric_limits<uint16_t>::max();

    struct TTData {
        int pn = 1;
        int dn = 1;
        int sh = 0;
    };

    struct TTState {
        TTData data;
        Hand hand;
        bool inc = false;
        bool hinc = false;
        int nouse = 0;
        int nouse2 = 0;
        bool protect = false;
        bool current = false;
    };

    class TranspositionTable {
    public:
        // Pool-based linked-list TT (matching shtsume architecture).
        // 3-level hierarchy: ZFolder (board key) -> MCard (hand) -> TList (depth).

        // Depth-specific pn/dn entry (linked list node).
        struct TList {
            TList* next;
            TTData data;
            uint16_t dp;  // depth from root (kAllDepth for resolved)
        };

        // Hand-piece card (linked list node within a ZFolder).
        struct MCard {
            MCard* next;
            TList* tlist;
            Hand hand;         // attacker's hand pieces (mkey equivalent)
            bool hinc;
            int nouse;
            bool current;
            bool protect;
            int current_pn;

            TList* findRecord(uint16_t ply) const {
                for (TList* t = tlist; t; t = t->next) {
                    if (t->dp == ply) return t;
                }
                return nullptr;
            }
            TList* findResolved() const { return findRecord(kAllDepth); }
        };

        // Board-key folder (linked list node within a hash bucket).
        struct ZFolder {
            Key key;           // zobrist board key
            ZFolder* next;
            MCard* mcard;
        };

        struct ProbeResult {
            TTState state;
            bool cutoff = false;
        };

        TranspositionTable();
        explicit TranspositionTable(uint64_t hashMB);
        ~TranspositionTable();

        void Resize(uint64_t hashMB);
        void NewSearch();

        template <bool or_node>
        ProbeResult Probe(const Position& pos);

        template <bool or_node>
        ProbeResult ProbeChild(const Position& pos, Move move);

        template <bool or_node>
        void SetCurrent(const Position& pos, int current_pn);

        template <bool or_node>
        void ClearCurrent(const Position& pos);

        void ClearProtect();

        // Pool usage reporting
        uint64_t getPoolSize() const { return poolSize_; }
        uint64_t getZUsed() const { return zNext_; }
        uint64_t getMUsed() const { return mNext_; }
        uint64_t getTUsed() const { return tNext_; }

        template <bool or_node>
        void SetProtect(const Position& pos);

        TTState Store(Key key, Hand query_hand, uint16_t ply, const TTState& state);

        bool ProbeIsMate(Key boardKey, Hand hand, uint16_t ply);
        int ProbePn(Key boardKey, Hand hand, uint16_t ply);

    private:
        enum class Relation {
            None,
            Equal,
            Super,
            Infer,
        };

        enum class CardKind {
            Unknown,
            Mate,
            NoMate,
        };

        ProbeResult Probe(Key key, Hand hand, uint16_t ply);

        MCard* EnsureCard(ZFolder* folder, Hand hand, uint16_t ply);
        static Relation Compare(Hand lhs, Hand rhs);
        static CardKind Kind(const MCard* card);
        static bool PreferSuperiorMate(const Hand& current, const Hand& candidate);
        static bool PreferInferiorNoMate(const Hand& current, const Hand& candidate);

        TTState StoreMate(ZFolder* folder, Hand query_hand, const TTState& state);
        TTState StoreNoMate(ZFolder* folder, Hand query_hand, const TTState& state);
        TTState StoreUnknown(ZFolder* folder, Hand query_hand, uint16_t ply, const TTState& state);

        template <bool or_node>
        static Hand MakeHand(const Position& pos);

        template <bool or_node>
        static Hand MakeChildHand(const Position& pos, Move move);

        ZFolder* findFolder(Key key);
        ZFolder* findOrCreateFolder(Key key);

        // Pool allocators
        TList* allocTList();
        MCard* allocMCard();
        ZFolder* allocZFolder();
        void freeTList(TList* tlist);   // free a chain of TList nodes
        void freeMCard(MCard* mcard);   // free single MCard (tlist must be freed separately)
        void freeZFolder(ZFolder* zf);  // free single ZFolder

        void Allocate(uint64_t hashMB);
        void Deallocate();

        // Pool sizing: shtsume MCARDS_PER_MBYTE=16384
        static constexpr uint64_t kDefaultHashMB = 256;
        static constexpr uint64_t kMCardsPerMB = 16384;

        uint64_t poolSize_;     // number of elements per pool
        uint64_t tableSize_;    // number of hash buckets (power of 2)
        uint64_t tableMask_;    // tableSize_ - 1

        ZFolder** table_;       // hash index array (pointers)
        ZFolder* zPool_;        // zfolder pool base
        ZFolder* zStack_;       // zfolder free list head (for returned nodes)
        uint64_t zNext_;        // next fresh zfolder index
        MCard* mPool_;          // mcard pool base
        MCard* mStack_;         // mcard free list head (for returned nodes)
        uint64_t mNext_;        // next fresh mcard index
        TList* tPool_;          // tlist pool base
        TList* tStack_;         // tlist free list head (for returned nodes)
        uint64_t tNext_;        // next fresh tlist index
        uint64_t num_;          // allocated count
    };

    // -----------------------------------------------------------------
    // Linked-list nodes matching shtsume's mlist_t / mvlist_t
    // -----------------------------------------------------------------

    // MList: singly-linked list of moves within one group (shtsume mlist_t)
    struct MList {
        Move move;
        MList* next;
    };

    // MvList: singly-linked list of move-groups (shtsume mvlist_t)
    // Used for both OR children (single move each) and AND groups (mlist chain)
    struct MvList {
        MList* mlist;       // grouped moves (OR: single, AND: chain)
        MvList* next;       // next sibling
        TTState state;      // pn/dn/sh/hand/inc/etc.
        bool searched;
        int length;

        Move headMove() const {
            assert(mlist);
            return mlist->move;
        }
    };

    // Pool allocator for MList and MvList nodes
    struct MvListPool {
        static constexpr size_t kBlockSize = 8192;

        MList* mlStack = nullptr;   // free list
        MvList* mvStack = nullptr;  // free list

        // Allocated blocks for cleanup
        std::vector<MList*> mlBlocks;
        std::vector<MvList*> mvBlocks;

        MList* allocMList() {
            if (mlStack) {
                MList* n = mlStack;
                mlStack = n->next;
                return n;
            }
            MList* block = new MList[kBlockSize];
            mlBlocks.push_back(block);
            for (size_t i = 1; i < kBlockSize; ++i) {
                block[i].next = mlStack;
                mlStack = &block[i];
            }
            return &block[0];
        }

        MvList* allocMvList() {
            if (mvStack) {
                MvList* n = mvStack;
                mvStack = n->next;
                n->mlist = nullptr;
                n->next = nullptr;
                n->state = TTState{};
                n->searched = false;
                n->length = 0;
                return n;
            }
            MvList* block = new MvList[kBlockSize];
            mvBlocks.push_back(block);
            for (size_t i = 1; i < kBlockSize; ++i) {
                block[i].next = mvStack;
                mvStack = &block[i];
            }
            auto* n = &block[0];
            n->mlist = nullptr;
            n->next = nullptr;
            n->state = TTState{};
            n->searched = false;
            n->length = 0;
            return n;
        }

        void freeMList(MList* list) {
            while (list) {
                MList* t = list;
                list = list->next;
                t->next = mlStack;
                mlStack = t;
            }
        }

        // Free entire MvList chain (including mlist chains)
        void freeMvList(MvList* list) {
            while (list) {
                MvList* t = list;
                list = list->next;
                freeMList(t->mlist);
                t->mlist = nullptr;
                t->next = mvStack;
                mvStack = t;
            }
        }

        // Add a move to front of mlist chain, return new MList*
        MList* mlistAdd(MList* head, Move move) {
            MList* n = allocMList();
            n->move = move;
            n->next = head;
            return n;
        }

        // Get last node of mlist chain
        static MList* mlistLast(MList* head) {
            while (head->next) head = head->next;
            return head;
        }

        // Create a new MvList node with a single move
        MvList* mvlistAdd(MvList* head, Move move) {
            MvList* n = allocMvList();
            n->mlist = allocMList();
            n->mlist->move = move;
            n->mlist->next = nullptr;
            n->next = head;
            return n;
        }

        // Count items in mlist chain
        static int mlistCount(const MList* head) {
            int n = 0;
            while (head) { ++n; head = head->next; }
            return n;
        }

        // Count items in mvlist chain
        static int mvlistLength(const MvList* head) {
            int n = 0;
            while (head) { ++n; head = head->next; }
            return n;
        }

        ~MvListPool() {
            for (auto* p : mlBlocks) delete[] p;
            for (auto* p : mvBlocks) delete[] p;
        }

        MvListPool() = default;
        MvListPool(const MvListPool&) = delete;
        MvListPool& operator=(const MvListPool&) = delete;
    };
}

class DfPn
{
public:
    DfPn() = default;
    DfPn(const DfPn& dfpn)
        : maxSearchNode(dfpn.maxSearchNode)
        , kMaxDepth(dfpn.kMaxDepth)
        , draw_ply(dfpn.draw_ply) {}
    DfPn(const int max_depth, const uint32_t max_search_node, const int draw_ply) {
        kMaxDepth = max_depth;
        maxSearchNode = max_search_node;
        this->draw_ply = draw_ply + 1;
    }
    DfPn& operator =(const DfPn& r) {
        kMaxDepth = r.kMaxDepth;
        maxSearchNode = r.maxSearchNode;
        draw_ply = r.draw_ply;
        return *this;
    }

    bool dfpn(Position& r);
    bool dfpn_andnode(Position& r);
    void dfpn_stop(const bool stop);
    Move dfpn_move(Position& pos);
    void get_pv(Position& pos, std::vector<u32>& pv);

    void set_draw_ply(const int draw_ply) {
        // WCSCのルールでは、最大手数で詰ました場合は勝ちになるため+1する
        this->draw_ply = draw_ply + 1;
    }
    void set_maxdepth(const int depth) {
        kMaxDepth = depth;
    }
    void set_max_search_node(const uint32_t max_search_node) {
        maxSearchNode = max_search_node;
    }
    void set_hash(const uint64_t hashMB) {
        transposition_table.Resize(hashMB);
    }

    uint32_t searchedNode = 0;

private:
    struct Threshold {
        int pn;
        int dn;
        int sh;
    };

    struct NodeState {
        int pn = 1;
        int dn = 1;
        int sh = 0;
        Hand hand;
        bool inc = false;
        bool hinc = false;
        int nouse = 0;
        int nouse2 = 0;
        bool protect = false;
        bool current = false;
    };

    template <bool or_node>
    NodeState finalizeNode(Position& pos, Hand query_hand, const NodeState& state);

    template <bool or_node>
    NodeState dfpn_inner(Position& pos, Threshold threshold, uint16_t maxDepth, uint32_t& searchedNode);

    template <bool or_node>
    NodeState make_tree_inner(Position& pos, const NodeState& base, uint16_t maxDepth, uint32_t& searchedNode);

    template <bool or_node>
    NodeState bns_plus_inner(Position& pos, const NodeState& base, uint16_t maxDepth, uint32_t& searchedNode, int addThPn, int ptsh = -1);

    template <bool or_node>
    int get_pv_inner(Position& pos, std::vector<u32>& pv);

    ns_dfpn::TranspositionTable transposition_table;
    ns_dfpn::MvListPool mvListPool;
    bool stop = false;
    uint32_t maxSearchNode = std::numeric_limits<uint32_t>::max();
    int kMaxDepth = 2000;
    int draw_ply = INT_MAX;
    int rootPly = 0;
    int maxThPn = 0;
    int searchLevel = 0;  // g_search_level equivalent (default 0)
    bool prevCheckerIsHorse = false;  // Set by AND node for distance-based pn init in child OR node
};
