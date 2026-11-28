#pragma once

#include <atomic>
#include <unordered_map>

// 置換表
namespace ns_dfpn {
    struct TTEntry {
        Hand hand; // 手駒（常に先手の手駒）
        int pn;
        int dn;
        int depth; // 局面の手数
        // 探索深さの上限(depth)による打ち切りに依存した反証か
        bool depth_limited;
        uint32_t num_searched;
        // 詰みの証明に必要な手数(pn == 0の場合)。ORノードは採用した王手の子+1、ANDノードは全応手の子の最大+1
        int mate_ply;
    };

    struct TranspositionTable {
        typedef std::vector<std::unique_ptr<TTEntry>> Cluster;

        TTEntry& LookUp(const Key key, const Hand hand, const int depth);

        TTEntry& LookUpDirect(Cluster& entries, const Hand hand, const int depth);

        template <bool or_node>
        TTEntry& LookUp(const Position& n);

        // moveを指した後の子ノードのキーを返す
        template <bool or_node>
        void GetChildFirstEntry(const Position& n, const Move move, Cluster*& entries, Hand& hand);

        // moveを指した後の子ノードの置換表エントリを返す
        template <bool or_node>
        TTEntry& LookUpChildEntry(const Position& n, const Move move);

        void NewSearch(const int64_t max_depth);

        std::unordered_map<Key, Cluster> tt;
        // 探索できる手数の上限(この手数までの局面に指せる)
        int64_t max_depth = INT_MAX;
    };
}

class DfPn
{
public:
    // 詰み探索の結果
    enum Result : int {
        Unknown = 0, // 未確定(探索ノード数の上限到達、停止など)
        Mate = 1,    // 詰み
        NoMate = 2,  // 不詰め
    };

    DfPn() {}
    DfPn(const DfPn& dfpn) : kMaxDepth(dfpn.kMaxDepth), maxSearchNode(dfpn.maxSearchNode), draw_ply(dfpn.draw_ply) {}
    DfPn(const int max_depth, const uint32_t max_search_node, const int draw_ply) {
        kMaxDepth = max_depth;
        maxSearchNode = max_search_node;
        this->draw_ply = static_cast<int64_t>(draw_ply) + 1;
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
        this->draw_ply = static_cast<int64_t>(draw_ply) + 1;
    }
    void set_maxdepth(const int depth) {
        kMaxDepth = depth;
    }
    void set_max_search_node(const uint32_t max_search_node) {
        maxSearchNode = max_search_node;
    }
    uint32_t get_searched_node() const {
        return searchedNode;
    }
    Result get_result() const {
        return result;
    }

    uint32_t searchedNode = 0;
private:
    // テストから置換表を共有した探索を行うためのアクセサ(test_cpp)
    friend struct DfPnTestAccess;

    template <bool or_node>
    void dfpn_inner(Position& n, const int thpn, const int thdn/*, bool inc_flag*/, const int64_t maxDepth, uint32_t& searchedNode);
    template<bool or_node>
    int get_pv_inner(Position& pos, std::vector<u32>& pv);
    int64_t root_max_depth(const Position& r) const;
    // 局面nからplies-1手進めた局面で指す手が、手数の上限limit(dfpn_innerのmaxDepthと同じ基準)以内か
    // (オーバーフローしないよう64ビットで比較する)
    static bool within(const Position& n, const int plies, const int64_t limit) {
        return static_cast<int64_t>(n.gamePly()) + plies <= limit;
    }

    ns_dfpn::TranspositionTable transposition_table;
    bool stop = false;
    Result result = Unknown;
    uint32_t maxSearchNode = 2097152;

    int kMaxDepth = 31;
    // 手数の上限(+1調整済み)。INT_MAXを指定しても溢れないよう64ビットで保持する
    int64_t draw_ply = INT_MAX;
};
