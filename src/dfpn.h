#pragma once

#include <atomic>
#include <unordered_map>

// 置換表
namespace ns_dfpn {
	struct TTEntry {
		Hand hand; // 手駒（常に先手の手駒）
		int pn;
		int dn;
		uint16_t depth;
		uint32_t num_searched;
	};

	struct TranspositionTable {
		typedef std::vector<std::unique_ptr<TTEntry>> Cluster;

		TTEntry& LookUp(const Key key, const Hand hand, const uint16_t depth);

		TTEntry& LookUpDirect(Cluster& entries, const Hand hand, const uint16_t depth);

		template <bool or_node>
		TTEntry& LookUp(const Position& n);

		// moveを指した後の子ノードのキーを返す
		template <bool or_node>
		void GetChildFirstEntry(const Position& n, const Move move, Cluster*& entries, Hand& hand);

		// moveを指した後の子ノードの置換表エントリを返す
		template <bool or_node>
		TTEntry& LookUpChildEntry(const Position& n, const Move move);

		void NewSearch();

		std::unordered_map<Key, Cluster> tt;
	};
}

class DfPn
{
public:
	DfPn() {}
	DfPn(const DfPn& dfpn) : kMaxDepth(dfpn.kMaxDepth), maxSearchNode(dfpn.maxSearchNode), draw_ply(dfpn.draw_ply) {}
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

	uint32_t searchedNode = 0;
private:
	template <bool or_node>
	void dfpn_inner(Position& n, const int thpn, const int thdn/*, bool inc_flag*/, const uint16_t maxDepth, uint32_t& searchedNode);
	template<bool or_node>
	int get_pv_inner(Position& pos, std::vector<u32>& pv);

	ns_dfpn::TranspositionTable transposition_table;
	bool stop = false;
	uint32_t maxSearchNode = 2097152;

	int kMaxDepth = 31;
	int draw_ply = INT_MAX;
};
