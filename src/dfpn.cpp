#include "dfpn.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

#include "generateMoves.hpp"

using namespace ns_dfpn;

namespace {
    constexpr int kInfinite = ns_dfpn::kInfinitePnDn;
    constexpr size_t kMaxCheckMoves = 91;
    constexpr int kPreProofMax = 240;
    constexpr int kPreDisproofMax = 1024;

    // Proof refinement constants matching shtsume defaults.
    constexpr int kNMakeTree = 2;       // g_n_make_tree  (MAKE_REPEAT_DEFAULT)
    constexpr int kMtMinPn = 4;         // g_mt_min_pn    (LEAF_PN_DEFAULT)
    constexpr int kMakeTreePnPlus = 1;  // MAKE_TREE_PN_PLUS
    constexpr int kAddSearchSh = 30;    // ADD_SEARCH_SH
    constexpr int kProofMax = 256;      // PROOF_MAX
    constexpr int kDisproofMax = 2048;  // DISPROOF_MAX
    constexpr int kRootDnThreshold = kInfinite - 1; // shtsume: INFINATE-1 (matches special-case path)

    // Forward declarations for functions used by interposition analysis
    bool sliderCheckInfo(const Position& pos, Square& checkerSq, Square& kingSq, PieceType& checkerPt);
    bool isInterpositionEvasion(const Position& pos, const Move move);
    bool isKingMove(const Position& pos, const Move move);
    SquareDelta stepTowards(const Square from, const Square to);
    int totalHand(const Hand& hand);

    // Compute the board key of a hypothetical position where an attacker piece
    // at src is moved to dest (non-capture). The attacker's color determines
    // the piece's Zobrist contribution. The turn is NOT flipped (matching
    // shtsume's sdata_tentative_move which preserves turn).
    Key computeTentativeBoardKey(const Position& pos, const Square src,
        const Square dest, const bool promote)
    {
        const Color attacker = oppositeColor(pos.turn());
        const PieceType pt = pieceToPieceType(pos.piece(src));
        const PieceType destPt = promote ? (pt + PTPromote) : pt;
        Key key = pos.getBoardKey();
        // Remove piece from src
        key -= Position::getZobrist(pt, src, attacker);
        // Place (possibly promoted) piece at dest
        key += Position::getZobrist(destPt, dest, attacker);
        return key;
    }

    // -----------------------------------------------------------------------
    // Futile interposition detection (無駄合い判定)
    //
    // Based on shtsume nevasion.c / ntbase.c generate_evasion().
    // Prunes futile interposition moves and computes proof_flag/invalid_flag.
    //
    //   proof_flag (g_invalid_drops): set via TT-proven path in
    //     invalidDrops → ProbeIsMate (hs_tbase_lookup equivalent).
    //   invalid_flag (g_invalid_moves): set when valid_next/valid_long detect
    //     reverse-check risk with no attacker hand pieces.
    //
    // n==1 drops: invalidDrops() — isMateByTentativeMoveBB (tsumi_check
    //   equivalent using bitboard analysis) + TT probe for promoted variants.
    //   tpin via attackersTo comparison. Hand composition preserved.
    // n>=2 drops: if prior legal moves (mvlist || move_list) → keep.
    //   Otherwise: is_dest_effect_valid (unpinned defender effect on dest).
    //   At n==2: also cross_point_check (bishop/rook cross + TT probe).
    //   Otherwise: unconditional prune (matching shtsume).
    // n==1 moves (Pattern B): validNext() —
    //   direct check on enemy king, reverse check (with SELF_EFFECT guard),
    //   invalid_drops on hypothetical (modOcc, hypPinned, excludeDefSq,
    //   defenderHasHand=true matching sdata_pickup_table flag=false).
    // n>=2 moves (Pattern B): validLong() —
    //   king escape, direct check on enemy king, checkBackRisk (collinearity
    //   only), unpinned defender effect (modOcc/hypPinned), all defender
    //   sliders attacking src + isMateByTentativeMoveBB (with excludeDefSq).
    //
    // crossPointCheck: matches shtsume's turn-dependent mate check:
    //   後手番 (gote defending): isKingMoveOnlyMate (ou_move_check equivalent)
    //   先手番 (sente defending): isMateByTentativeMoveBB (tsumi_check approx)
    //
    // Differences vs shtsume:
    //   - isMateByTentativeMoveBB approximates tsumi_check using bitboard
    //     analysis on the original Position with overlay parameters
    //     (baseOcc, excludeDefSq, defenderHasHand).
    //     Section (c) hand-drop check uses canDropAtSquare (HAND_CHECK
    //     equivalent) for nifu/rank constraint matching.
    //   - hs_tbase_lookup: ProbeIsMate covers _hs_tbase_lookup semantics;
    //     GC re-expansion path is not applicable (different TT architecture).
    //   - Group structure: flat array vs shtsume's linked-list.
    // -----------------------------------------------------------------------

    struct EvasionFilterResult {
        bool proofFlag = false;
        bool invalidFlag = false;
    };

    // Compute pinnedBB of position as if the piece at 'excluded' were removed.
    // This mirrors shtsume's create_pin() on the hypothetical sbuf where
    // sdata_pickup_table has removed a piece.
    Bitboard pinnedBBWithout(const Position& pos, const Square excluded) {
        Bitboard result = allZeroBB();
        const Color us = pos.turn();
        const Color them = oppositeColor(us);
        const Square ksq = pos.kingSquare(us);

        // Attacker sliders that could pin defender pieces to defender's king
        Bitboard pinners = pos.bbOf(them);
        pinners &= (pos.bbOf(Lance) & lanceAttackToEdge(us, ksq)) |
            (pos.bbOf(Rook, Dragon) & rookAttackToEdge(ksq)) |
            (pos.bbOf(Bishop, Horse) & bishopAttackToEdge(ksq));

        // Modified occupancy: excluded square is empty
        Bitboard occ = pos.occupiedBB();
        occ.clearBit(excluded);

        while (pinners.isAny()) {
            const Square sq = pinners.firstOneFromSQ11();
            Bitboard between = betweenBB(sq, ksq) & occ;
            if (between
                && between.isOneBit<false>()
                && between.andIsAny(pos.bbOf(us)))
            {
                result |= between;
            }
        }
        return result;
    }

    // Whether a piece type can promote.
    bool canPromoteType(const PieceType pt) {
        return pt >= Pawn && pt <= Rook && pt != Gold;
    }

    // Whether a piece MUST promote when moving to dest (forced promotion).
    // Matches shtsume's HAND_CHECK(sdata, dest).
    // Returns true if the defender can legally drop any hand piece at sq.
    // Checks nifu, rank constraints for pawn/lance/knight.
    bool canDropAtSquare(const Position& pos, const Color defender, const Square sq) {
        const Hand hand = pos.hand(defender);
        // Rook, Bishop, Gold, Silver: no rank restrictions
        if (hand.exists<HRook>() || hand.exists<HBishop>() ||
            hand.exists<HGold>() || hand.exists<HSilver>())
            return true;
        const Rank r = makeRank(sq);
        if (defender == Black) {
            // Knight: cannot drop on Rank1-2
            if (hand.exists<HKnight>() && r >= Rank3) return true;
            // Lance: cannot drop on Rank1
            if (hand.exists<HLance>() && r >= Rank2) return true;
            // Pawn: cannot drop on Rank1 + nifu
            if (hand.exists<HPawn>() && r >= Rank2) {
                if (!(pos.bbOf(Pawn, defender) & fileMask(makeFile(sq))).isAny())
                    return true;
            }
        }
        else {
            // Knight: cannot drop on Rank8-9
            if (hand.exists<HKnight>() && r <= Rank7) return true;
            // Lance: cannot drop on Rank9
            if (hand.exists<HLance>() && r <= Rank8) return true;
            // Pawn: cannot drop on Rank9 + nifu
            if (hand.exists<HPawn>() && r <= Rank8) {
                if (!(pos.bbOf(Pawn, defender) & fileMask(makeFile(sq))).isAny())
                    return true;
            }
        }
        return false;
    }

    bool mustPromoteAtDest(const Color c, const PieceType pt, const Square to) {
        const Rank r = makeRank(to);
        if (c == Black) {
            return (pt == Pawn && r == Rank1) || (pt == Lance && r == Rank1) || (pt == Knight && r <= Rank2);
        }
        else {
            return (pt == Pawn && r == Rank9) || (pt == Lance && r == Rank9) || (pt == Knight && r >= Rank8);
        }
    }

    // Check if an attacker piece moving from src to dest (hypothetically)
    // gives check on the defender's king AND the resulting position is static
    // checkmate (no legal evasion exists).
    //
    // This uses pure bitboard analysis to avoid modifying the Position:
    //   1) Check if piece at dest attacks the defender's king (using modified occ)
    //   2) Check if the defender's king has any escape in the hypothetical
    //   3) Check if any defender piece blocks or captures the attacker at dest
    //
    // Equivalent to shtsume's tsumi_check (base.c).
    // King escape (a): recomputes all attacker effects with king-absent occ
    //   per escape square (slightly stricter than shtsume which only recomputes
    //   the checker's effect with king absent).
    // Double check: S_NOUTE>1 equivalent via discovered checker detection.
    // Capture/block (b) and interposition (c): overlay-based attackersTo with
    //   newOcc (src cleared, dest set) + newPinned (recomputed on newOcc,
    //   ghost at src excluded from pinners).  The moved piece at dest is not
    //   in pos.bbOf(attacker) but is not needed as a pinner because the check
    //   line (dest→kingSq) is clear — otherwise no check.
    // Hand drops in interposition: canDropAtSquare (HAND_CHECK equivalent).
    // baseOcc: the occupancy BEFORE the tentative move (src→dest).
    //   Default overload uses pos.occupiedBB().
    //   When called from validNext's invalidDrops, the interposition piece at
    //   interpSrc has already been removed, so baseOcc has interpSrc cleared.
    // excludeDefSq: a defender-side square that has been conceptually removed
    //   (e.g. the interposition piece's src in validLong/validNext context).
    //   pos.bbOf(defender) still includes it, so we explicitly exclude it from
    //   king escape mask and defender blocker/interposer enumeration.
    //   Default SquareNum means no exclusion.
    // defenderHasHand: when true, the defender is known to have hand pieces in
    //   the hypothetical (e.g. validNext's sdata_pickup_table flag=false adds
    //   the picked-up piece to defender's hand). Overrides pos.hand(defender).
    bool isMateByTentativeMoveBB(const Position& pos, const PieceType pt, const Square src,
        const Square dest, const bool promote,
        const Bitboard& baseOcc,
        const Square excludeDefSq = SquareNum,
        const bool defenderHasHand = false)
    {
        const Color defender = pos.turn();
        const Color attacker = oppositeColor(defender);
        const Square kingSq = pos.kingSquare(defender);

        // Compute the piece type after potential promotion
        const PieceType movedPt = promote ? (pt + PTPromote) : pt;

        // Modified occupancy: piece moved from src to dest (non-capture, dest was empty)
        Bitboard newOcc = baseOcc;
        newOcc.clearBit(src);
        newOcc.setBit(dest);

        // Check if the moved piece at dest attacks the king
        const Bitboard pieceAttack = Position::attacksFrom(movedPt, attacker, dest, newOcc);
        if (!pieceAttack.isSet(kingSq)) return false;

        // Defender occupancy mask: exclude phantom defender if specified
        Bitboard defPieces = pos.bbOf(defender);
        if (excludeDefSq != SquareNum) defPieces.clearBit(excludeDefSq);

        // The piece gives check. Now check for evasion options:

        // (a) King escape: king moves to a square not attacked by attacker
        {
            const Bitboard kingMoves = pos.attacksFrom<King>(kingSq);
            Bitboard escapes = kingMoves;
            escapes.andEqualNot(defPieces); // can't move to own piece
            while (escapes.isAny()) {
                const Square esc = escapes.firstOneFromSQ11();
                // Modified occ for king move: king leaves kingSq, moves to esc
                Bitboard escOcc = newOcc;
                escOcc.clearBit(kingSq);
                escOcc.setBit(esc);
                // Check if moved piece at dest attacks esc
                const Bitboard movedAtk = Position::attacksFrom(movedPt, attacker, dest, escOcc);
                if (movedAtk.isSet(esc)) continue;
                // Check other attacker pieces, excluding ghost at src
                // (piece has moved from src to dest, but pos.bbOf still has it at src)
                Bitboard otherAttackers = pos.attackersTo(attacker, esc, escOcc);
                otherAttackers.clearBit(src); // remove ghost
                if (otherAttackers.isAny()) continue;
                return false; // King can escape
            }
        }

        // Double check detection (shtsume: if(S_NOUTE(sdata)>1) return true).
        // Moving the piece from src may open a discovered check from an
        // attacker slider behind src. If so, only king moves can evade
        // (already failed above), so it's mate.
        // pos.attackersTo uses pos.bbOf which still has the piece at src
        // (ghost), so we clear it. The direct checker at dest is NOT in
        // pos.bbOf(attacker) at dest, so attackersTo won't count it.
        // Any remaining attacker = discovered checker = double check.
        {
            Bitboard otherCheckers = pos.attackersTo(attacker, kingSq, newOcc);
            otherCheckers.clearBit(src); // ghost removal
            if (otherCheckers.isAny()) {
                return true; // double check → mate
            }
        }

        // Compute pinned BB for the hypothetical position after the move.
        // Pin = attacker slider -> exactly one defender piece -> defender king.
        // Use newOcc (src cleared, dest set). Exclude ghost at src from pinners.
        Bitboard newPinned = allZeroBB();
        {
            Bitboard pinners = pos.bbOf(attacker);
            pinners.clearBit(src); // ghost: piece no longer at src
            pinners &= (pos.bbOf(Lance) & lanceAttackToEdge(defender, kingSq)) |
                (pos.bbOf(Rook, Dragon) & rookAttackToEdge(kingSq)) |
                (pos.bbOf(Bishop, Horse) & bishopAttackToEdge(kingSq));
            while (pinners.isAny()) {
                const Square sq = pinners.firstOneFromSQ11();
                const Bitboard between = betweenBB(sq, kingSq) & newOcc;
                if (between
                    && between.isOneBit<false>()
                    && between.andIsAny(pos.bbOf(defender)))
                {
                    newPinned |= between;
                }
            }
        }

        // (b) Block or capture the checking piece at dest
        {
            Bitboard blockers = pos.attackersTo(defender, dest, newOcc);
            blockers.clearBit(kingSq);
            if (excludeDefSq != SquareNum) blockers.clearBit(excludeDefSq);
            while (blockers.isAny()) {
                const Square bSq = blockers.firstOneFromSQ11();
                if (!newPinned.isSet(bSq)) return false;
            }
        }

        // (c) Interpose between dest and kingSq (for slider checks)
        if (movedPt == Lance || movedPt == Bishop || movedPt == Rook ||
            movedPt == Horse || movedPt == Dragon)
        {
            const Bitboard between = betweenBB(dest, kingSq);
            if (between.isAny()) {
                Bitboard interposerBB = between;
                while (interposerBB.isAny()) {
                    const Square iSq = interposerBB.firstOneFromSQ11();
                    Bitboard interposers = pos.attackersTo(defender, iSq, newOcc);
                    interposers.clearBit(kingSq);
                    if (excludeDefSq != SquareNum) interposers.clearBit(excludeDefSq);
                    while (interposers.isAny()) {
                        const Square ipSq = interposers.firstOneFromSQ11();
                        if (!newPinned.isSet(ipSq)) return false;
                    }
                    if (defenderHasHand || canDropAtSquare(pos, defender, iSq)) {
                        return false;
                    }
                }
            }
        }

        // No evasion found
        return true;
    }

    // Convenience overload using original position's occupancy.
    bool isMateByTentativeMoveBB(const Position& pos, const PieceType pt, const Square src,
        const Square dest, const bool promote)
    {
        return isMateByTentativeMoveBB(pos, pt, src, dest, promote,
            pos.occupiedBB());
    }

    // Equivalent of shtsume's invalid_drops().
    // Checks whether ANY attacker piece can move to dest (hypothetically)
    // giving check-and-static-checkmate (or TT-proven mate).
    //
    // Unlike the old doMove-based approach, this does NOT modify the Position,
    // so the defender's hand is fully preserved (matching shtsume's semantics).
    // The tpin check is handled by checking if the attacker piece is pinned
    // to the attacker's king before testing (using discoveredCheck analogy).
    //
    // When tt is non-null, the promote path also checks the TT for proven
    // mate (matching shtsume's hs_tbase_lookup in _invalid_drops). If mate
    // is found via TT, proofFlag is set to true.
    bool invalidDrops(const Position& pos, const Square dest,
        TranspositionTable* tt, bool& proofFlag) {
        const Color defender = pos.turn();
        const Color attacker = oppositeColor(defender);
        const Square attackerKing = pos.kingSquare(attacker);

        Bitboard attackers = pos.attackersTo(attacker, dest);

        while (attackers.isAny()) {
            const Square src = attackers.firstOneFromSQ11();

            // tpin check: skip attacker pieces pinned to their own king.
            // Equivalent of shtsume set_tpin(): if removing this piece from
            // src would expose the attacker's king to a new defender slider
            // attack, the piece is "tpin'd" and cannot move.
            if (isInSquare(attackerKing)) {
                Bitboard occ = pos.occupiedBB();
                occ.clearBit(src);
                Bitboard newDefAttacks = pos.attackersTo(defender, attackerKing, occ);
                Bitboard origDefAttacks = pos.attackersTo(defender, attackerKing);
                if (newDefAttacks != origDefAttacks) {
                    continue; // This piece is tpin'd
                }
            }

            const PieceType pt = pieceToPieceType(pos.piece(src));
            const bool forced = mustPromoteAtDest(attacker, pt, dest);
            const bool promotable = canPromoteType(pt) && canPromote(attacker, src, dest);

            if (!forced) {
                if (isMateByTentativeMoveBB(pos, pt, src, dest, false)) return true;
            }
            if (promotable) {
                if (isMateByTentativeMoveBB(pos, pt, src, dest, true)) return true;
                // shtsume _invalid_drops: for the promoted variant, if static
                // mate check fails, probe the TT (hs_tbase_lookup). If the
                // hypothetical position is proven mate in TT → futile.
                if (tt) {
                    const Key hypKey = computeTentativeBoardKey(pos, src, dest, true);
                    // Hand: attacker's hand (the side to prove mate).
                    // In the hypothetical, no capture occurs, so hand is unchanged.
                    const Hand attackerHand = pos.hand(attacker);
                    if (tt->ProbeIsMate(hypKey, attackerHand,
                        static_cast<uint16_t>(pos.gamePly())))
                    {
                        proofFlag = true;
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // Overload: invalidDrops with hypothetical occupancy.
    // Used from validNext step 3 where the interposition piece has been
    // conceptually removed from the board (pickup_table equivalent).
    // baseOcc: occupancy with the interposition src cleared.
    // excludeDefSq: defender square removed from the board.
    // defenderHasHand: true when the picked-up piece goes to defender's hand
    //   (validNext: sdata_pickup_table flag=false).
    bool invalidDrops(const Position& pos, const Square dest,
        TranspositionTable* tt, bool& proofFlag,
        const Bitboard& baseOcc,
        const Square excludeDefSq, const bool defenderHasHand) {
        const Color defender = pos.turn();
        const Color attacker = oppositeColor(defender);
        const Square attackerKing = pos.kingSquare(attacker);

        // Use hypothetical occ to find attackers (sliders through cleared
        // interposition src can now reach dest)
        Bitboard attackers = pos.attackersTo(attacker, dest, baseOcc);

        while (attackers.isAny()) {
            const Square src = attackers.firstOneFromSQ11();

            // tpin check using hypothetical occ
            if (isInSquare(attackerKing)) {
                Bitboard occ = baseOcc;
                occ.clearBit(src);
                Bitboard newDefAttacks = pos.attackersTo(defender, attackerKing, occ);
                Bitboard origDefAttacks = pos.attackersTo(defender, attackerKing, baseOcc);
                if (newDefAttacks != origDefAttacks) {
                    continue; // This piece is tpin'd
                }
            }

            const PieceType pt = pieceToPieceType(pos.piece(src));
            const bool forced = mustPromoteAtDest(attacker, pt, dest);
            const bool promotable = canPromoteType(pt) && canPromote(attacker, src, dest);

            if (!forced) {
                if (isMateByTentativeMoveBB(pos, pt, src, dest, false, baseOcc, excludeDefSq, defenderHasHand)) return true;
            }
            if (promotable) {
                if (isMateByTentativeMoveBB(pos, pt, src, dest, true, baseOcc, excludeDefSq, defenderHasHand)) return true;
                if (tt) {
                    const Key hypKey = computeTentativeBoardKey(pos, src, dest, true);
                    const Hand attackerHand = pos.hand(attacker);
                    if (tt->ProbeIsMate(hypKey, attackerHand,
                        static_cast<uint16_t>(pos.gamePly())))
                    {
                        proofFlag = true;
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // Equivalent of shtsume's is_dest_effect_valid().
    // Returns true if a non-king, non-pinned defender piece attacks dest.
    bool hasUnpinnedDefenderEffect(const Position& pos, const Square dest) {
        const Color us = pos.turn();
        Bitboard defenders = pos.attackersToExceptKing(us, dest);
        const Bitboard pinned = pos.pinnedBB();
        while (defenders.isAny()) {
            const Square src = defenders.firstOneFromSQ11();
            if (!pinned.isSet(src)) return true;
        }
        return false;
    }

    // King-move-only mate check for a tentative move (src→dest).
    // Matches shtsume's ou_move_check: only checks if the defender's king
    // has any safe escape square. Does NOT check capture/block/interpose.
    // Used by crossPointCheck for 後手番 (gote defending) to match shtsume.
    bool isKingMoveOnlyMate(const Position& pos, const PieceType pt,
        const Square src, const Square dest, const bool promote)
    {
        const Color defender = pos.turn();
        const Color attacker = oppositeColor(defender);
        const Square kingSq = pos.kingSquare(defender);
        const PieceType movedPt = promote ? (pt + PTPromote) : pt;

        Bitboard newOcc = pos.occupiedBB();
        newOcc.clearBit(src);
        newOcc.setBit(dest);

        const Bitboard pieceAttack = Position::attacksFrom(movedPt, attacker, dest, newOcc);
        if (!pieceAttack.isSet(kingSq)) return false;

        const Bitboard kingMoves = pos.attacksFrom<King>(kingSq);
        Bitboard escapes = kingMoves;
        escapes.andEqualNot(pos.bbOf(defender));
        while (escapes.isAny()) {
            const Square esc = escapes.firstOneFromSQ11();
            Bitboard escOcc = newOcc;
            escOcc.clearBit(kingSq);
            escOcc.setBit(esc);
            const Bitboard movedAtk = Position::attacksFrom(movedPt, attacker, dest, escOcc);
            if (movedAtk.isSet(esc)) continue;
            Bitboard otherAttackers = pos.attackersTo(attacker, esc, escOcc);
            otherAttackers.clearBit(src);
            if (otherAttackers.isAny()) continue;
            return false; // King can escape
        }
        return true; // No escape
    }

    // Equivalent of shtsume's cross_point_check().
    // At n==2, if attacker's bishop/horse attacks dest AND the checking piece
    // is a non-promoting rook or lance, the diagonal and line cross at dest,
    // creating potential escape opportunities after interposition.
    //
    // shtsume creates a hypothetical (attacker moves checker to dest,
    // non-promoted), then:
    //   1) ou_move_check (後手番) / tsumi_check (先手番) — if mate → false
    //   2) tbase_lookup — if pn > 0 → return true (default pn=1)
    bool crossPointCheck(const Position& pos, const Square dest,
        const Square checkerSq, const PieceType checkerPt,
        TranspositionTable* tt)
    {
        const Color defender = pos.turn();
        const Color attacker = oppositeColor(defender);
        // Check if attacker has bishop/horse that attacks dest
        Bitboard bishopHorse = (pos.bbOf(Bishop) | pos.bbOf(Horse)) & pos.bbOf(attacker);
        Bitboard attackBB = pos.attacksFrom<Bishop>(dest, pos.occupiedBB());
        bishopHorse &= attackBB;
        if (!bishopHorse.isAny()) return false;

        // Check if the checking piece is a non-promoting rook or lance
        bool condition = false;
        if (checkerPt == Lance) {
            condition = true;
        }
        else if (checkerPt == Rook && !canPromote(attacker, checkerSq, dest)) {
            condition = true;
        }
        if (!condition) return false;

        // Step 1: Mate check on hypothetical (checker→dest, non-promoted).
        // shtsume: ou_move_check for 後手番 (king escape only),
        //          tsumi_check for 先手番 (full: escape + capture + interpose).
        bool isMate;
        if (defender == White) {
            // 後手番: ou_move_check — king escape only (more aggressive prune)
            isMate = isKingMoveOnlyMate(pos, checkerPt, checkerSq, dest, false);
        }
        else {
            // 先手番: tsumi_check — full check
            isMate = isMateByTentativeMoveBB(pos, checkerPt, checkerSq, dest, false);
        }
        if (isMate) {
            return false; // Checkmate → futile interposition
        }

        // Step 2: TT probe on the hypothetical (non-promoted).
        // shtsume: tbase_lookup(hypothetical, mvlist, turn, tbase);
        //          if (mvlist.tdata.pn) return true;
        // When TT has no entry, _tbase_lookup defaults to pn=1 → return true.
        if (tt) {
            const Key hypKey = computeTentativeBoardKey(pos, checkerSq, dest, false);
            const Hand attackerHand = pos.hand(attacker);
            const int pn = tt->ProbePn(hypKey, attackerHand,
                static_cast<uint16_t>(pos.gamePly()));
            if (pn == 0) return false;  // TT confirmed mate → futile
            return true;                // pn > 0 or no entry → valid
        }

        // No TT available: default to valid (matching shtsume's pn=1 default)
        return true;
    }

    // Check whether removing a piece from src could create a discovery
    // (reverse check) on the attacker's king through a defender slider.
    // Equivalent of shtsume's reverse-check detection in valid_next().
    // Checks: slider can reach src (path clear) + collinearity with enemyKing.
    // Does NOT check src→king path (matching shtsume, which also does not).
    bool hasReverseCheckRisk(const Position& pos, const Square src) {
        const Color us = pos.turn(); // defender
        const Color them = oppositeColor(us);
        const Square enemyKing = pos.kingSquare(them);
        if (!isInSquare(enemyKing)) return false;
        const Direction direc = squareRelation(src, enemyKing);
        if (direc == DirecMisc) return false;

        const Bitboard occ = pos.occupiedBB();
        if (direc & DirecCross) {
            Bitboard sliders = (pos.bbOf(Rook) | pos.bbOf(Dragon)) & pos.bbOf(us);
            if (direc == DirecFile) {
                sliders |= pos.bbOf(Lance, us);
            }
            while (sliders.isAny()) {
                const Square sl = sliders.firstOneFromSQ11();
                if (squareRelation(sl, src) == direc) {
                    const Bitboard betweenSlSrc = betweenBB(sl, src);
                    if (!(betweenSlSrc & occ).isAny()) {
                        return true;
                    }
                }
            }
        }
        if (direc & DirecDiag) {
            Bitboard sliders = (pos.bbOf(Bishop) | pos.bbOf(Horse)) & pos.bbOf(us);
            while (sliders.isAny()) {
                const Square sl = sliders.firstOneFromSQ11();
                if (squareRelation(sl, src) == direc) {
                    const Bitboard betweenSlSrc = betweenBB(sl, src);
                    if (!(betweenSlSrc & occ).isAny()) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // Equivalent of shtsume's check_back_risk() for valid_long.
    // Checks ONLY collinearity + ordering: is there a defender slider on the
    // same line as src and enemyKing, with src between the slider and the king?
    // NO path clearance checks (matching shtsume, which checks no paths).
    bool checkBackRisk(const Position& pos, const Square src) {
        const Color us = pos.turn(); // defender
        const Color them = oppositeColor(us);
        const Square enemyKing = pos.kingSquare(them);
        if (!isInSquare(enemyKing)) return false;
        const Direction direc = squareRelation(src, enemyKing);
        if (direc == DirecMisc) return false;

        if (direc & DirecCross) {
            Bitboard sliders = (pos.bbOf(Rook) | pos.bbOf(Dragon)) & pos.bbOf(us);
            if (direc == DirecFile) {
                sliders |= pos.bbOf(Lance, us);
            }
            while (sliders.isAny()) {
                const Square sl = sliders.firstOneFromSQ11();
                // src between sl and enemyKing: betweenBB(sl, king) includes src
                if (betweenBB(sl, enemyKing).isSet(src)) {
                    return true;
                }
            }
        }
        if (direc & DirecDiag) {
            Bitboard sliders = (pos.bbOf(Bishop) | pos.bbOf(Horse)) & pos.bbOf(us);
            while (sliders.isAny()) {
                const Square sl = sliders.firstOneFromSQ11();
                if (betweenBB(sl, enemyKing).isSet(src)) {
                    return true;
                }
            }
        }
        return false;
    }

    // valid_next equivalent (n==1 move interpositions, Pattern B only).
    // Creates hypothetical: piece removed from move.from().
    // shtsume: sdata_pickup_table(&sbuf, src, false) then:
    //   1) Direct check on attacker's king in hypothetical → valid
    //   2) Reverse check via collinear slider → valid (set invalidFlag)
    //   3) If attacker has no hand: invalid_drops on hypothetical → prune
    //   4) Default: keep
    bool validNext(Position& pos, const Move move, TranspositionTable* tt,
        bool& invalidFlag, bool& proofFlag) {
        const Color us = pos.turn();
        const Color them = oppositeColor(us);
        const Square src = move.from();
        const Square enemyKing = pos.kingSquare(them);

        // Compute hypothetical occupancy (piece removed from src)
        Bitboard modOcc = pos.occupiedBB();
        modOcc.clearBit(src);

        // 1) Direct check on attacker's king in hypothetical.
        //    shtsume: BPOS_TEST(SELF_EFFECT(&sbuf), ENEMY_OU(&sbuf))
        //    After removing the piece from src, does any defender piece
        //    now attack the attacker's king? (Discovery through src.)
        if (isInSquare(enemyKing)) {
            if (pos.attackersToIsAny(us, enemyKing, modOcc)) {
                return true;
            }
        }

        // 2) Reverse check: defender slider behind src attacks enemy king.
        //    shtsume guard: BPOS_TEST(SELF_EFFECT(&sbuf), src) — only check
        //    if defender has any effect on src in the hypothetical (src cleared).
        //    Then per-piece-type collinearity + ordering check.
        if (isInSquare(enemyKing)
            && pos.attackersToIsAny(us, src, modOcc)
            && hasReverseCheckRisk(pos, src)) {
            if (totalHand(pos.hand(them)) == 0) {
                invalidFlag = true;
            }
            return true;
        }

        // 3) If attacker has no hand pieces, check invalid_drops on dest.
        //    shtsume: invalid_drops(&sbuf, NEW_POS(move), tbase)
        //    Uses the hypothetical where piece has been removed from src.
        //    We use the overload with hypothetical occ (src cleared) and
        //    hypothetical pins so attacker sliders through src can reach dest.
        if (totalHand(pos.hand(them)) == 0) {
            // defenderHasHand=true: sdata_pickup_table flag=false adds the
            // picked-up piece to defender's hand in the hypothetical.
            if (invalidDrops(pos, move.to(), tt, proofFlag, modOcc, src, true)) {
                invalidFlag = true;
                return false;
            }
        }

        return true;
    }

    // valid_long equivalent (n>=2 move interpositions, Pattern B only).
    // Creates hypothetical: piece removed from move.from(), recalculate.
    // shtsume: sdata_pickup_table(&sbuf, src, true) then:
    //   1) Defender king has escape squares in hypothetical → valid
    //   2) Direct check on attacker's king (dual-king) → valid
    //   3) Reverse check (check_back_risk) → valid (set invalidFlag)
    //   4) Unpinned defender effect on dest → valid
    //   5) Defender slider behind src attacks dest after piece removal:
    //      check if attacker moving checker to dest is checkmate → valid
    //      (set invalidFlag if attacker has no hand pieces)
    //   6) Default: prune (return false)
    bool validLong(Position& pos, const Move move, const Square dest,
        const Square kingSq, const Square checkerSq, bool& invalidFlag)
    {
        const Color us = pos.turn();
        const Color them = oppositeColor(us);
        const Square src = move.from();

        // 1) King escape: check evasion_bb equivalent (shtsume: piece removed
        //    from src, effects recalculated). King moves to a square not
        //    attacked by attacker in the hypothetical occ (src cleared).
        {
            const Bitboard kingAttacks = pos.attacksFrom<King>(kingSq);
            const Bitboard occ = pos.occupiedBB();
            // In the hypothetical, src is empty (piece removed)
            Bitboard defPieces = pos.bbOf(us);
            defPieces.clearBit(src);
            Bitboard escapeBB = kingAttacks;
            escapeBB.andEqualNot(defPieces);
            while (escapeBB.isAny()) {
                const Square esc = escapeBB.firstOneFromSQ11();
                Bitboard newOcc = occ;
                newOcc.clearBit(src);
                newOcc.clearBit(kingSq);
                newOcc.setBit(esc);
                if (!pos.attackersToIsAny(them, esc, newOcc)) {
                    return true;
                }
            }
        }

        // 2) Direct check on attacker's king in hypothetical (dual-king).
        //    shtsume: BPOS_TEST(SELF_EFFECT(&sbuf), ENEMY_OU(&sbuf))
        {
            const Square enemyKing = pos.kingSquare(them);
            Bitboard modOcc = pos.occupiedBB();
            modOcc.clearBit(src);
            if (isInSquare(enemyKing)) {
                if (pos.attackersToIsAny(us, enemyKing, modOcc)) {
                    return true;
                }
            }
        }

        // 3) Reverse check risk (check_back_risk equivalent).
        //    shtsume's check_back_risk checks only collinearity + ordering,
        //    no path clearance. Use checkBackRisk to match.
        if (checkBackRisk(pos, src)) {
            if (totalHand(pos.hand(them)) == 0) {
                invalidFlag = true;
            }
            return true;
        }

        // 4) Unpinned defender effect (non-king, not the moving piece) on dest.
        //    Use modified occ (src cleared) so slider lines through src are
        //    correctly open, and hypothetical pinnedBB (src cleared) so pieces
        //    that were pinned through src may now be detected as unpinned —
        //    matches shtsume's sdata_pickup_table + create_pin hypothetical.
        {
            Bitboard modOcc = pos.occupiedBB();
            modOcc.clearBit(src);
            Bitboard defenders = pos.attackersTo(us, dest, modOcc);
            defenders.clearBit(kingSq);
            defenders.clearBit(src); // piece at src has been removed
            const Bitboard pinned = pinnedBBWithout(pos, src);
            while (defenders.isAny()) {
                const Square defSq = defenders.firstOneFromSQ11();
                if (!pinned.isSet(defSq)) return true;
            }
        }

        // 5) Defender slider attacking src in the hypothetical (src cleared).
        //    shtsume: for each unpinned defender slider (bishop/horse, rook/dragon,
        //    lance) with effect on PREV_POS(move) = src, create a secondary
        //    hypothetical (checker→dest) and run tsumi_check. If NOT mate → valid.
        //    The slider's newly opened line through src may provide indirect
        //    defense against the check (cover escape squares, interpose, etc).
        //    We use modOcc (src cleared) as baseOcc for isMateByTentativeMoveBB
        //    so the opened slider lines are correctly reflected.
        {
            Bitboard modOcc = pos.occupiedBB();
            modOcc.clearBit(src);
            const Bitboard pinned = pinnedBBWithout(pos, src);

            // Find defender sliders attacking src in the hypothetical
            Bitboard defSliders = pos.attackersTo(us, src, modOcc);
            defSliders.clearBit(kingSq);
            defSliders.clearBit(src); // removed piece
            // Keep only sliders
            Bitboard sliderMask = (pos.bbOf(Lance) & pos.bbOf(us))
                | ((pos.bbOf(Bishop) | pos.bbOf(Horse)) & pos.bbOf(us))
                | ((pos.bbOf(Rook) | pos.bbOf(Dragon)) & pos.bbOf(us));
            defSliders &= sliderMask;

            while (defSliders.isAny()) {
                const Square sl = defSliders.firstOneFromSQ11();
                if (pinned.isSet(sl)) continue;
                // Unpinned slider found: check if checker→dest is mate
                // on the hypothetical with src cleared.
                // Pass src as excludeDefSq so the phantom defender piece
                // at the interposition source is properly excluded from
                // king escape / capture / interpose enumeration.
                const PieceType cpt = pieceToPieceType(pos.piece(checkerSq));
                const bool forced = mustPromoteAtDest(them, cpt, dest);
                // shtsume: sdata_tentative_move(..., false) — non-promoted only
                bool isCheckmate = false;
                if (!forced) {
                    isCheckmate = isMateByTentativeMoveBB(pos, cpt, checkerSq, dest, false,
                        modOcc, src);
                }
                if (!isCheckmate) {
                    if (totalHand(pos.hand(them)) == 0) {
                        invalidFlag = true;
                    }
                    return true;
                }
                // Mate confirmed — other sliders give the same result (same hypothetical)
                break;
            }
        }

        // 6) Default: prune
        return false;
    }

    // Main interposition filter: prunes futile interpositions and computes flags.
    // Equivalent of shtsume's generate_evasion() interposition section:
    //   n==1 drops:  invalid_drops() — can attacker recapture to checkmate?
    //                Now includes TT probe for promoted variants (hs_tbase_lookup)
    //   n==2 drops:  keep if prior legal moves, else is_dest_effect_valid()
    //                or cross_point_check() (with TT probe)
    //   n>=3 drops:  keep if prior legal moves, else is_dest_effect_valid()
    //   n==1 moves:  Pattern A (has prior legal moves) → keep all
    //                Pattern B (no prior legal moves) → valid_next()
    //   n>=2 moves:  Pattern A → keep all
    //                Pattern B → valid_long()
    // proof_flag:   set via TT-proven path in invalid_drops (hs_tbase_lookup)
    // invalid_flag: set when valid_next/valid_long detect reverse-check risk
    //               and attacker has no hand pieces
    EvasionFilterResult filterInterpositions(
        Position& pos, ExtMove* moves, ExtMove*& last,
        TranspositionTable* tt)
    {
        EvasionFilterResult result;
        ExtMove* const savedLast = last;

        Square checkerSq, kingSq;
        PieceType checkerPt;
        if (!sliderCheckInfo(pos, checkerSq, kingSq, checkerPt))
            return result;

        const Bitboard between = betweenBB(kingSq, checkerSq);
        if (!between.isAny()) return result;

        // hasLegalMoves tracks whether king moves or captures of checker exist
        // (= shtsume's "mvlist" is non-null before interposition processing)
        bool hasLegalMoves = false;
        for (const ExtMove* curr = moves; curr != last; ++curr) {
            const Move mv = curr->move;
            if (isKingMove(pos, mv) || (!mv.isDrop() && mv.to() == checkerSq)) {
                hasLegalMoves = true;
                break;
            }
        }

        // hasDropList / hasMoveList track accumulated interposition moves
        // (= shtsume's drop_list / move_list non-null)
        bool hasDropList = false;
        bool hasMoveList = false;

        const SquareDelta step = stepTowards(kingSq, checkerSq);
        if (step == DeltaNothing) return result;

        int n = 0;
        Square dest = kingSq;
        while (true) {
            dest = dest + step;
            if (dest == checkerSq) break;
            if (!isInSquare(dest)) break;
            n++;

            // ---------------------------------------------------------------
            // Drop interpositions at this dest
            // ---------------------------------------------------------------
            // Find a representative drop for this dest
            Move representativeDrop = Move::moveNone();
            bool hasDropsHere = false;
            for (const ExtMove* curr = moves; curr != last; ++curr) {
                if (curr->move.isDrop() && curr->move.to() == dest) {
                    hasDropsHere = true;
                    if (!representativeDrop.isAny())
                        representativeDrop = curr->move;
                }
            }

            bool pruneDrops = false;
            if (hasDropsHere) {
                if (n == 1) {
                    // n==1: shtsume calls invalid_drops() unconditionally
                    pruneDrops = invalidDrops(pos, dest, tt, result.proofFlag);
                }
                else {
                    // n>=2 drops: shtsume checks if(mvlist || move_list) → keep.
                    if (!hasLegalMoves && !hasMoveList) {
                        if (hasUnpinnedDefenderEffect(pos, dest)) {
                            // Unpinned defender piece attacks dest → valid
                        }
                        else if (n == 2 && crossPointCheck(pos, dest, checkerSq,
                                                           checkerPt, tt)) {
                            // n==2 only: cross-point escape detected → valid
                        }
                        else {
                            // shtsume prunes unconditionally here (no defender
                            // effect + no cross_point_check → futile).
                            pruneDrops = true;
                        }
                    }
                }

                if (pruneDrops) {
                    ExtMove* curr = moves;
                    while (curr != last) {
                        if (curr->move.isDrop() && curr->move.to() == dest)
                            curr->move = (--last)->move;
                        else
                            ++curr;
                    }
                }
                else {
                    hasDropList = true;
                }
            }

            // ---------------------------------------------------------------
            // Move interpositions at this dest
            // ---------------------------------------------------------------
            bool hasMoveInterpsHere = false;
            for (const ExtMove* curr = moves; curr != last; ++curr) {
                const Move mv = curr->move;
                if (!mv.isDrop() && mv.to() == dest && !isKingMove(pos, mv)
                    && mv.to() != checkerSq)
                {
                    hasMoveInterpsHere = true;
                    break;
                }
            }

            if (hasMoveInterpsHere) {
                // Pattern A: king moves, captures, or drops exist → keep all
                // Pattern B: no prior legal moves → filter with valid_next/valid_long
                if (!hasLegalMoves && !hasDropList) {
                    // Pattern B
                    ExtMove* curr = moves;
                    while (curr != last) {
                        const Move mv = curr->move;
                        if (!mv.isDrop() && mv.to() == dest && !isKingMove(pos, mv)
                            && mv.to() != checkerSq)
                        {
                            bool valid;
                            if (n == 1) {
                                valid = validNext(pos, mv, tt,
                                    result.invalidFlag, result.proofFlag);
                            }
                            else {
                                valid = validLong(pos, mv, dest, kingSq, checkerSq,
                                    result.invalidFlag);
                            }
                            if (!valid) {
                                curr->move = (--last)->move;
                                continue;
                            }
                        }
                        ++curr;
                    }
                }
                // Update hasMoveList if any move interpositions remain at this dest
                for (const ExtMove* curr = moves; curr != last; ++curr) {
                    const Move mv = curr->move;
                    if (!mv.isDrop() && mv.to() == dest && !isKingMove(pos, mv)
                        && mv.to() != checkerSq)
                    {
                        hasMoveList = true;
                        break;
                    }
                }
            }
        }

        // Safety net: if pruning removed all evasions and no king
        // moves/captures exist, restore to prevent false mate.
        if (moves == last && savedLast != last && !hasLegalMoves) {
            last = savedLast;
            result.proofFlag = false;
            result.invalidFlag = false;
        }

        return result;
    }

    template <bool or_node>
    class MovePicker {
    public:
        explicit MovePicker(Position& pos, TranspositionTable* tt = nullptr)
            : evasionFilter_{}
        {
            if constexpr (or_node) {
                last_ = generateMoves<CheckAll>(moves_, pos);
                if (pos.inCheck()) {
                    ExtMove* curr = moves_;
                    while (curr != last_) {
                        if (!pos.checkMoveIsEvasion(curr->move)) {
                            curr->move = (--last_)->move;
                        }
                        else {
                            ++curr;
                        }
                    }
                }
            }
            else {
                last_ = generateMoves<Evasion>(moves_, pos);
                ExtMove* curr = moves_;
                const Bitboard pinned = pos.pinnedBB();
                while (curr != last_) {
                    if (!pos.pseudoLegalMoveIsLegal<false, false>(curr->move, pinned)) {
                        curr->move = (--last_)->move;
                    }
                    else {
                        ++curr;
                    }
                }
                // Apply futile interposition filtering (shtsume generate_evasion equivalent)
                evasionFilter_ = filterInterpositions(pos, moves_, last_, tt);
            }
            assert(size() <= kMaxCheckMoves);
        }

        size_t size() const { return static_cast<size_t>(last_ - moves_); }
        bool empty() const { return moves_ == last_; }
        ExtMove* begin() { return moves_; }
        ExtMove* end() { return last_; }
        const ExtMove* begin() const { return moves_; }
        const ExtMove* end() const { return last_; }

        bool proofFlag() const { return evasionFilter_.proofFlag; }
        bool invalidFlag() const { return evasionFilter_.invalidFlag; }

    private:
        ExtMove moves_[kMaxCheckMoves];
        ExtMove* last_ = moves_;
        EvasionFilterResult evasionFilter_;
    };

    using MList = ns_dfpn::MList;
    using MvList = ns_dfpn::MvList;
    using MvListPool = ns_dfpn::MvListPool;

    enum class AndGroupKind {
        Single,
        DropSerial,
        MoveSerial,
        PromotePair,
        TokinDest,
        BigPieceDest,
    };

    Hand zeroHand() {
        Hand hand;
        hand.set(0);
        return hand;
    }

    void setHandCount(Hand& hand, const HandPiece hp, const int count) {
        if (count > 0) {
            hand.orEqual(count, hp);
        }
    }

    template <HandPiece HP>
    int countOf(const Hand& hand) {
        return static_cast<int>(hand.numOf<HP>());
    }

    int totalHand(const Hand& hand) {
        return countOf<HPawn>(hand)
            + countOf<HLance>(hand)
            + countOf<HKnight>(hand)
            + countOf<HSilver>(hand)
            + countOf<HGold>(hand)
            + countOf<HBishop>(hand)
            + countOf<HRook>(hand);
    }

    Hand makeHandFromCounts(int pawn, int lance, int knight, int silver, int gold, int bishop, int rook) {
        Hand hand = zeroHand();
        setHandCount(hand, HPawn, pawn);
        setHandCount(hand, HLance, lance);
        setHandCount(hand, HKnight, knight);
        setHandCount(hand, HSilver, silver);
        setHandCount(hand, HGold, gold);
        setHandCount(hand, HBishop, bishop);
        setHandCount(hand, HRook, rook);
        return hand;
    }

    void incrementHand(Hand& hand, const HandPiece hp) {
        hand.plusOne(hp);
    }

    bool decrementHandIfAny(Hand& hand, const HandPiece hp) {
        if (hand.exists(hp)) {
            hand.minusOne(hp);
            return true;
        }
        return false;
    }

    int piecePriority(const PieceType pt) {
        // Match shtsume raw piece encoding:
        // SFU=1,SKY=2,SKE=3,SGI=4,SKI=5,SKA=6,SHI=7,SOU=8,
        // STO=9,SNY=10,SNK=11,SNG=12,SUM=14,SRY=15
        switch (pt) {
        case Pawn: return 1;
        case Lance: return 2;
        case Knight: return 3;
        case Silver: return 4;
        case Gold: return 5;
        case Bishop: return 6;
        case Rook: return 7;
        case King: return 8;
        case ProPawn: return 9;
        case ProLance: return 10;
        case ProKnight: return 11;
        case ProSilver: return 12;
        case Horse: return 14;
        case Dragon: return 15;
        default: return 0;
        }
    }

    int movingPiecePriority(const Position& pos, const Move move) {
        if (move.isDrop()) {
            return piecePriority(handPieceToPieceType(move.handPieceDropped()));
        }
        return piecePriority(pieceToPieceType(pos.piece(move.from())));
    }

    struct AndNodeFlags {
        bool proof_flag = false;
        bool invalid_flag = false;
        bool ryuma_flag = false;
    };

    bool sliderCheckInfo(const Position& pos, Square& checkerSq, Square& kingSq, PieceType& checkerPt) {
        const Bitboard checkers = pos.checkersBB();
        if (checkers.popCount() != 1) {
            return false;
        }
        checkerSq = checkers.constFirstOneFromSQ11();
        kingSq = pos.kingSquare(pos.turn());
        checkerPt = pieceToPieceType(pos.piece(checkerSq));
        switch (checkerPt) {
        case Lance:
        case Bishop:
        case Rook:
        case Horse:
        case Dragon:
            return betweenBB(checkerSq, kingSq).isAny();
        default:
            return false;
        }
    }

    bool isInterpositionEvasion(const Position& pos, const Move move) {
        Square checkerSq;
        Square kingSq;
        PieceType checkerPt;
        if (!sliderCheckInfo(pos, checkerSq, kingSq, checkerPt)) {
            return false;
        }
        if (!betweenBB(checkerSq, kingSq).isSet(move.to())) {
            return false;
        }
        if (!move.isDrop() && move.from() == kingSq) {
            return false;
        }
        return true;
    }

    bool isUnsupportedDropInterposition(const Position& pos, const Move move) {
        Square checkerSq;
        Square kingSq;
        PieceType checkerPt;
        if (!move.isDrop() || !sliderCheckInfo(pos, checkerSq, kingSq, checkerPt) || squareDistance(checkerSq, kingSq) <= 2) {
            return false;
        }
        if (!betweenBB(checkerSq, kingSq).isSet(move.to())) {
            return false;
        }
        return !pos.attackersToIsAny(pos.turn(), move.to());
    }

    bool isKingMove(const Position& pos, const Move move) {
        return !move.isDrop() && move.from() == pos.kingSquare(pos.turn());
    }

    // -----------------------------------------------------------------------
    // Linked-list proof/disproof number computation (shtsume equivalent)
    // -----------------------------------------------------------------------

    int proofNumber(const MvList* list, int* count) {
        assert(list);
        int best = list->state.data.pn;
        int total = 0;
        if (best == kInfinite && list->state.data.dn == 0) {
            if (count) *count = 0;
            return kInfinite;
        }
        for (const MvList* p = list->next; p; p = p->next) {
            if (p->state.data.pn == 0) break;
            ++total;
            best = std::max(best, p->state.data.pn);
        }
        if (count) *count = total;
        return std::min(kInfinite - 1, best + total);
    }

    int disproofNumber(const MvList* list, int* count) {
        assert(list);
        int best = list->state.data.dn;
        int total = 0;
        if (best == kInfinite && list->state.data.pn == 0) {
            if (count) *count = 0;
            return kInfinite;
        }
        for (const MvList* p = list->next; p; p = p->next) {
            if (p->state.data.dn == 0) break;
            ++total;
            best = std::max(best, p->state.data.dn);
        }
        if (count) *count = total;
        return std::min(kInfinite - 1, best + total);
    }

    // -----------------------------------------------------------------------
    // Linked-list merge sort (shtsume sdata_mvlist_sort equivalent)
    // -----------------------------------------------------------------------

    // Split list into two halves
    MvList* mvlistSplitHalf(MvList* head) {
        MvList* slow = head;
        MvList* fast = head->next;
        while (fast) {
            fast = fast->next;
            if (fast) { slow = slow->next; fast = fast->next; }
        }
        MvList* second = slow->next;
        slow->next = nullptr;
        return second;
    }

    // Merge two sorted lists using comparator
    template <typename Cmp>
    MvList* mvlistMerge(MvList* a, MvList* b, Cmp cmp) {
        MvList dummy;
        dummy.next = nullptr;
        MvList* tail = &dummy;
        while (a && b) {
            if (cmp(a, b) <= 0) {
                tail->next = a; tail = a; a = a->next;
            } else {
                tail->next = b; tail = b; b = b->next;
            }
        }
        tail->next = a ? a : b;
        return dummy.next;
    }

    // Merge sort on linked list
    template <typename Cmp>
    MvList* mvlistSort(MvList* head, Cmp cmp) {
        if (!head || !head->next) return head;
        MvList* second = mvlistSplitHalf(head);
        head = mvlistSort(head, cmp);
        second = mvlistSort(second, cmp);
        return mvlistMerge(head, second, cmp);
    }

    // Reorder front element into correct position (shtsume sdata_mvlist_reorder)
    template <typename Cmp>
    MvList* mvlistReorderFront(MvList* list, Cmp cmp) {
        if (!list || !list->next) return list;
        if (cmp(list, list->next) <= 0) return list;
        MvList* mv = list;
        MvList* newHead = list->next;
        MvList* prev = newHead;
        MvList* cur = prev->next;
        while (cur) {
            if (cmp(mv, cur) <= 0) {
                prev->next = mv;
                mv->next = cur;
                return newHead;
            }
            prev = cur;
            cur = cur->next;
        }
        prev->next = mv;
        mv->next = nullptr;
        return newHead;
    }

    // -----------------------------------------------------------------------
    // Comparators returning int (for merge sort)
    // -----------------------------------------------------------------------

    int orChildCmp(const MvList* a, const MvList* b, const Position& pos) {
        // pn ascending
        if (a->state.data.pn != b->state.data.pn) return (a->state.data.pn < b->state.data.pn) ? -1 : 1;
        // dn ascending
        if (a->state.data.dn != b->state.data.dn) return (a->state.data.dn < b->state.data.dn) ? -1 : 1;
        // sh==0 preferred when pn==0
        if (a->state.data.pn == 0) {
            const bool aSh0 = (a->state.data.sh == 0);
            const bool bSh0 = (b->state.data.sh == 0);
            if (aSh0 != bSh0) return aSh0 ? -1 : 1;
        }
        // inc descending (prefer inc=true)
        if (a->state.inc != b->state.inc) return a->state.inc ? -1 : 1;
        // sh ascending
        if (a->state.data.sh != b->state.data.sh) return (a->state.data.sh < b->state.data.sh) ? -1 : 1;
        // protect ascending
        if (a->state.protect != b->state.protect) return a->state.protect ? 1 : -1;
        // current ascending
        if (a->state.current != b->state.current) return a->state.current ? 1 : -1;
        // nouse2 ascending
        if (a->state.nouse2 != b->state.nouse2) return (a->state.nouse2 < b->state.nouse2) ? -1 : 1;

        const Move ma = a->headMove(), mb = b->headMove();
        // Captures preferred (boolean, not value)
        const bool capA = !ma.isDrop() && pos.piece(ma.to()) != Empty;
        const bool capB = !mb.isDrop() && pos.piece(mb.to()) != Empty;
        if (capA != capB) return capA ? -1 : 1;
        // Board moves preferred
        if (ma.isDrop() != mb.isDrop()) return ma.isDrop() ? 1 : -1;
        // Promotions preferred
        if (ma.isPromotion() != mb.isPromotion()) return ma.isPromotion() ? -1 : 1;
        // Higher drop piece
        if (ma.isDrop() && mb.isDrop()) {
            if (ma.handPieceDropped() != mb.handPieceDropped())
                return (ma.handPieceDropped() > mb.handPieceDropped()) ? -1 : 1;
        }
        // Closer distance
        if (a->length != b->length) return (a->length < b->length) ? -1 : 1;
        // Raw move tiebreaker
        if (ma.value() != mb.value()) return (ma.value() < mb.value()) ? -1 : 1;
        return 0;
    }

    int andGroupCmp(const MvList* a, const MvList* b, const Position& pos) {
        if (a->state.data.dn != b->state.data.dn) return (a->state.data.dn < b->state.data.dn) ? -1 : 1;
        if (a->state.data.pn != b->state.data.pn) return (a->state.data.pn < b->state.data.pn) ? -1 : 1;
        if (a->state.inc != b->state.inc) return (a->state.inc < b->state.inc) ? -1 : 1;
        if (a->state.nouse2 != b->state.nouse2) return (a->state.nouse2 < b->state.nouse2) ? -1 : 1;
        if (a->state.data.pn == 0 && b->state.data.pn == 0) {
            if (a->state.data.sh != b->state.data.sh) return (a->state.data.sh > b->state.data.sh) ? -1 : 1;
        } else if (a->state.data.sh != b->state.data.sh) {
            return (a->state.data.sh < b->state.data.sh) ? -1 : 1;
        }
        // Full andMoveLess tiebreaker on head moves
        const Move ma = a->headMove(), mb = b->headMove();
        if (ma.isDrop() != mb.isDrop()) return ma.isDrop() ? 1 : -1;
        const bool capA = !ma.isDrop() && pos.piece(ma.to()) != Empty;
        const bool capB = !mb.isDrop() && pos.piece(mb.to()) != Empty;
        if (capA != capB) return capA ? -1 : 1;
        const int prioA = movingPiecePriority(pos, ma);
        const int prioB = movingPiecePriority(pos, mb);
        if (prioA != prioB) return (prioA < prioB) ? -1 : 1;
        const bool kingA = isKingMove(pos, ma);
        const bool kingB = isKingMove(pos, mb);
        if (kingA != kingB) return kingA ? -1 : 1;
        if (ma.isPromotion() != mb.isPromotion()) return ma.isPromotion() ? -1 : 1;
        if (ma.isDrop() && mb.isDrop()) {
            if (ma.handPieceDropped() != mb.handPieceDropped())
                return (ma.handPieceDropped() < mb.handPieceDropped()) ? -1 : 1;
        }
        if (ma.value() != mb.value()) return (ma.value() < mb.value()) ? -1 : 1;
        return 0;
    }

    // -----------------------------------------------------------------------
    // Append mlist of src to dst (shtsume mlist_concat equivalent)
    // -----------------------------------------------------------------------
    void appendGroupMoves(MvList* dst, MvList* src) {
        MList* last = MvListPool::mlistLast(dst->mlist);
        last->next = src->mlist;
        src->mlist = nullptr;  // detach so free won't double-free
    }

    // -----------------------------------------------------------------------
    // Linked-list versions of reduction/collapse functions

    MvList* applyAndRyumaReductionLL(MvList* list, const Position& pos, MvListPool& pool) {
        if (!list || !list->next) return list;

        // Case 1: front is unsupported drop interposition
        if (isUnsupportedDropInterposition(pos, list->headMove())
            && list->state.data.pn != 0
            && list->state.data.dn != 0
            && list->next->state.data.pn != 0
            && list->next->state.data.dn != 0) {
            // Merge front into second
            MvList* front = list;
            list = list->next;
            appendGroupMoves(list, front);
            front->next = nullptr;
            pool.freeMvList(front);
            return list;
        }

        // Case 2: find unsupported drop interposition in tail, merge into front
        MvList* prev = list;
        MvList* cur = list->next;
        while (cur) {
            if (cur->state.data.pn == 0) break;
            if (isUnsupportedDropInterposition(pos, cur->headMove())) {
                prev->next = cur->next;
                appendGroupMoves(list, cur);
                cur->next = nullptr;
                pool.freeMvList(cur);
                break;
            }
            prev = cur;
            cur = cur->next;
        }
        return list;
    }

    bool collapseAndProofGhiLL(MvList*& list, const Position& pos, MvListPool& pool, int aggregatePn) {
        if (!list || !list->next || list->state.data.pn == 0 || list->state.data.dn == 0) {
            return false;
        }
        // shtsume: mvlist->tdata.pn > PRE_PROOF_MAX && list->next->tdata.pn && !S_BOARD(...)
        if (aggregatePn <= kPreProofMax || list->next->state.data.pn == 0 || pos.piece(list->headMove().to()) != Empty) {
            return false;
        }
        // Find max-pn node across ALL nodes including front (shtsume includes front)
        MvList* maxNode = list;
        for (MvList* cur = list->next; cur; cur = cur->next) {
            if (cur->state.data.pn > maxNode->state.data.pn) {
                maxNode = cur;
            }
        }
        // Remove maxNode from the list
        if (maxNode == list) {
            list = list->next;
        } else {
            MvList* prev = list;
            while (prev->next != maxNode) prev = prev->next;
            prev->next = maxNode->next;
        }
        maxNode->next = nullptr;
        // Re-sort remaining list (shtsume: sdata_mvlist_sort disproof_number_comp)
        list = mvlistSort(list, [&](const MvList* a, const MvList* b) {
            return andGroupCmp(a, b, pos);
        });
        // Merge max-pn moves into new front
        appendGroupMoves(list, maxNode);
        pool.freeMvList(maxNode);
        return true;
    }

    bool collapseAndDisproofGhiLL(MvList*& list, MvListPool& pool) {
        if (!list || !list->next) return false;
        if (list->state.data.pn == 0 || list->state.data.dn <= kPreDisproofMax) return false;
        MvList* second = list->next;
        if (second->state.data.pn == 0 || second->state.data.dn == 0) return false;
        // Remove second, merge into front
        list->next = second->next;
        appendGroupMoves(list, second);
        second->next = nullptr;
        pool.freeMvList(second);
        return true;
    }

    bool moveGivesNeighborCheck(const Position& pos, const Move move) {
        const Color them = oppositeColor(pos.turn());
        const Square ksq = pos.kingSquare(them);
        const Square to = move.to();
        const PieceType pt = move.pieceTypeTo();

        switch (pt) {
        case Pawn:
        case Lance:
            return pawnAttack(them, ksq).isSet(to);
        case Knight:
            return knightAttack(them, ksq).isSet(to);
        case Silver:
            return silverAttack(them, ksq).isSet(to);
        case Bishop:
            return bishopAttack(ksq, allOneBB()).isSet(to);
        case Rook:
            return rookAttack(ksq, allOneBB()).isSet(to);
        case Horse:
        case Dragon:
            return kingAttack(ksq).isSet(to);
        case King:
            return false;
        default:
            return goldAttack(them, ksq).isSet(to);
        }
    }

    Hand disproofSeedHand(const Hand& us, const Hand& them) {
        Hand hand = zeroHand();
        if (us.exists<HPawn>())   setHandCount(hand, HPawn,   countOf<HPawn>(us)   + countOf<HPawn>(them));
        if (us.exists<HLance>())  setHandCount(hand, HLance,  countOf<HLance>(us)  + countOf<HLance>(them));
        if (us.exists<HKnight>()) setHandCount(hand, HKnight, countOf<HKnight>(us) + countOf<HKnight>(them));
        if (us.exists<HSilver>()) setHandCount(hand, HSilver, countOf<HSilver>(us) + countOf<HSilver>(them));
        if (us.exists<HGold>())   setHandCount(hand, HGold,   countOf<HGold>(us)   + countOf<HGold>(them));
        if (us.exists<HBishop>()) setHandCount(hand, HBishop, countOf<HBishop>(us) + countOf<HBishop>(them));
        if (us.exists<HRook>())   setHandCount(hand, HRook,   countOf<HRook>(us)   + countOf<HRook>(them));
        return hand;
    }

    Hand minHand(const Hand& lhs, const Hand& rhs) {
        Hand hand = zeroHand();
        setHandCount(hand, HPawn, std::min(countOf<HPawn>(lhs), countOf<HPawn>(rhs)));
        setHandCount(hand, HLance, std::min(countOf<HLance>(lhs), countOf<HLance>(rhs)));
        setHandCount(hand, HKnight, std::min(countOf<HKnight>(lhs), countOf<HKnight>(rhs)));
        setHandCount(hand, HSilver, std::min(countOf<HSilver>(lhs), countOf<HSilver>(rhs)));
        setHandCount(hand, HGold, std::min(countOf<HGold>(lhs), countOf<HGold>(rhs)));
        setHandCount(hand, HBishop, std::min(countOf<HBishop>(lhs), countOf<HBishop>(rhs)));
        setHandCount(hand, HRook, std::min(countOf<HRook>(lhs), countOf<HRook>(rhs)));
        return hand;
    }

    Hand undoOrMoveOnProofHand(const Position& pos, const Move move, Hand hand) {
        if (move.isDrop()) {
            incrementHand(hand, move.handPieceDropped());
            return hand;
        }
        const Piece captured = pos.piece(move.to());
        if (captured == Empty) {
            return hand;
        }
        const HandPiece hp = pieceTypeToHandPiece(pieceToPieceType(captured));
        decrementHandIfAny(hand, hp);
        return hand;
    }

    Hand undoOrMoveOnDisproofHand(const Position& pos, const Move move, Hand hand) {
        if (move.isDrop()) {
            incrementHand(hand, move.handPieceDropped());
            return hand;
        }
        const Piece captured = pos.piece(move.to());
        if (captured == Empty) {
            return hand;
        }
        const HandPiece hp = pieceTypeToHandPiece(pieceToPieceType(captured));
        decrementHandIfAny(hand, hp);
        return hand;
    }

    Hand terminalProofHand(const Position& pos);

    TTState makeMateByOrMove(const Position& pos, const Move move, const TTState& child) {
        TTState result = child;
        result.hand = child.hand;
        result.hinc = child.hinc;
        result.nouse = child.nouse;
        result.current = false;
        if (move.isDrop()) {
            incrementHand(result.hand, move.handPieceDropped());
        }
        else {
            const Piece captured = pos.piece(move.to());
            if (captured != Empty) {
                const HandPiece hp = pieceTypeToHandPiece(pieceToPieceType(captured));
                if (!decrementHandIfAny(result.hand, hp)) {
                    result.hinc = true;
                    ++result.nouse;
                }
            }
        }
        result.inc = (result.hand == pos.hand(pos.turn())) ? result.hinc : true;
        result.nouse2 = totalHand(pos.hand(pos.turn())) - totalHand(result.hand) + result.nouse;
        return result;
    }

    TTState makeNoMateByAndMove(const Position& pos, const Move move, const TTState& child) {
        TTState result = child;
        if (move.isDrop()) {
            const HandPiece hp = move.handPieceDropped();
            const Hand defender = pos.hand(pos.turn());
            const Hand attacker = pos.hand(oppositeColor(pos.turn()));
            if (defender.numOf(hp) + attacker.numOf(hp) == result.hand.numOf(hp) && result.hand.exists(hp)) {
                result.hand.minusOne(hp);
            }
        }
        else {
            const Piece captured = pos.piece(move.to());
            if (captured != Empty) {
                const HandPiece hp = pieceTypeToHandPiece(pieceToPieceType(captured));
                const Hand defender = pos.hand(pos.turn());
                const Hand attacker = pos.hand(oppositeColor(pos.turn()));
                if (defender.numOf(hp) + attacker.numOf(hp) + 1 == result.hand.numOf(hp) && result.hand.exists(hp)) {
                    result.hand.minusOne(hp);
                }
            }
        }
        result.inc = (result.hand == pos.hand(oppositeColor(pos.turn())));
        result.hinc = false;
        result.nouse = 0;
        result.nouse2 = 0;
        result.protect = false;
        result.current = false;
        return result;
    }

    TTState makeTerminalMateState(const Position& pos, const bool proofFlag, const int sh) {
        TTState result;
        result.data = { 0, kInfinite, sh };
        const Hand attacker = pos.hand(oppositeColor(pos.turn()));
        result.hand = proofFlag ? attacker : terminalProofHand(pos);
        const int totalAttacker = totalHand(attacker);
        const int totalProof = totalHand(result.hand);
        result.inc = totalAttacker > 0;
        result.hinc = totalProof > 0;
        result.nouse = totalProof;
        result.nouse2 = totalAttacker;
        result.current = false;
        return result;
    }

    TTState makeMateByAndProofFlag(const Position& pos, const TTState& child) {
        TTState result;
        result.data = { 0, kInfinite, child.data.sh + 1 };
        result.hand = pos.hand(oppositeColor(pos.turn()));
        result.inc = child.inc;
        result.hinc = child.inc;
        result.nouse = child.nouse + totalHand(result.hand) - totalHand(child.hand);
        result.nouse2 = result.nouse;
        result.current = false;
        return result;
    }

    Hand terminalProofHand(const Position& pos) {
        Hand hand = zeroHand();
        const Square king = pos.kingSquare(pos.turn());
        const bool contactCheck = (pos.checkersBB() & pos.attacksFrom<King>(king)).isAny()
            || (pos.checkersBB() & pos.attacksFrom<Knight>(pos.turn(), king)).isAny();
        if (!contactCheck) {
            hand.setPP(pos.hand(oppositeColor(pos.turn())), pos.hand(pos.turn()));
        }
        return hand;
    }

    // -----------------------------------------------------------------------
    // MvList-based overloads for dfpn_inner hot path
    // -----------------------------------------------------------------------

    Hand unionProofHandsLL(const MvList* list) {
        int pawn = 0, lance = 0, knight = 0, silver = 0, gold = 0, bishop = 0, rook = 0;
        for (const MvList* p = list; p; p = p->next) {
            if (p->state.data.pn != 0) break;
            pawn = std::max(pawn, countOf<HPawn>(p->state.hand));
            lance = std::max(lance, countOf<HLance>(p->state.hand));
            knight = std::max(knight, countOf<HKnight>(p->state.hand));
            silver = std::max(silver, countOf<HSilver>(p->state.hand));
            gold = std::max(gold, countOf<HGold>(p->state.hand));
            bishop = std::max(bishop, countOf<HBishop>(p->state.hand));
            rook = std::max(rook, countOf<HRook>(p->state.hand));
        }
        return makeHandFromCounts(pawn, lance, knight, silver, gold, bishop, rook);
    }

    TTState makeMateByAndNodeLL(const Position& pos, const MvList* list) {
        TTState result;
        result.data = { 0, kInfinite, list->state.data.sh + 1 };
        const Hand united = unionProofHandsLL(list);
        const Hand defender = pos.hand(pos.turn());
        const Hand attacker = pos.hand(oppositeColor(pos.turn()));
        const int pawn = (!defender.exists<HPawn>() && countOf<HPawn>(united) != countOf<HPawn>(attacker)) ? countOf<HPawn>(attacker) : countOf<HPawn>(united);
        const int lance = (!defender.exists<HLance>() && countOf<HLance>(united) != countOf<HLance>(attacker)) ? countOf<HLance>(attacker) : countOf<HLance>(united);
        const int knight = (!defender.exists<HKnight>() && countOf<HKnight>(united) != countOf<HKnight>(attacker)) ? countOf<HKnight>(attacker) : countOf<HKnight>(united);
        const int silver = (!defender.exists<HSilver>() && countOf<HSilver>(united) != countOf<HSilver>(attacker)) ? countOf<HSilver>(attacker) : countOf<HSilver>(united);
        const int gold = (!defender.exists<HGold>() && countOf<HGold>(united) != countOf<HGold>(attacker)) ? countOf<HGold>(attacker) : countOf<HGold>(united);
        const int bishop = (!defender.exists<HBishop>() && countOf<HBishop>(united) != countOf<HBishop>(attacker)) ? countOf<HBishop>(attacker) : countOf<HBishop>(united);
        const int rook = (!defender.exists<HRook>() && countOf<HRook>(united) != countOf<HRook>(attacker)) ? countOf<HRook>(attacker) : countOf<HRook>(united);
        result.hand = makeHandFromCounts(pawn, lance, knight, silver, gold, bishop, rook);
        result.nouse = list->state.nouse + totalHand(result.hand) - totalHand(list->state.hand);
        result.hinc = result.nouse > 0;
        result.nouse2 = result.nouse + totalHand(attacker) - totalHand(result.hand);
        result.inc = result.nouse2 > 0;
        result.current = false;
        return result;
    }

    TTState makeNoMateByOrNodeLL(const Position& pos, const MvList* list) {
        // disproofHandOr for linked list
        Hand hand;
        bool first = true;
        for (const MvList* p = list; p; p = p->next) {
            if (p->state.data.dn != 0) continue;
            const Hand unit = undoOrMoveOnDisproofHand(pos, p->headMove(), p->state.hand);
            hand = first ? unit : minHand(hand, unit);
            first = false;
        }
        if (first) {
            hand = disproofSeedHand(pos.hand(pos.turn()), pos.hand(oppositeColor(pos.turn())));
        } else {
            const Hand attacker = pos.hand(pos.turn());
            Hand filtered = zeroHand();
            if (attacker.exists<HPawn>()) setHandCount(filtered, HPawn, countOf<HPawn>(hand));
            if (attacker.exists<HLance>()) setHandCount(filtered, HLance, countOf<HLance>(hand));
            if (attacker.exists<HKnight>()) setHandCount(filtered, HKnight, countOf<HKnight>(hand));
            if (attacker.exists<HSilver>()) setHandCount(filtered, HSilver, countOf<HSilver>(hand));
            if (attacker.exists<HGold>()) setHandCount(filtered, HGold, countOf<HGold>(hand));
            if (attacker.exists<HBishop>()) setHandCount(filtered, HBishop, countOf<HBishop>(hand));
            if (attacker.exists<HRook>()) setHandCount(filtered, HRook, countOf<HRook>(hand));
            hand = filtered;
        }
        TTState result;
        result.data = { kInfinite, 0, list->state.data.sh + 1 };
        result.hand = hand;
        result.inc = (hand == pos.hand(pos.turn()));
        result.hinc = false;
        result.nouse = 0;
        result.nouse2 = 0;
        result.current = false;
        return result;
    }

    // Probe AND group head move in TT
    void probeAndGroupLL(MvList* group, const Position& pos, TranspositionTable& tt) {
        const auto childProbe = tt.ProbeChild<false>(pos, group->headMove());
        group->state = childProbe.state;
        group->searched = childProbe.cutoff;
    }

    // Expand front mate groups for AND node (linked list version)
    bool expandFrontMateGroupsLL(MvList*& list, const Position& pos, TranspositionTable& tt, MvListPool& pool) {
        bool expanded = false;
        while (list && list->state.data.pn == 0 && list->mlist && list->mlist->next) {
            // Split: create new node with remaining moves
            MvList* split = pool.allocMvList();
            split->mlist = list->mlist->next;
            list->mlist->next = nullptr;
            split->length = list->length;
            probeAndGroupLL(split, pos, tt);
            // Insert at front
            split->next = list;
            list = split;
            expanded = true;
        }
        return expanded;
    }
}

void TranspositionTable::NewSearch() {
    // Clear hash index table only — O(tableSize) not O(poolSize)
    std::memset(table_, 0, sizeof(ZFolder*) * tableSize_);
    // Reset pool counters — free lists empty, all fresh from pool
    zStack_ = nullptr;
    mStack_ = nullptr;
    tStack_ = nullptr;
    zNext_ = 0;
    mNext_ = 0;
    tNext_ = 0;
    num_ = 0;
}

void TranspositionTable::Allocate(uint64_t hashMB) {
    if (hashMB == 0) hashMB = 1;
    poolSize_ = hashMB * kMCardsPerMB;

    // Table size: smallest power-of-2 >= poolSize_, min 2^14 (matching shtsume)
    uint64_t bits = 14;
    while (bits < 30 && (1ULL << bits) < poolSize_) bits++;
    tableSize_ = 1ULL << bits;
    tableMask_ = tableSize_ - 1;

    table_ = static_cast<ZFolder**>(std::calloc(tableSize_, sizeof(ZFolder*)));
    zPool_ = static_cast<ZFolder*>(std::malloc(sizeof(ZFolder) * poolSize_));
    mPool_ = static_cast<MCard*>(std::malloc(sizeof(MCard) * poolSize_));
    tPool_ = static_cast<TList*>(std::malloc(sizeof(TList) * poolSize_));
    zStack_ = nullptr;
    mStack_ = nullptr;
    tStack_ = nullptr;
    zNext_ = 0;
    mNext_ = 0;
    tNext_ = 0;
    num_ = 0;
}

void TranspositionTable::Deallocate() {
    std::free(table_);
    std::free(zPool_);
    std::free(mPool_);
    std::free(tPool_);
    table_ = nullptr;
    zPool_ = nullptr;
    mPool_ = nullptr;
    tPool_ = nullptr;
}

TranspositionTable::TranspositionTable() {
    Allocate(kDefaultHashMB);
}

TranspositionTable::TranspositionTable(uint64_t hashMB) {
    Allocate(hashMB);
}

TranspositionTable::~TranspositionTable() {
    Deallocate();
}

void TranspositionTable::Resize(uint64_t hashMB) {
    Deallocate();
    Allocate(hashMB);
}

// Pool allocators: try free list first, then fresh from pool via counter.
// Each alloc memsets the node to 0, so no stale data.
TranspositionTable::TList* TranspositionTable::allocTList() {
    TList* t;
    if (tStack_) {
        t = tStack_;
        tStack_ = t->next;
    } else if (tNext_ < poolSize_) {
        t = &tPool_[tNext_++];
    } else {
        return nullptr;  // pool exhausted
    }
    std::memset(t, 0, sizeof(TList));
    t->data.pn = 1;
    t->data.dn = 1;
    num_++;
    return t;
}

TranspositionTable::MCard* TranspositionTable::allocMCard() {
    MCard* m;
    if (mStack_) {
        m = mStack_;
        mStack_ = m->next;
    } else if (mNext_ < poolSize_) {
        m = &mPool_[mNext_++];
    } else {
        return nullptr;
    }
    std::memset(m, 0, sizeof(MCard));
    m->current_pn = 1;
    return m;
}

TranspositionTable::ZFolder* TranspositionTable::allocZFolder() {
    ZFolder* z;
    if (zStack_) {
        z = zStack_;
        zStack_ = z->next;
    } else if (zNext_ < poolSize_) {
        z = &zPool_[zNext_++];
    } else {
        return nullptr;
    }
    std::memset(z, 0, sizeof(ZFolder));
    return z;
}

void TranspositionTable::freeTList(TList* tlist) {
    if (!tlist) return;
    TList* last = tlist;
    while (true) {
        num_--;
        if (!last->next) break;
        last = last->next;
    }
    last->next = tStack_;
    tStack_ = tlist;
}

void TranspositionTable::freeMCard(MCard* mcard) {
    mcard->next = mStack_;
    mStack_ = mcard;
}

void TranspositionTable::freeZFolder(ZFolder* zf) {
    zf->next = zStack_;
    zStack_ = zf;
}

TranspositionTable::ZFolder* TranspositionTable::findFolder(Key key) {
    const uint64_t address = key & tableMask_;
    ZFolder* zf = table_[address];
    while (zf) {
        if (zf->key == key) return zf;
        zf = zf->next;
    }
    return nullptr;
}

TranspositionTable::ZFolder* TranspositionTable::findOrCreateFolder(Key key) {
    const uint64_t address = key & tableMask_;
    ZFolder* zf = table_[address];
    while (zf) {
        if (zf->key == key) return zf;
        zf = zf->next;
    }
    ZFolder* newZf = allocZFolder();
    if (!newZf) return nullptr;
    newZf->key = key;
    newZf->next = table_[address];
    table_[address] = newZf;
    return newZf;
}

TranspositionTable::Relation TranspositionTable::Compare(const Hand lhs, const Hand rhs) {
    if (lhs == rhs) {
        return Relation::Equal;
    }
    if (lhs.isEqualOrSuperior(rhs)) {
        return Relation::Super;
    }
    if (rhs.isEqualOrSuperior(lhs)) {
        return Relation::Infer;
    }
    return Relation::None;
}

TranspositionTable::CardKind TranspositionTable::Kind(const MCard* card) {
    const TList* resolved = card->findResolved();
    if (!resolved) {
        return CardKind::Unknown;
    }
    if (resolved->data.pn == 0) {
        return CardKind::Mate;
    }
    if (resolved->data.dn == 0) {
        return CardKind::NoMate;
    }
    return CardKind::Unknown;
}

bool TranspositionTable::PreferSuperiorMate(const Hand& current, const Hand& candidate) {
    return current != candidate && current.isEqualOrSuperior(candidate);
}

bool TranspositionTable::PreferInferiorNoMate(const Hand& current, const Hand& candidate) {
    return current != candidate && candidate.isEqualOrSuperior(current);
}

TranspositionTable::MCard* TranspositionTable::EnsureCard(ZFolder* folder, const Hand hand, const uint16_t ply) {
    for (MCard* card = folder->mcard; card; card = card->next) {
        if (card->hand == hand) {
            if (!card->findRecord(ply) && !card->findResolved()) {
                TList* t = allocTList();
                if (t) {
                    t->dp = ply;
                    t->next = card->tlist;
                    card->tlist = t;
                }
            }
            return card;
        }
    }
    // Create new MCard
    MCard* card = allocMCard();
    if (!card) return nullptr;
    card->hand = hand;
    TList* t = allocTList();
    if (t) {
        t->dp = ply;
        t->next = nullptr;
        card->tlist = t;
    }
    card->next = folder->mcard;
    folder->mcard = card;
    return card;
}

template <bool or_node>
Hand TranspositionTable::MakeHand(const Position& pos) {
    return or_node ? pos.hand(pos.turn()) : pos.hand(oppositeColor(pos.turn()));
}

template <bool or_node>
Hand TranspositionTable::MakeChildHand(const Position& pos, const Move move) {
    if constexpr (or_node) {
        Hand hand = pos.hand(pos.turn());
        if (move.isDrop()) {
            hand.minusOne(move.handPieceDropped());
        }
        else {
            const Piece captured = pos.piece(move.to());
            if (captured != Empty) {
                hand.plusOne(pieceTypeToHandPiece(pieceToPieceType(captured)));
            }
        }
        return hand;
    }
    return pos.hand(oppositeColor(pos.turn()));
}

TranspositionTable::ProbeResult TranspositionTable::Probe(Key key, const Hand hand, const uint16_t ply) {
    ProbeResult result;
    result.state.hand = hand;

    ZFolder* folder = findFolder(key);
    if (!folder) {
        return result;
    }

    const MCard* superMate = nullptr;
    const MCard* inferNoMate = nullptr;
    const MCard* equalMate = nullptr;
    const MCard* equalNoMate = nullptr;
    MCard* equalUnknown = nullptr;
    int tmpPn = 1;
    int tmpDn = 1;

    for (MCard* card = folder->mcard; card; card = card->next) {
        const Relation rel = Compare(hand, card->hand);
        const CardKind kind = Kind(card);
        if (rel == Relation::Super) {
            if (kind == CardKind::Mate) {
                if (!superMate || PreferSuperiorMate(superMate->hand, card->hand)) {
                    superMate = card;
                }
            }
            else if (kind == CardKind::Unknown) {
                // Reduce tmpPn from SUPER Unknown (shtsume: no current check here)
                for (TList* t = card->tlist; t; t = t->next) {
                    if (tmpPn > t->data.pn) {
                        tmpPn = std::min(tmpPn, t->data.pn);
                    }
                }
            }
        }
        else if (rel == Relation::Infer) {
            if (kind == CardKind::NoMate) {
                if (!inferNoMate || PreferInferiorNoMate(inferNoMate->hand, card->hand)) {
                    inferNoMate = card;
                }
            }
            else if (kind == CardKind::Unknown) {
                for (TList* t = card->tlist; t; t = t->next) {
                    tmpPn = std::max(tmpPn, t->data.pn);
                }
                if (card->current) {
                    tmpPn = std::max(tmpPn, card->current_pn);
                }
            }
        }
        else if (rel == Relation::Equal) {
            if (kind == CardKind::Mate) {
                if (!equalMate) {
                    equalMate = card;
                }
            }
            else if (kind == CardKind::NoMate) {
                equalNoMate = card;
                break;
            }
            else if (!equalUnknown) {
                equalUnknown = card;
            }
        }
    }

    const auto loadResolved = [&](const MCard* card, const Relation rel) {
        const TList* record = card->findResolved();
        assert(record);
        result.state.data = record->data;
        result.state.hand = card->hand;
        result.state.inc = false;
        result.state.hinc = false;
        result.state.nouse = 0;
        result.state.nouse2 = 0;
        result.state.protect = card->protect;
        result.state.current = false;
        if (rel == Relation::Super && record->data.pn == 0) {
            result.state.inc = true;
            result.state.hinc = card->hinc;
            result.state.nouse = card->nouse;
            result.state.nouse2 = totalHand(hand) - totalHand(card->hand) + card->nouse;
        }
        else if (rel == Relation::Equal && record->data.pn == 0) {
            result.state.inc = card->hinc;
            result.state.hinc = card->hinc;
            result.state.nouse = card->nouse;
            result.state.nouse2 = card->nouse;
            result.state.current = card->current;
        }
        else if (rel == Relation::Infer && record->data.dn == 0) {
            result.state.inc = false;
        }
        else if (rel == Relation::Equal && record->data.dn == 0) {
            result.state.inc = false;
        }
        result.cutoff = true;
    };

    if (superMate) {
        loadResolved(superMate, Relation::Super);
        return result;
    }
    if (inferNoMate) {
        loadResolved(inferNoMate, Relation::Infer);
        return result;
    }
    if (equalMate) {
        loadResolved(equalMate, Relation::Equal);
        return result;
    }
    if (equalNoMate) {
        loadResolved(equalNoMate, Relation::Equal);
        return result;
    }
    if (equalUnknown) {
        if (equalUnknown->current) {
            result.state.data = { std::max(1, equalUnknown->current_pn), 1, 0 };
            result.state.current = true;
            result.state.protect = equalUnknown->protect;
            result.cutoff = true;
            return result;
        }
        bool hit = false;
        for (TList* t = equalUnknown->tlist; t; t = t->next) {
            if (t->dp == ply) {
                result.state.data = t->data;
                result.state.hinc = equalUnknown->hinc;
                result.state.nouse = equalUnknown->nouse;
                result.state.protect = equalUnknown->protect;
                result.state.current = equalUnknown->current;
                hit = true;
            }
            tmpPn = std::max(tmpPn, t->data.pn);
            tmpDn = std::max(tmpDn, t->data.dn);
        }
        if (hit) {
            result.cutoff = true;
            return result;
        }
        // Create new TList for this depth (matching shtsume lookup behavior)
        TList* newT = allocTList();
        if (newT) {
            newT->dp = ply;
            newT->data = { tmpPn, tmpDn, 0 };
            newT->next = equalUnknown->tlist;
            equalUnknown->tlist = newT;
        }
        result.state.data = { tmpPn, tmpDn, 0 };
        result.state.hinc = equalUnknown->hinc;
        result.state.nouse = equalUnknown->nouse;
        result.state.protect = equalUnknown->protect;
        return result;
    }

    result.state.data = { tmpPn, tmpDn, 0 };
    return result;
}

template <bool or_node>
TranspositionTable::ProbeResult TranspositionTable::Probe(const Position& pos) {
    return Probe(pos.getBoardKey(), MakeHand<or_node>(pos), static_cast<uint16_t>(pos.gamePly()));
}

template <bool or_node>
TranspositionTable::ProbeResult TranspositionTable::ProbeChild(const Position& pos, const Move move) {
    return Probe(pos.getBoardKeyAfter(move), MakeChildHand<or_node>(pos, move), static_cast<uint16_t>(pos.gamePly() + 1));
}

bool TranspositionTable::ProbeIsMate(Key boardKey, Hand hand, uint16_t ply) {
    ProbeResult result = Probe(boardKey, hand, ply);
    return result.cutoff && result.state.data.pn == 0;
}

int TranspositionTable::ProbePn(Key boardKey, Hand hand, uint16_t ply) {
    ZFolder* folder = findFolder(boardKey);
    if (!folder) return -1;
    ProbeResult result = Probe(boardKey, hand, ply);
    if (!result.cutoff) return -1;
    return result.state.data.pn;
}

template <bool or_node>
void TranspositionTable::SetCurrent(const Position& pos, const int current_pn) {
    ZFolder* folder = findOrCreateFolder(pos.getBoardKey());
    if (!folder) return;
    MCard* card = EnsureCard(folder, MakeHand<or_node>(pos), static_cast<uint16_t>(pos.gamePly()));
    if (card) {
        card->current = true;
        card->current_pn = current_pn;
    }
}

template <bool or_node>
void TranspositionTable::ClearCurrent(const Position& pos) {
    ZFolder* folder = findFolder(pos.getBoardKey());
    if (!folder) return;
    const Hand hand = MakeHand<or_node>(pos);
    for (MCard* card = folder->mcard; card; card = card->next) {
        if (card->hand == hand) {
            card->current = false;
            return;
        }
    }
}

void TranspositionTable::ClearProtect() {
    // Walk entire table and clear all protect flags (matching shtsume tbase_clear_protect)
    for (uint64_t i = 0; i < tableSize_; i++) {
        for (ZFolder* zf = table_[i]; zf; zf = zf->next) {
            for (MCard* mc = zf->mcard; mc; mc = mc->next) {
                mc->protect = false;
            }
        }
    }
}

template <bool or_node>
void TranspositionTable::SetProtect(const Position& pos) {
    ZFolder* folder = findFolder(pos.getBoardKey());
    if (!folder) return;
    const Hand hand = MakeHand<or_node>(pos);
    for (MCard* card = folder->mcard; card; card = card->next) {
        const Relation rel = Compare(hand, card->hand);
        if ((rel == Relation::Equal || rel == Relation::Super) && Kind(card) == CardKind::Mate) {
            card->protect = true;
        }
    }
}

TTState TranspositionTable::StoreMate(ZFolder* folder, const Hand query_hand, const TTState& state) {
    // Step 1: Remove existing mcard with sdata's actual hand (matching shtsume tsumi_update step 1)
    MCard* prev = nullptr;
    for (MCard* mc = folder->mcard; mc; ) {
        if (mc->hand == query_hand) {
            if (prev) prev->next = mc->next;
            else folder->mcard = mc->next;
            freeTList(mc->tlist);
            mc->tlist = nullptr;
            MCard* tmp = mc;
            mc = mc->next;
            freeMCard(tmp);
            break;
        }
        prev = mc;
        mc = mc->next;
    }

    // Step 2: Register with PROOF pieces key (state.hand)
    bool updated = false;
    prev = nullptr;
    for (MCard* mc = folder->mcard; mc; ) {
        const Relation rel = Compare(state.hand, mc->hand);
        const CardKind kind = Kind(mc);
        if (rel == Relation::Super) {
            prev = mc;
            mc = mc->next;
        }
        else if (rel == Relation::Infer) {
            // Delete — new proof is tighter (matching shtsume)
            MCard* tmp = mc;
            mc = mc->next;
            if (prev) prev->next = mc;
            else folder->mcard = mc;
            freeTList(tmp->tlist);
            tmp->tlist = nullptr;
            freeMCard(tmp);
        }
        else if (rel == Relation::Equal) {
            if (kind == CardKind::Mate) {
                TList* record = mc->findResolved();
                if (record->data.sh > state.data.sh || mc->hinc != state.hinc || mc->nouse != state.nouse) {
                    record->data = state.data;
                    mc->hinc = state.hinc;
                    mc->nouse = state.nouse;
                }
                if (state.protect && !mc->protect) {
                    mc->protect = true;
                }
                updated = true;
            }
            else if (kind == CardKind::Unknown) {
                // Replace all tlist with single resolved entry
                freeTList(mc->tlist);
                mc->tlist = nullptr;
                TList* t = allocTList();
                if (t) {
                    t->dp = kAllDepth;
                    t->data = state.data;
                    mc->tlist = t;
                }
                mc->hinc = state.hinc;
                mc->nouse = state.nouse;
                if (state.protect) mc->protect = true;
                updated = true;
            }
            prev = mc;
            mc = mc->next;
        }
        else {
            prev = mc;
            mc = mc->next;
        }
    }

    if (!updated) {
        MCard* newMc = allocMCard();
        if (newMc) {
            newMc->hand = state.hand;
            TList* t = allocTList();
            if (t) {
                t->dp = kAllDepth;
                t->data = state.data;
                newMc->tlist = t;
            }
            newMc->hinc = state.hinc;
            newMc->nouse = state.nouse;
            if (state.protect) newMc->protect = true;
            newMc->next = folder->mcard;
            folder->mcard = newMc;
        }
    }
    return state;
}

TTState TranspositionTable::StoreNoMate(ZFolder* folder, const Hand query_hand, const TTState& state) {
    // Step 1: Remove existing mcard with actual hand
    MCard* prev = nullptr;
    for (MCard* mc = folder->mcard; mc; ) {
        if (mc->hand == query_hand) {
            if (prev) prev->next = mc->next;
            else folder->mcard = mc->next;
            freeTList(mc->tlist);
            mc->tlist = nullptr;
            MCard* tmp = mc;
            mc = mc->next;
            freeMCard(tmp);
            break;
        }
        prev = mc;
        mc = mc->next;
    }

    // Step 2: Register with DISPROOF pieces key
    bool updated = false;
    prev = nullptr;
    for (MCard* mc = folder->mcard; mc; ) {
        const Relation rel = Compare(state.hand, mc->hand);
        const CardKind kind = Kind(mc);
        if (rel == Relation::Super) {
            // Delete — opposite of mate (matching shtsume fudumi_update)
            MCard* tmp = mc;
            mc = mc->next;
            if (prev) prev->next = mc;
            else folder->mcard = mc;
            freeTList(tmp->tlist);
            tmp->tlist = nullptr;
            freeMCard(tmp);
        }
        else if (rel == Relation::Infer) {
            prev = mc;
            mc = mc->next;
        }
        else if (rel == Relation::Equal) {
            if (kind == CardKind::Unknown) {
                freeTList(mc->tlist);
                mc->tlist = nullptr;
                TList* t = allocTList();
                if (t) {
                    t->dp = kAllDepth;
                    t->data = state.data;
                    mc->tlist = t;
                }
                mc->hinc = state.hinc;
                mc->nouse = state.nouse;
                if (state.protect) mc->protect = true;
            }
            else if (kind == CardKind::NoMate && state.protect && !mc->protect) {
                mc->protect = true;
            }
            updated = true;
            prev = mc;
            mc = mc->next;
        }
        else {
            prev = mc;
            mc = mc->next;
        }
    }

    if (!updated) {
        MCard* newMc = allocMCard();
        if (newMc) {
            newMc->hand = state.hand;
            TList* t = allocTList();
            if (t) {
                t->dp = kAllDepth;
                t->data = state.data;
                newMc->tlist = t;
            }
            newMc->hinc = state.hinc;
            newMc->nouse = state.nouse;
            if (state.protect) newMc->protect = true;
            newMc->next = folder->mcard;
            folder->mcard = newMc;
        }
    }
    return state;
}

TTState TranspositionTable::StoreUnknown(ZFolder* folder, const Hand query_hand, const uint16_t ply, const TTState& state) {
    const MCard* superMate = nullptr;
    const MCard* inferNoMate = nullptr;
    const MCard* equalMate = nullptr;
    const MCard* equalNoMate = nullptr;
    MCard* equalUnknown = nullptr;

    for (MCard* mc = folder->mcard; mc; mc = mc->next) {
        const Relation rel = Compare(query_hand, mc->hand);
        const CardKind kind = Kind(mc);
        if (rel == Relation::Super) {
            if (kind == CardKind::Mate) {
                if (!superMate || PreferSuperiorMate(superMate->hand, mc->hand)) {
                    superMate = mc;
                }
            }
            else if (kind == CardKind::Unknown && !mc->current) {
                // Propagate: raise stored pn (matching shtsume fumei_update SUPER)
                for (TList* t = mc->tlist; t; t = t->next) {
                    if (state.data.pn > t->data.pn) {
                        t->data.pn = state.data.pn;
                    }
                }
            }
        }
        else if (rel == Relation::Infer) {
            if (kind == CardKind::NoMate) {
                if (!inferNoMate || PreferInferiorNoMate(inferNoMate->hand, mc->hand)) {
                    inferNoMate = mc;
                }
            }
            else if (kind == CardKind::Unknown && !mc->current) {
                // Propagate: lower stored pn (matching shtsume fumei_update INFER)
                for (TList* t = mc->tlist; t; t = t->next) {
                    if (state.data.pn < t->data.pn) {
                        t->data.pn = state.data.pn;
                    }
                }
            }
        }
        else if (rel == Relation::Equal) {
            if (kind == CardKind::Mate) {
                if (!equalMate) equalMate = mc;
            }
            else if (kind == CardKind::NoMate) {
                if (!equalNoMate) equalNoMate = mc;
            }
            else if (!equalUnknown) {
                equalUnknown = mc;
            }
        }
    }

    const auto resolvedState = [&](const MCard* mc, const Relation rel) {
        const TList* record = mc->findResolved();
        assert(record);
        TTState resolved;
        resolved.data = record->data;
        resolved.hand = mc->hand;
        resolved.protect = mc->protect;
        if (rel == Relation::Super && record->data.pn == 0) {
            resolved.inc = true;
            resolved.hinc = mc->hinc;
            resolved.nouse = mc->nouse;
            resolved.nouse2 = totalHand(query_hand) - totalHand(mc->hand) + mc->nouse;
        }
        else if (rel == Relation::Equal && record->data.pn == 0) {
            resolved.inc = mc->hinc;
            resolved.hinc = mc->hinc;
            resolved.nouse = mc->nouse;
            resolved.nouse2 = mc->nouse;
            resolved.current = mc->current;
        }
        return resolved;
    };

    if (superMate) return resolvedState(superMate, Relation::Super);
    if (inferNoMate) return resolvedState(inferNoMate, Relation::Infer);
    if (equalMate) return resolvedState(equalMate, Relation::Equal);
    if (equalNoMate) return resolvedState(equalNoMate, Relation::Equal);

    if (equalUnknown) {
        TList* same = nullptr;
        for (TList* t = equalUnknown->tlist; t; t = t->next) {
            if (t->dp == ply) {
                same = t;
            }
            else {
                t->data.pn = state.data.pn;
            }
        }
        if (same) {
            same->data = state.data;
        }
        else {
            TList* newT = allocTList();
            if (newT) {
                newT->dp = ply;
                newT->data = state.data;
                newT->next = equalUnknown->tlist;
                equalUnknown->tlist = newT;
            }
        }
        equalUnknown->hinc = state.hinc;
        equalUnknown->nouse = state.nouse;
        if (state.protect) equalUnknown->protect = true;
        return { state.data, query_hand, state.inc, state.hinc, state.nouse, state.nouse2, equalUnknown->protect, equalUnknown->current };
    }

    // No matching mcard → create new
    MCard* newMc = allocMCard();
    if (newMc) {
        newMc->hand = query_hand;
        TList* t = allocTList();
        if (t) {
            t->dp = ply;
            t->data = state.data;
            newMc->tlist = t;
        }
        newMc->hinc = state.hinc;
        newMc->nouse = state.nouse;
        if (state.protect) newMc->protect = true;
        newMc->next = folder->mcard;
        folder->mcard = newMc;
    }
    return { state.data, query_hand, state.inc, state.hinc, state.nouse, state.nouse2, state.protect, false };
}

TTState TranspositionTable::Store(const Key key, const Hand query_hand, const uint16_t ply, const TTState& state) {
    ZFolder* folder = findOrCreateFolder(key);
    if (!folder) return state;
    if (state.data.pn == 0) {
        return StoreMate(folder, query_hand, state);
    }
    if (state.data.dn == 0) {
        return StoreNoMate(folder, query_hand, state);
    }
    return StoreUnknown(folder, query_hand, ply, state);
}

template TranspositionTable::ProbeResult TranspositionTable::Probe<true>(const Position& pos);
template TranspositionTable::ProbeResult TranspositionTable::Probe<false>(const Position& pos);
template TranspositionTable::ProbeResult TranspositionTable::ProbeChild<true>(const Position& pos, Move move);
template TranspositionTable::ProbeResult TranspositionTable::ProbeChild<false>(const Position& pos, Move move);
template void TranspositionTable::SetCurrent<true>(const Position& pos, int current_pn);
template void TranspositionTable::SetCurrent<false>(const Position& pos, int current_pn);
template void TranspositionTable::ClearCurrent<true>(const Position& pos);
template void TranspositionTable::ClearCurrent<false>(const Position& pos);
template void TranspositionTable::SetProtect<true>(const Position& pos);
template void TranspositionTable::SetProtect<false>(const Position& pos);

namespace {
    bool andMoveLess(const Position& pos, const Move lhs, const Move rhs) {
        if (lhs.isDrop() != rhs.isDrop()) return !lhs.isDrop();
        const bool lhsCapture = !lhs.isDrop() && pos.piece(lhs.to()) != Empty;
        const bool rhsCapture = !rhs.isDrop() && pos.piece(rhs.to()) != Empty;
        if (lhsCapture != rhsCapture) return lhsCapture;
        const int lhsPriority = movingPiecePriority(pos, lhs);
        const int rhsPriority = movingPiecePriority(pos, rhs);
        if (lhsPriority != rhsPriority) return lhsPriority < rhsPriority;
        const bool lhsKing = isKingMove(pos, lhs);
        const bool rhsKing = isKingMove(pos, rhs);
        if (lhsKing != rhsKing) return lhsKing;
        if (lhs.isPromotion() != rhs.isPromotion()) return lhs.isPromotion();
        if (lhs.isDrop() && rhs.isDrop()) return lhs.handPieceDropped() < rhs.handPieceDropped();
        return lhs.value() < rhs.value();
    }

    bool isBigPieceType(const PieceType pt) {
        return pt == Bishop || pt == Rook || pt == Horse || pt == Dragon;
    }

    bool isPromotePairType(const PieceType pt) {
        return pt == Pawn || pt == Lance || pt == Knight || pt == Silver;
    }

    PieceType movingPieceType(const Position& pos, const Move move) {
        if (move.isDrop()) {
            return handPieceToPieceType(move.handPieceDropped());
        }
        return pieceToPieceType(pos.piece(move.from()));
    }

    int interpositionOrderKey(const Position& pos, const Move move) {
        const Square kingSq = pos.kingSquare(pos.turn());
        return squareDistance(kingSq, move.to());
    }

    SquareDelta stepTowards(const Square from, const Square to) {
        const int fileDiff = static_cast<int>(makeFile(to)) - static_cast<int>(makeFile(from));
        const int rankDiff = static_cast<int>(makeRank(to)) - static_cast<int>(makeRank(from));
        const int df = (fileDiff > 0) - (fileDiff < 0);
        const int dr = (rankDiff > 0) - (rankDiff < 0);
        if (df == 0 && dr < 0) return DeltaN;
        if (df < 0 && dr < 0) return DeltaNE;
        if (df < 0 && dr == 0) return DeltaE;
        if (df < 0 && dr > 0) return DeltaSE;
        if (df == 0 && dr > 0) return DeltaS;
        if (df > 0 && dr > 0) return DeltaSW;
        if (df > 0 && dr == 0) return DeltaW;
        if (df > 0 && dr < 0) return DeltaNW;
        return DeltaNothing;
    }

    struct AndGroupKey {
        AndGroupKind kind = AndGroupKind::Single;
        Square to = SQ11;
        Square from = SQ11;
    };

    bool sameAndGroupKey(const AndGroupKey& lhs, const AndGroupKey& rhs) {
        return lhs.kind == rhs.kind && lhs.to == rhs.to && lhs.from == rhs.from;
    }

    AndGroupKey classifyAndGroupKey(const Position& pos, const Move move, const bool groupedMoveInterpositions) {
        if (!isInterpositionEvasion(pos, move)) {
            return {};
        }
        if (move.isDrop()) {
            // All interposition drops share one group (shtsume: single drop_list)
            return { AndGroupKind::DropSerial, SQ11, SQ11 };
        }
        if (!groupedMoveInterpositions) {
            // All move interpositions share one group (shtsume Pattern B: single move_list)
            return { AndGroupKind::MoveSerial, SQ11, SQ11 };
        }

        const PieceType pt = movingPieceType(pos, move);
        if (isPromotePairType(pt)) {
            return { AndGroupKind::PromotePair, move.to(), move.from() };
        }
        if (pt == ProPawn) {
            return { AndGroupKind::TokinDest, move.to(), SQ11 };
        }
        if (isBigPieceType(pt)) {
            return { AndGroupKind::BigPieceDest, move.to(), SQ11 };
        }
        return { AndGroupKind::Single, move.to(), move.from() };
    }

    AndNodeFlags analyzeAndNode(const Position& pos, const MovePicker<false>& movePicker) {
        AndNodeFlags flags;
        Square checkerSq;
        Square kingSq;
        PieceType checkerPt;
        if (!sliderCheckInfo(pos, checkerSq, kingSq, checkerPt)) {
            return flags;
        }
        flags.ryuma_flag = squareDistance(checkerSq, kingSq) > 2;
        // Use flags computed by filterInterpositions (shtsume-equivalent semantics)
        flags.proof_flag = movePicker.proofFlag();
        flags.invalid_flag = movePicker.invalidFlag();
        return flags;
    }

    // -----------------------------------------------------------------------
    // Linked-list buildAndGroups (for dfpn_inner hot path)
    // -----------------------------------------------------------------------

    // Sort MList chain using insertion sort (small chains)
    MList* sortMListChain(MList* head, const Position& pos, const AndGroupKind kind) {
        if (!head || !head->next) return head;
        MList sorted_sentinel;
        sorted_sentinel.next = nullptr;
        MList* cur = head;
        while (cur) {
            MList* next = cur->next;
            // Find insertion point
            MList* prev = &sorted_sentinel;
            while (prev->next) {
                bool less = false;
                const Move a = cur->move, b = prev->next->move;
                if (kind == AndGroupKind::DropSerial) {
                    const int ao = interpositionOrderKey(pos, a), bo = interpositionOrderKey(pos, b);
                    if (ao != bo) less = ao < bo;
                    else if (a.handPieceDropped() != b.handPieceDropped()) less = a.handPieceDropped() < b.handPieceDropped();
                    else less = a.value() < b.value();
                } else if (kind == AndGroupKind::MoveSerial) {
                    const int ao = interpositionOrderKey(pos, a), bo = interpositionOrderKey(pos, b);
                    if (ao != bo) less = ao < bo;
                    else less = andMoveLess(pos, a, b);
                } else {
                    less = andMoveLess(pos, a, b);
                }
                if (less) break;
                prev = prev->next;
            }
            cur->next = prev->next;
            prev->next = cur;
            cur = next;
        }
        return sorted_sentinel.next;
    }

    MvList* buildAndGroupsLL(const Position& pos, const MovePicker<false>& movePicker, TranspositionTable& tt, MvListPool& pool) {
        // Reuse existing vector-based grouping logic then convert to linked list
        bool groupedMoveInterpositions = false;
        for (const auto& extMove : movePicker) {
            if (!isInterpositionEvasion(pos, extMove.move) || extMove.move.isDrop()) {
                groupedMoveInterpositions = true;
                break;
            }
        }

        // Build groups into a temporary array, then convert to MvList chain
        struct TmpGroup {
            MList* mlist = nullptr;
            AndGroupKind kind = AndGroupKind::Single;
        };
        std::vector<TmpGroup> tmpGroups;
        tmpGroups.reserve(movePicker.size());
        std::vector<std::pair<AndGroupKey, size_t>> keys;

        for (const auto& extMove : movePicker) {
            const Move move = extMove.move;
            const AndGroupKey key = classifyAndGroupKey(pos, move, groupedMoveInterpositions);

            MList* ml = pool.allocMList();
            ml->move = move;
            ml->next = nullptr;

            if (key.kind == AndGroupKind::Single) {
                tmpGroups.push_back({ ml, AndGroupKind::Single });
                continue;
            }

            auto it = std::find_if(keys.begin(), keys.end(), [&](const auto& entry) {
                return sameAndGroupKey(entry.first, key);
            });
            if (it == keys.end()) {
                keys.emplace_back(key, tmpGroups.size());
                tmpGroups.push_back({ ml, key.kind });
            } else {
                // Append to existing group's mlist
                MList* last = MvListPool::mlistLast(tmpGroups[it->second].mlist);
                last->next = ml;
            }
        }

        // Sort mlist chains and convert to MvList linked list
        const Square enemyKing = pos.kingSquare(oppositeColor(pos.turn()));
        MvList* result = nullptr;
        for (int i = static_cast<int>(tmpGroups.size()) - 1; i >= 0; --i) {
            auto& tg = tmpGroups[i];
            tg.mlist = sortMListChain(tg.mlist, pos, tg.kind);
            MvList* node = pool.allocMvList();
            node->mlist = tg.mlist;
            node->length = squareDistance(tg.mlist->move.to(), enemyKing);
            node->next = result;
            result = node;
        }
        return result;
    }

    // expandInitialMateGroups for linked list
    bool expandInitialMateGroupsLL(MvList*& list, const Position& pos, TranspositionTable& tt, MvListPool& pool) {
        bool expanded = false;
        for (MvList* p = list; p; p = p->next) {
            probeAndGroupLL(p, pos, tt);
            if (p->state.data.pn == 0 && p->mlist && p->mlist->next) {
                MvList* split = pool.allocMvList();
                split->mlist = p->mlist->next;
                p->mlist->next = nullptr;
                split->length = p->length;
                // Insert split after p
                split->next = p->next;
                p->next = split;
                expanded = true;
                // Skip the newly inserted node
            }
        }
        return expanded;
    }

    // -----------------------------------------------------------------------
    // Shared helpers: build OR children / AND groups as MvList
    // -----------------------------------------------------------------------

    // Build OR children list from MovePicker, probing TT for each child.
    MvList* buildOrChildrenLL(const Position& pos, MovePicker<true>& movePicker, TranspositionTable& tt, MvListPool& pool) {
        const Color them = oppositeColor(pos.turn());
        const Square enemyKing = pos.kingSquare(them);
        MvList* head = nullptr;
        MvList* tail = nullptr;
        for (const auto& extMove : movePicker) {
            const Move move = extMove.move;
            auto childProbe = tt.ProbeChild<true>(pos, move);
            MvList* child = pool.allocMvList();
            child->mlist = pool.allocMList();
            child->mlist->move = move;
            child->mlist->next = nullptr;
            child->state = childProbe.state;
            child->searched = childProbe.cutoff;
            child->length = squareDistance(move.to(), enemyKing);
            child->next = nullptr;
            if (tail) { tail->next = child; } else { head = child; }
            tail = child;
        }
        return head;
    }

    // Build AND groups list from MovePicker, probe TT and expand initial mate groups.
    MvList* buildAndGroupsProbedLL(const Position& pos, MovePicker<false>& movePicker, TranspositionTable& tt, MvListPool& pool) {
        MvList* groups = buildAndGroupsLL(pos, movePicker, tt, pool);
        expandInitialMateGroupsLL(groups, pos, tt, pool);
        return groups;
    }
}

void DfPn::dfpn_stop(const bool stop) {
    this->stop = stop;
}
template <bool or_node>
DfPn::NodeState DfPn::finalizeNode(Position& pos, const Hand query_hand, const NodeState& state) {
    TTState stored = transposition_table.Store(
        pos.getBoardKey(),
        query_hand,
        static_cast<uint16_t>(pos.gamePly()),
        { { state.pn, state.dn, state.sh }, state.hand, state.inc, state.hinc, state.nouse, state.nouse2, state.protect, state.current });
    return { stored.data.pn, stored.data.dn, stored.data.sh, stored.hand, stored.inc, stored.hinc, stored.nouse, stored.nouse2, stored.protect, stored.current };
}

template <bool or_node>
DfPn::NodeState DfPn::make_tree_inner(Position& pos, const NodeState& base, const uint16_t maxDepth, uint32_t& searchedNode) {
    if (stop || pos.gamePly() >= maxDepth || base.pn != 0 || base.dn == 0 || base.sh <= 1 || base.inc || base.protect) {
        return base;
    }
    // Re-probe TT protect flag to avoid exponential re-traversal when
    // multiple AND branches reach the same position (siblings may have
    // already protected this node after our caller's initial probe).
    {
        const auto freshProbe = transposition_table.Probe<or_node>(pos);
        if (freshProbe.state.protect) {
            return base;
        }
    }

    transposition_table.SetProtect<or_node>(pos);

    if constexpr (or_node) {
        MovePicker<true> movePicker(pos);
        MvList* children = buildOrChildrenLL(pos, movePicker, transposition_table, mvListPool);

        // Convergent alternate mate search (shtsume: !tmp->search || (pn && pn < g_mt_min_pn)).
        {
            const int thPn = std::max(kMakeTreePnPlus + 1, kMtMinPn);
            for (MvList* child = children; child; child = child->next) {
                if (stop) break;
                if (!child->searched || (child->state.data.pn > 0 && child->state.data.pn < kMtMinPn)) {
                    StateInfo stateInfo;
                    pos.doMove(child->headMove(), stateInfo);
                    dfpn_inner<false>(pos, { thPn, kInfinite - 1, draw_ply }, maxDepth, searchedNode);
                    pos.undoMove(child->headMove());
                    child->state = transposition_table.ProbeChild<true>(pos, child->headMove()).state;
                }
            }
        }

        children = mvlistSort(children, [&](const MvList* a, const MvList* b) {
            return orChildCmp(a, b, pos);
        });

        // shtsume make_tree_or: process front proven child, mark searched, re-sort, repeat
        // Reset searched (used as shtsume pr flag for "already processed this call")
        for (MvList* p = children; p; p = p->next) { p->searched = false; }
        while (children && !children->state.inc && !children->state.protect && !children->searched) {
            if (stop || children->state.data.pn != 0 || children->state.current) {
                break;
            }
            {
                StateInfo stateInfo;
                pos.doMove(children->headMove(), stateInfo);
                const NodeState childBase{ children->state.data.pn, children->state.data.dn, children->state.data.sh, children->state.hand,
                    children->state.inc, children->state.hinc, children->state.nouse, children->state.nouse2, children->state.protect, children->state.current };
                make_tree_inner<false>(pos, childBase, maxDepth, searchedNode);
                pos.undoMove(children->headMove());
            }
            children->searched = true;  // equivalent to shtsume list->pr = 1
            children = mvlistSort(children, [&](const MvList* a, const MvList* b) {
                return orChildCmp(a, b, pos);
            });
        }
        // Local update (shtsume make_tree_update equivalent):
        // Re-probe children, re-sort, compute aggregate, write to TT.
        for (MvList* p = children; p; p = p->next) {
            p->state = transposition_table.ProbeChild<true>(pos, p->headMove()).state;
        }
        children = mvlistSort(children, [&](const MvList* a, const MvList* b) {
            return orChildCmp(a, b, pos);
        });
        {
            NodeState result;
            result.pn = children->state.data.pn;
            result.dn = children->state.data.dn;
            result.sh = children->state.data.sh + 1;
            result.current = false;
            result.protect = true;
            if (result.pn == 0) {
                const TTState r = makeMateByOrMove(pos, children->headMove(), children->state);
                result.hand = r.hand; result.inc = r.inc; result.hinc = r.hinc;
                result.nouse = r.nouse; result.nouse2 = r.nouse2;
            } else if (result.dn == 0) {
                const TTState r = makeNoMateByOrNodeLL(pos, children);
                result.hand = r.hand; result.inc = r.inc; result.hinc = r.hinc;
                result.nouse = r.nouse; result.nouse2 = r.nouse2;
            } else {
                result.hand = children->state.hand; result.inc = children->state.inc; result.hinc = children->state.hinc;
                result.nouse = children->state.nouse; result.nouse2 = children->state.nouse2;
            }
            mvListPool.freeMvList(children);
            return finalizeNode<true>(pos, pos.hand(pos.turn()), result);
        }
    }
    else {
        MovePicker<false> movePicker(pos, &transposition_table);
        MvList* groups = buildAndGroupsProbedLL(pos, movePicker, transposition_table, mvListPool);
        groups = mvlistSort(groups, [&](const MvList* a, const MvList* b) {
            return andGroupCmp(a, b, pos);
        });

        // Recovery loop (shtsume make_tree_and: re-prove children whose proof data was lost)
        while (groups && groups->state.data.pn != 0) {
            if (stop || groups->state.data.dn == 0) break;
            StateInfo stateInfo;
            pos.doMove(groups->headMove(), stateInfo);
            dfpn_inner<true>(pos, { kInfinite - 1, kInfinite - 1, draw_ply }, maxDepth, searchedNode);
            pos.undoMove(groups->headMove());
            probeAndGroupLL(groups, pos, transposition_table);
            groups = mvlistSort(groups, [&](const MvList* a, const MvList* b) {
                return andGroupCmp(a, b, pos);
            });
        }

        for (const MvList* group = groups; group; group = group->next) {
            if (stop || group->state.inc || group->state.current || group->state.protect) {
                continue;
            }
            StateInfo stateInfo;
            pos.doMove(group->headMove(), stateInfo);
            const NodeState childBase{ group->state.data.pn, group->state.data.dn, group->state.data.sh, group->state.hand,
                group->state.inc, group->state.hinc, group->state.nouse, group->state.nouse2, group->state.protect, group->state.current };
            make_tree_inner<true>(pos, childBase, maxDepth, searchedNode);
            pos.undoMove(group->headMove());
        }
        // Local update (shtsume make_tree_update equivalent)
        for (MvList* p = groups; p; p = p->next) {
            probeAndGroupLL(p, pos, transposition_table);
        }
        groups = mvlistSort(groups, [&](const MvList* a, const MvList* b) {
            return andGroupCmp(a, b, pos);
        });
        {
            NodeState result;
            result.pn = groups->state.data.pn;
            result.dn = groups->state.data.dn;
            result.sh = groups->state.data.sh + 1;
            result.current = false;
            result.protect = true;
            if (result.pn == 0) {
                const TTState r = makeMateByAndNodeLL(pos, groups);
                result.hand = r.hand; result.inc = r.inc; result.hinc = r.hinc;
                result.nouse = r.nouse; result.nouse2 = r.nouse2;
            } else if (result.dn == 0) {
                const TTState r = makeNoMateByAndMove(pos, groups->headMove(), groups->state);
                result.hand = r.hand; result.inc = r.inc; result.hinc = r.hinc;
                result.nouse = r.nouse; result.nouse2 = r.nouse2;
            } else {
                result.hand = groups->state.hand; result.inc = groups->state.inc; result.hinc = groups->state.hinc;
                result.nouse = groups->state.nouse; result.nouse2 = groups->state.nouse2;
            }
            mvListPool.freeMvList(groups);
            return finalizeNode<false>(pos, pos.hand(oppositeColor(pos.turn())), result);
        }
    }
}

template <bool or_node>
DfPn::NodeState DfPn::bns_plus_inner(Position& pos, const NodeState& base, const uint16_t maxDepth, uint32_t& searchedNode, int addThPn, int ptsh) {
    if (stop || pos.gamePly() >= maxDepth || base.pn != 0 || base.dn == 0 || base.sh <= 1) {
        return base;
    }

    // Default ptsh to base.sh at top-level entry.
    if (ptsh < 0) ptsh = base.sh;
    // shtsume: if(!ptsh) — depth budget exhausted, stop exploring.
    if (ptsh <= 0) {
        return base;
    }

    if constexpr (or_node) {
        MovePicker<true> movePicker(pos);
        MvList* children = buildOrChildrenLL(pos, movePicker, transposition_table, mvListPool);
        children = mvlistSort(children, [&](const MvList* a, const MvList* b) {
            return orChildCmp(a, b, pos);
        });

        // shtsume bns_plus_or: early exit if front is inc-mate or base-mate (sh==0)
        if (children->state.data.pn == 0 && (children->state.inc || children->state.data.sh == 0)) {
            NodeState result;
            result.pn = children->state.data.pn;
            result.dn = disproofNumber(children, nullptr);
            result.sh = children->state.data.sh + 1;
            result.current = false;
            result.protect = false;
            const TTState r = makeMateByOrMove(pos, children->headMove(), children->state);
            result.hand = r.hand; result.inc = r.inc; result.hinc = r.hinc;
            result.nouse = r.nouse; result.nouse2 = r.nouse2;
            mvListPool.freeMvList(children);
            return finalizeNode<true>(pos, pos.hand(pos.turn()), result);
        }

        // shtsume: tsh = ptsh-1, then MIN across proved siblings
        int tsh = ptsh - 1;
        for (MvList* child = children; child; child = child->next) {
            if (stop || child->state.current || child->state.protect) {
                continue;
            }
            StateInfo stateInfo;
            pos.doMove(child->headMove(), stateInfo);
            if (child->state.data.pn == 0) {
                tsh = std::min(tsh, static_cast<int>(child->state.data.sh));
                const NodeState childBase{ child->state.data.pn, child->state.data.dn, child->state.data.sh, child->state.hand,
                    child->state.inc, child->state.hinc, child->state.nouse, child->state.nouse2, child->state.protect, child->state.current };
                bns_plus_inner<false>(pos, childBase, maxDepth, searchedNode, addThPn, tsh);
                {
                    const auto probe = transposition_table.Probe<false>(pos);
                    child->state = probe.state;
                }
                if (child->state.data.pn == 0 && child->state.inc) {
                    pos.undoMove(child->headMove());
                    break;
                }
            }
            else if (child->state.data.pn > 0
                && child->state.data.pn < kInfinite - 1
                && base.sh > 1)
            {
                const int thPn = std::min(addThPn, std::max(static_cast<int>(child->state.data.pn), 0)) + 1;
                const int thSh = std::min(draw_ply, base.sh + kAddSearchSh);
                dfpn_inner<false>(pos, { thPn, kInfinite - 1, thSh }, maxDepth, searchedNode);
                {
                    const auto probe = transposition_table.Probe<false>(pos);
                    child->state = probe.state;
                }
                if (child->state.data.pn == 0 && child->state.inc) {
                    pos.undoMove(child->headMove());
                    break;
                }
            }
            pos.undoMove(child->headMove());
        }
        // Local update (shtsume bns_plus_or: re-sort + conditional TT update)
        children = mvlistSort(children, [&](const MvList* a, const MvList* b) {
            return orChildCmp(a, b, pos);
        });
        const int newSh = children->state.data.sh + 1;
        if (base.sh > newSh || children->state.inc) {
            NodeState result;
            result.pn = children->state.data.pn;
            result.dn = disproofNumber(children, nullptr);
            result.sh = newSh;
            result.current = false;
            result.protect = false;
            if (result.pn == 0) {
                const TTState r = makeMateByOrMove(pos, children->headMove(), children->state);
                result.hand = r.hand; result.inc = r.inc; result.hinc = r.hinc;
                result.nouse = r.nouse; result.nouse2 = r.nouse2;
            } else if (result.dn == 0) {
                const TTState r = makeNoMateByOrNodeLL(pos, children);
                result.hand = r.hand; result.inc = r.inc; result.hinc = r.hinc;
                result.nouse = r.nouse; result.nouse2 = r.nouse2;
            } else {
                result.hand = children->state.hand; result.inc = children->state.inc; result.hinc = children->state.hinc;
                result.nouse = children->state.nouse; result.nouse2 = children->state.nouse2;
            }
            mvListPool.freeMvList(children);
            return finalizeNode<true>(pos, pos.hand(pos.turn()), result);
        }
        mvListPool.freeMvList(children);
        const auto orProbe = transposition_table.Probe<true>(pos);
        return NodeState{ orProbe.state.data.pn, orProbe.state.data.dn, orProbe.state.data.sh, orProbe.state.hand,
            orProbe.state.inc, orProbe.state.hinc, orProbe.state.nouse, orProbe.state.nouse2, orProbe.state.protect, orProbe.state.current };
    }
    else {
        MovePicker<false> movePicker(pos, &transposition_table);
        MvList* groups = buildAndGroupsProbedLL(pos, movePicker, transposition_table, mvListPool);
        groups = mvlistSort(groups, [&](const MvList* a, const MvList* b) {
            return andGroupCmp(a, b, pos);
        });

        // shtsume bns_plus_and: process front, mark processed, re-sort, repeat
        // Reset searched (used as shtsume pr flag) — probeAndGroupLL sets it from cutoff
        for (MvList* p = groups; p; p = p->next) { p->searched = false; }
        int tsh = ptsh - 1;
        while (groups) {
            if (stop || groups->state.inc || groups->searched) {
                break;
            }
            if (groups->state.data.pn != 0 || groups->state.current) {
                break;
            }
            tsh = std::min(tsh, static_cast<int>(groups->state.data.sh));
            {
                StateInfo stateInfo;
                pos.doMove(groups->headMove(), stateInfo);
                const NodeState childBase{ groups->state.data.pn, groups->state.data.dn, groups->state.data.sh, groups->state.hand,
                    groups->state.inc, groups->state.hinc, groups->state.nouse, groups->state.nouse2, groups->state.protect, groups->state.current };
                bns_plus_inner<true>(pos, childBase, maxDepth, searchedNode, addThPn, tsh);
                {
                    const auto probe = transposition_table.Probe<true>(pos);
                    groups->state = probe.state;
                }
                pos.undoMove(groups->headMove());
            }
            if (groups->state.data.pn == 0 && groups->state.inc) {
                break;
            }
            groups->searched = true;  // equivalent to shtsume pr=1
            groups = mvlistSort(groups, [&](const MvList* a, const MvList* b) {
                return andGroupCmp(a, b, pos);
            });
        }
        // Local update (shtsume bns_plus_and: always update TT)
        groups = mvlistSort(groups, [&](const MvList* a, const MvList* b) {
            return andGroupCmp(a, b, pos);
        });
        {
            NodeState result;
            result.pn = proofNumber(groups, nullptr);
            result.dn = groups->state.data.dn;
            result.sh = groups->state.data.sh + 1;
            result.current = false;
            result.protect = false;
            if (result.pn == 0) {
                const TTState r = makeMateByAndNodeLL(pos, groups);
                result.hand = r.hand; result.inc = r.inc; result.hinc = r.hinc;
                result.nouse = r.nouse; result.nouse2 = r.nouse2;
            } else if (result.dn == 0) {
                const TTState r = makeNoMateByAndMove(pos, groups->headMove(), groups->state);
                result.hand = r.hand; result.inc = r.inc; result.hinc = r.hinc;
                result.nouse = r.nouse; result.nouse2 = r.nouse2;
            } else {
                result.hand = groups->state.hand; result.inc = groups->state.inc; result.hinc = groups->state.hinc;
                result.nouse = groups->state.nouse; result.nouse2 = groups->state.nouse2;
            }
            mvListPool.freeMvList(groups);
            return finalizeNode<false>(pos, pos.hand(oppositeColor(pos.turn())), result);
        }
    }
}

template <bool or_node>
DfPn::NodeState DfPn::dfpn_inner(Position& pos, Threshold threshold, const uint16_t maxDepth, uint32_t& searchedNode) {
    const Hand query_hand = or_node ? pos.hand(pos.turn()) : pos.hand(oppositeColor(pos.turn()));

    if constexpr (or_node) {
        if (pos.gamePly() + 1 > maxDepth) {
            return finalizeNode<or_node>(pos, query_hand, { kInfinite, 0, 0, disproofSeedHand(pos.hand(pos.turn()), pos.hand(oppositeColor(pos.turn()))) });
        }
    }

    const TranspositionTable::ProbeResult probe = transposition_table.Probe<or_node>(pos);
    NodeState node{ probe.state.data.pn, probe.state.data.dn, probe.state.data.sh, probe.state.hand,
        probe.state.inc, probe.state.hinc, probe.state.nouse, probe.state.nouse2, probe.state.protect, probe.state.current };
    if (node.pn == 0 || node.dn == 0) {
        return node;
    }
    // Same position on the current search path: do not re-expand.
    // Update cpn to the current threshold so that the parent AND node's
    // proofNumber reaches its threshold and exits. In shtsume, cpn grows
    // naturally because the cycle position is re-expanded (set_current
    // overwrites cpn each time). We avoid re-expansion (stack depth) by
    // updating cpn here and returning immediately.
    if (node.current) {
        transposition_table.SetCurrent<or_node>(pos, threshold.pn);
        node.pn = std::max(1, threshold.pn);
        return node;
    }

    MovePicker<or_node> movePicker(pos);
    if (movePicker.empty()) {
        if constexpr (or_node) {
            return finalizeNode<or_node>(pos, query_hand, { kInfinite, 0, 0, disproofSeedHand(pos.hand(pos.turn()), pos.hand(oppositeColor(pos.turn()))) });
        }
        const TTState terminal = makeTerminalMateState(pos, false, 0);
        return finalizeNode<or_node>(pos, query_hand, { terminal.data.pn, terminal.data.dn, terminal.data.sh, terminal.hand,
            terminal.inc, terminal.hinc, terminal.nouse, terminal.nouse2, terminal.protect, terminal.current });
    }

    const AndNodeFlags andFlags = [&]() {
        if constexpr (or_node) {
            return AndNodeFlags{};
        }
        else {
            return analyzeAndNode(pos, movePicker);
        }
    }();
    const Color them = oppositeColor(pos.turn());
    const Square enemyKing = pos.kingSquare(them);

    // Linked-list child/group list (pool-allocated)
    MvList* children = nullptr;  // OR node: single-move MvList per child
    MvList* groups = nullptr;    // AND node: grouped MvList

    // Helper to free the list and return a finalized node
    const auto freeAndFinalize = [&](NodeState ns) -> NodeState {
        if constexpr (or_node) {
            mvListPool.freeMvList(children);
        } else {
            mvListPool.freeMvList(groups);
        }
        return finalizeNode<or_node>(pos, query_hand, ns);
    };

    if constexpr (or_node) {
        // Distance-based pn initialization (shtsume equivalent):
        const bool skipDistBoost = prevCheckerIsHorse;
        // Build children list: iterate forward, append to tail to preserve order
        MvList* childrenTail = nullptr;
        for (const auto& extMove : movePicker) {
            const Move move = extMove.move;
            TranspositionTable::ProbeResult childProbe = transposition_table.ProbeChild<or_node>(pos, move);
            MvList* child = mvListPool.allocMvList();
            child->mlist = mvListPool.allocMList();
            child->mlist->move = move;
            child->mlist->next = nullptr;
            child->state = childProbe.state;
            child->searched = childProbe.cutoff;
            child->length = squareDistance(move.to(), enemyKing);
            if (!skipDistBoost && !child->searched && child->state.data.pn > 0 && child->state.data.dn > 0) {
                child->state.data.pn = std::max(child->state.data.pn, child->length);
            }
            child->next = nullptr;
            if (childrenTail) {
                childrenTail->next = child;
            } else {
                children = child;
            }
            childrenTail = child;
        }
    }
    else {
        // Detect checker piece type for distance-based pn init in child OR nodes.
        const Bitboard checkers = pos.checkersBB();
        if (checkers.isAny()) {
            const Square csq = checkers.constFirstOneFromSQ11();
            const PieceType pt = pieceToPieceType(pos.piece(csq));
            prevCheckerIsHorse = (pt == Horse || pt == Dragon);
        }
        else {
            prevCheckerIsHorse = false;
        }
        groups = buildAndGroupsLL(pos, movePicker, transposition_table, mvListPool);
        expandInitialMateGroupsLL(groups, pos, transposition_table, mvListPool);
    }

    const auto sortChildren = [&]() {
        if constexpr (or_node) {
            children = mvlistSort(children, [&](const MvList* a, const MvList* b) {
                return orChildCmp(a, b, pos);
            });
        }
        else {
            groups = mvlistSort(groups, [&](const MvList* a, const MvList* b) {
                return andGroupCmp(a, b, pos);
            });
            if (andFlags.ryuma_flag) {
                groups = applyAndRyumaReductionLL(groups, pos, mvListPool);
            }
        }
    };
    sortChildren();
    bool needsFullSort = false;

    while (searchedNode < maxSearchNode && !stop) {
        if (needsFullSort) {
            sortChildren();
            needsFullSort = false;
        }
        if constexpr (or_node) {
            // When a proven mate with surplus hand pieces (inc) is found and sh > 1,
            // search all unsearched siblings shallowly to find a simpler (0-move) mate.
            // This matches shtsume's bn_search_or optimization (depth < 30).
            if (children->state.data.pn == 0 && children->state.data.sh > 1
                && children->state.inc && (pos.gamePly() - rootPly) < 30) {
                const Threshold shallow = { kInfinite - 1, kInfinite - 1, 2 };
                for (MvList* child = children; child; child = child->next) {
                    if (!child->searched) {
                        StateInfo si;
                        pos.doMove(child->headMove(), si);
                        ++searchedNode;
                        const NodeState cs = dfpn_inner<false>(pos, shallow, maxDepth, searchedNode);
                        pos.undoMove(child->headMove());
                        child->state = { { cs.pn, cs.dn, cs.sh }, cs.hand,
                            cs.inc, cs.hinc, cs.nouse, cs.nouse2, cs.protect, cs.current };
                        child->searched = true;
                    }
                }
                sortChildren();
            }

            int dcnt = 0;
            node.pn = children->state.data.pn;
            node.dn = (pos.gamePly() == rootPly) ? children->state.data.dn : disproofNumber(children, &dcnt);
            node.sh = children->state.data.sh + 1;
            node.inc = children->state.inc;  // propagate inc from front child (shtsume: mvlist->inc = list->inc)

            if (node.pn == 0) {
                const TTState resolved = makeMateByOrMove(pos, children->headMove(), children->state);
                node.dn = kInfinite;
                node.hand = resolved.hand;
                node.inc = resolved.inc;
                node.hinc = resolved.hinc;
                node.nouse = resolved.nouse;
                node.nouse2 = resolved.nouse2;
                return freeAndFinalize(node);
            }
            if (node.dn == 0) {
                const TTState resolved = makeNoMateByOrNodeLL(pos, children);
                node.hand = resolved.hand;
                node.inc = resolved.inc;
                node.hinc = resolved.hinc;
                node.nouse = resolved.nouse;
                node.nouse2 = resolved.nouse2;
                return freeAndFinalize(node);
            }
            if (node.pn >= threshold.pn || node.dn >= threshold.dn || node.sh >= threshold.sh) {
                return freeAndFinalize(node);
            }

            Threshold childThreshold = threshold;
            if (children->next) {
                childThreshold.pn = std::min(threshold.pn, std::min(kInfinite, children->searched ? children->next->state.data.pn + 1 : 1));
                if (threshold.dn == kInfinite - 1)
                    childThreshold.dn = kInfinite - 1;
                else
                    childThreshold.dn = (pos.gamePly() == rootPly) ? threshold.dn : threshold.dn - dcnt;
            }
            childThreshold.sh = threshold.sh - 1;
            if (childThreshold.pn < kInfinite - 1) {
                maxThPn = std::max(maxThPn, childThreshold.pn);
            }

            transposition_table.SetCurrent<or_node>(pos, childThreshold.pn);
            StateInfo stateInfo;
            const Move move = children->headMove();
            pos.doMove(move, stateInfo);
            ++searchedNode;
            const NodeState childState = dfpn_inner<false>(pos, childThreshold, maxDepth, searchedNode);
            pos.undoMove(move);
            transposition_table.ClearCurrent<or_node>(pos);
            children->state = { { childState.pn, childState.dn, childState.sh }, childState.hand,
                childState.inc, childState.hinc, childState.nouse, childState.nouse2, childState.protect, childState.current };
            children->searched = true;
            children = mvlistReorderFront(children, [&](const MvList* a, const MvList* b) {
                return orChildCmp(a, b, pos);
            });
        }
        else {
            // AND side: full sort every iteration (matching shtsume bn_search_and)
            sortChildren();
            while (true) {
                int pcnt = 0;
                node.dn = groups->state.data.dn;
                for (const MvList* g = groups; g; g = g->next) {
                    if (g->state.data.pn > 0) {
                        node.dn = g->state.data.dn;
                        break;
                    }
                }
                node.pn = proofNumber(groups, &pcnt);
                node.sh = groups->state.data.sh + 1;

                if (groups->state.data.pn >= kPreProofMax - 1 && groups->state.data.dn == 1) {
                    if (groups->state.current) {
                        // Cycle-caused simple loop: soft exit with moderate values.
                        // Don't declare hard no-mate — the position may be provable
                        // via a non-cyclic path.  (In shtsume, GC eventually frees
                        // the stale entry; here we avoid creating it.)
                        return freeAndFinalize(node);
                    }
                    const TTState resolved = makeNoMateByAndMove(pos, groups->headMove(), groups->state);
                    node.pn = kInfinite;
                    node.dn = 0;
                    node.sh = groups->state.data.sh + 1;
                    node.hand = resolved.hand;
                    node.inc = resolved.inc;
                    node.hinc = resolved.hinc;
                    node.nouse = resolved.nouse;
                    node.nouse2 = resolved.nouse2;
                    node.protect = resolved.protect;
                    return freeAndFinalize(node);
                }

                if (collapseAndProofGhiLL(groups, pos, mvListPool, node.pn) || collapseAndDisproofGhiLL(groups, mvListPool)) {
                    continue;
                }

                if (node.dn == 0) {
                    const TTState resolved = makeNoMateByAndMove(pos, groups->headMove(), groups->state);
                    node.pn = kInfinite;
                    node.hand = resolved.hand;
                    node.inc = resolved.inc;
                    node.hinc = resolved.hinc;
                    node.nouse = resolved.nouse;
                    node.nouse2 = resolved.nouse2;
                    node.protect = resolved.protect;
                    return freeAndFinalize(node);
                }
                if (node.pn == 0) {
                    TTState resolved = makeMateByAndNodeLL(pos, groups);
                    if (andFlags.proof_flag) {
                        resolved = makeMateByAndProofFlag(pos, groups->state);
                    }
                    if (andFlags.invalid_flag && groups->state.inc) {
                        resolved = makeTerminalMateState(pos, andFlags.proof_flag, node.sh);
                        resolved.protect = true;
                    }
                    node.dn = kInfinite;
                    node.hand = resolved.hand;
                    node.inc = resolved.inc;
                    node.hinc = resolved.hinc;
                    node.nouse = resolved.nouse;
                    node.nouse2 = resolved.nouse2;
                    node.protect = resolved.protect;
                    return freeAndFinalize(node);
                }
                if (node.pn >= threshold.pn || node.dn >= threshold.dn || node.sh >= threshold.sh) {
                    return freeAndFinalize(node);
                }

                Threshold childThreshold = threshold;
                if (groups->next) {
                    childThreshold.dn = std::min(threshold.dn, groups->next->state.data.dn + 1);
                    childThreshold.pn = threshold.pn - pcnt;
                }
                childThreshold.sh = threshold.sh - 1;

                transposition_table.SetCurrent<or_node>(pos, childThreshold.pn);
                StateInfo stateInfo;
                const Move move = groups->headMove();
                pos.doMove(move, stateInfo);
                ++searchedNode;
                const NodeState childState = dfpn_inner<true>(pos, childThreshold, maxDepth, searchedNode);
                pos.undoMove(move);
                transposition_table.ClearCurrent<or_node>(pos);
                groups->state = { { childState.pn, childState.dn, childState.sh }, childState.hand,
                    childState.inc, childState.hinc, childState.nouse, childState.nouse2, childState.protect, childState.current };
                groups->searched = true;
                expandFrontMateGroupsLL(groups, pos, transposition_table, mvListPool);
                break;
            }
        }
    }

    return freeAndFinalize(node);
}

Move DfPn::dfpn_move(Position& pos) {
    MovePicker<true> movePicker(pos);
    MvList* children = buildOrChildrenLL(pos, movePicker, transposition_table, mvListPool);
    children = mvlistSort(children, [&](const MvList* a, const MvList* b) {
        return orChildCmp(a, b, pos);
    });
    Move result = Move::moveNone();
    for (const MvList* child = children; child; child = child->next) {
        if (child->state.data.pn == 0) {
            result = child->headMove();
            break;
        }
    }
    mvListPool.freeMvList(children);
    return result;
}

template <bool or_node>
int DfPn::get_pv_inner(Position& pos, std::vector<u32>& pv) {
    if constexpr (or_node) {
        MovePicker<true> movePicker(pos);
        MvList* children = buildOrChildrenLL(pos, movePicker, transposition_table, mvListPool);
        children = mvlistSort(children, [&](const MvList* a, const MvList* b) {
            return orChildCmp(a, b, pos);
        });
        int result = 0;
        for (const MvList* child = children; child; child = child->next) {
            if (child->state.data.pn != 0) {
                continue;
            }
            pv.push_back(child->headMove().value());
            if (child->state.data.sh <= 1) {
                result = 1;
                break;
            }
            StateInfo stateInfo;
            pos.doMove(child->headMove(), stateInfo);
            const int depth = get_pv_inner<false>(pos, pv);
            pos.undoMove(child->headMove());
            result = depth + 1;
            break;
        }
        mvListPool.freeMvList(children);
        return result;
    }
    else {
        // PV extraction: probe each move individually (no grouping), matching shtsume's tsearchpv_update_and
        MovePicker<false> movePicker(pos, &transposition_table);
        const Square enemyKing = pos.kingSquare(oppositeColor(pos.turn()));
        MvList* children = nullptr;
        for (const auto& extMove : movePicker) {
            children = mvListPool.mvlistAdd(children, extMove.move);
            children->length = squareDistance(extMove.move.to(), enemyKing);
            probeAndGroupLL(children, pos, transposition_table);
        }
        children = mvlistSort(children, [&](const MvList* a, const MvList* b) {
            return andGroupCmp(a, b, pos);
        });

        // shtsume tsearchpv_update_and: re-search unresolved front children
        // until the front child is proven (pn==0).
        while (children && children->state.data.pn != 0) {
            uint32_t tmpNode = 0;
            StateInfo stateInfo;
            pos.doMove(children->headMove(), stateInfo);
            dfpn_inner<true>(pos, { kInfinite - 1, kInfinite - 1, draw_ply }, static_cast<uint16_t>(std::min<int>(rootPly + kMaxDepth, draw_ply)), tmpNode);
            pos.undoMove(children->headMove());
            probeAndGroupLL(children, pos, transposition_table);
            children = mvlistSort(children, [&](const MvList* a, const MvList* b) {
                return andGroupCmp(a, b, pos);
            });
        }

        int result = 0;
        if (children && children->state.data.pn == 0) {
            pv.push_back(children->headMove().value());
            StateInfo stateInfo;
            pos.doMove(children->headMove(), stateInfo);
            const int depth = get_pv_inner<true>(pos, pv);
            pos.undoMove(children->headMove());
            result = depth + 1;
        }
        mvListPool.freeMvList(children);
        return result;
    }
    return 0;
}

void DfPn::get_pv(Position& pos, std::vector<u32>& pv) {
    pv.clear();
    get_pv_inner<true>(pos, pv);
}

bool DfPn::dfpn(Position& pos) {
    transposition_table.NewSearch();
    stop = false;
    searchedNode = 0;
    maxThPn = 0;
    rootPly = pos.gamePly();
    const uint16_t maxDepth = static_cast<uint16_t>(std::min<int>(rootPly + kMaxDepth, draw_ply));
    const int shThreshold = std::min(kMaxDepth, draw_ply);
    const NodeState base = dfpn_inner<true>(pos, { kProofMax - 1, kRootDnThreshold, shThreshold }, maxDepth, searchedNode);
    printf("dfpn: pn=%d dn=%d sh=%d inc=%d nodes=%u rootPly=%d maxDepth=%d shTh=%d\n",
        base.pn, base.dn, base.sh, (int)base.inc, searchedNode, rootPly, maxDepth, shThreshold);
    NodeState result = base;
    if (base.pn == 0 && !stop) {
        // make_tree: g_n_make_tree iterations (shtsume default 2).
        for (int i = 0; i < kNMakeTree && !stop; i++) {
            transposition_table.ClearProtect();
            result = make_tree_inner<true>(pos, result, maxDepth, searchedNode);
        }
        // search_level refinement loop (shtsume g_search_level, default 0).
        // Each iteration does bns_plus + make_tree to refine the proof tree.
        int addThPn = maxThPn;
        for (int lv = searchLevel; lv > 0 && !stop && result.pn == 0 && !result.inc; lv--) {
            addThPn++;
            transposition_table.ClearProtect();
            result = bns_plus_inner<true>(pos, result, maxDepth, searchedNode, addThPn);
            for (int i = 0; i < kNMakeTree && !stop; i++) {
                transposition_table.ClearProtect();
                result = make_tree_inner<true>(pos, result, maxDepth, searchedNode);
            }
        }
        if (result.pn != 0) {
            result = base;
        }
    }
    return result.pn == 0;
}

bool DfPn::dfpn_andnode(Position& pos) {
    transposition_table.NewSearch();
    stop = false;
    searchedNode = 0;
    maxThPn = 0;
    rootPly = pos.gamePly();
    const uint16_t maxDepth = static_cast<uint16_t>(std::min<int>(rootPly + kMaxDepth, draw_ply));
    const int shThreshold = std::min(kMaxDepth, draw_ply);
    const NodeState base = dfpn_inner<false>(pos, { kDisproofMax - 1, kRootDnThreshold, shThreshold }, maxDepth, searchedNode);
    NodeState result = base;
    if (base.pn == 0 && !stop) {
        for (int i = 0; i < kNMakeTree && !stop; i++) {
            transposition_table.ClearProtect();
            result = make_tree_inner<false>(pos, result, maxDepth, searchedNode);
        }
        int addThPn = maxThPn;
        for (int lv = searchLevel; lv > 0 && !stop && result.pn == 0 && !result.inc; lv--) {
            addThPn++;
            transposition_table.ClearProtect();
            result = bns_plus_inner<false>(pos, result, maxDepth, searchedNode, addThPn);
            for (int i = 0; i < kNMakeTree && !stop; i++) {
                transposition_table.ClearProtect();
                result = make_tree_inner<false>(pos, result, maxDepth, searchedNode);
            }
        }
        if (result.pn != 0) {
            result = base;
        }
    }
    return result.pn == 0;
}

template int DfPn::get_pv_inner<true>(Position& pos, std::vector<u32>& pv);
template int DfPn::get_pv_inner<false>(Position& pos, std::vector<u32>& pv);
template DfPn::NodeState DfPn::finalizeNode<true>(Position& pos, Hand query_hand, const NodeState& state);
template DfPn::NodeState DfPn::finalizeNode<false>(Position& pos, Hand query_hand, const NodeState& state);
template DfPn::NodeState DfPn::make_tree_inner<true>(Position& pos, const NodeState& base, uint16_t maxDepth, uint32_t& searchedNode);
template DfPn::NodeState DfPn::make_tree_inner<false>(Position& pos, const NodeState& base, uint16_t maxDepth, uint32_t& searchedNode);
template DfPn::NodeState DfPn::bns_plus_inner<true>(Position& pos, const NodeState& base, uint16_t maxDepth, uint32_t& searchedNode, int addThPn, int ptsh);
template DfPn::NodeState DfPn::bns_plus_inner<false>(Position& pos, const NodeState& base, uint16_t maxDepth, uint32_t& searchedNode, int addThPn, int ptsh);
template DfPn::NodeState DfPn::dfpn_inner<true>(Position& pos, Threshold threshold, uint16_t maxDepth, uint32_t& searchedNode);
template DfPn::NodeState DfPn::dfpn_inner<false>(Position& pos, Threshold threshold, uint16_t maxDepth, uint32_t& searchedNode);
