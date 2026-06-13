/*
  Apery, a USI shogi playing engine derived from Stockfish, a UCI chess playing engine.
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2018 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad
  Copyright (C) 2011-2018 Hiraoka Takuya

  Apery is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Apery is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "generateMoves.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <tuple>
#include <utility>

namespace {
    constexpr std::array<int, 8> kOslDirFileDelta = { 1, 0, -1, 1, -1, 1, 0, -1 };
    constexpr std::array<int, 8> kOslDirRankDelta = { -1, -1, -1, 0, 0, 1, 1, 1 };

    int orient_for_osl_attacker(const Color attacker, const int delta) {
        return attacker == Black ? delta : -delta;
    }

    bool offset_square_osl(const Square sq, const int file_delta, const int rank_delta, Square& out) {
        const int file = static_cast<int>(makeFile(sq)) + file_delta;
        const int rank = static_cast<int>(makeRank(sq)) + rank_delta;
        if (!isInSquare(static_cast<File>(file), static_cast<Rank>(rank))) {
            return false;
        }
        out = makeSquare(static_cast<File>(file), static_cast<Rank>(rank));
        return true;
    }

    int osl_dir_index_from_delta(const Color attacker, const int file_delta, const int rank_delta) {
        const int oriented_file_delta = orient_for_osl_attacker(attacker, file_delta);
        const int oriented_rank_delta = orient_for_osl_attacker(attacker, rank_delta);
        for (size_t dir = 0; dir < kOslDirFileDelta.size(); ++dir) {
            if (kOslDirFileDelta[dir] == oriented_file_delta
                && kOslDirRankDelta[dir] == oriented_rank_delta) {
                return static_cast<int>(dir);
            }
        }
        return -1;
    }

    bool osl_king8_square(const Square king, const int dir_index, const Color attack_color, Square& out) {
        return offset_square_osl(
            king,
            -orient_for_osl_attacker(attack_color, kOslDirFileDelta[static_cast<size_t>(dir_index)]),
            -orient_for_osl_attacker(attack_color, kOslDirRankDelta[static_cast<size_t>(dir_index)]),
            out);
    }

    Bitboard osl_pinned_pieces_of(const Position& pos, const Color pinned_color) {
        Bitboard result = allZeroBB();
        const Color attacker = oppositeColor(pinned_color);
        const Square king_sq = pos.kingSquare(pinned_color);
        Bitboard pinners = pos.bbOf(attacker);

        pinners &= (pos.bbOf(Lance) & lanceAttackToEdge(attacker, king_sq))
            | (pos.bbOf(Rook, Dragon) & rookAttackToEdge(king_sq))
            | (pos.bbOf(Bishop, Horse) & bishopAttackToEdge(king_sq));

        while (pinners.isAny()) {
            const Square sq = pinners.firstOneFromSQ11();
            const Bitboard between = betweenBB(sq, king_sq) & pos.occupiedBB();
            if (between && between.isOneBit<false>() && between.andIsAny(pos.bbOf(pinned_color))) {
                result |= between;
            }
        }
        return result;
    }

    bool same_dir_as_osl_king8_index(const Square king, const Square sq, const int dir_index, const Color attack_color) {
        const Color defense_color = oppositeColor(attack_color);
        const int file_delta = static_cast<int>(makeFile(king)) - static_cast<int>(makeFile(sq));
        const int rank_delta = static_cast<int>(makeRank(king)) - static_cast<int>(makeRank(sq));
        const int step_file = file_delta == 0 ? 0 : (file_delta > 0 ? 1 : -1);
        const int step_rank = rank_delta == 0 ? 0 : (rank_delta > 0 ? 1 : -1);
        return step_file == orient_for_osl_attacker(defense_color, kOslDirFileDelta[static_cast<size_t>(dir_index)])
            && step_rank == orient_for_osl_attacker(defense_color, kOslDirRankDelta[static_cast<size_t>(dir_index)]);
    }

    bool has_enough_osl_defense_effect(const Position& pos, const Color attack_color, const Square target_king,
        const Square sq, const Bitboard& pinned_defense, const int dir_index) {
        const Color defense_color = oppositeColor(attack_color);
        Bitboard defenders = pos.attackersTo(defense_color, sq);
        defenders.clearBit(target_king);
        if (!defenders.isAny()) {
            return false;
        }

        if ((defenders & ~pinned_defense).isAny()) {
            return true;
        }

        while (defenders.isAny()) {
            const Square from = defenders.firstOneFromSQ11();
            if (pinned_defense.isSet(from) && same_dir_as_osl_king8_index(target_king, from, dir_index, attack_color)) {
                return true;
            }
        }
        return false;
    }

    bool is_osl_long_king_effect_attacker(const Position& pos, const Square king, const Square from) {
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
        return file_delta == 0 || rank_delta == 0 || std::abs(file_delta) == std::abs(rank_delta);
    }

    bool osl_king8_liberty(const Position& pos, const Color defense_color, const Square to) {
        const Color attack_color = oppositeColor(defense_color);
        const Square king = pos.kingSquare(defense_color);
        int dir_index = -1;
        for (size_t dir = 0; dir < kOslDirFileDelta.size(); ++dir) {
            Square candidate = SquareNum;
            if (osl_king8_square(king, static_cast<int>(dir), attack_color, candidate) && candidate == to) {
                dir_index = static_cast<int>(dir);
                break;
            }
        }
        if (dir_index < 0) {
            return false;
        }

        Square expected = SquareNum;
        if (!osl_king8_square(king, dir_index, attack_color, expected) || expected != to) {
            return false;
        }

        bool liberty = false;
        const Piece piece = pos.piece(to);
        const bool is_empty = piece == Empty;
        const bool is_attack_piece = piece != Empty && pieceToColor(piece) == attack_color;
        const bool is_defense_piece = piece != Empty && pieceToColor(piece) == defense_color;
        if (is_defense_piece) {
            return false;
        }

        if (!pos.attackersToIsAny(attack_color, to)) {
            liberty = is_empty || is_attack_piece;
        }
        else {
            const Bitboard pinned_defense = osl_pinned_pieces_of(pos, defense_color);
            const bool enough_defense = has_enough_osl_defense_effect(pos, attack_color, king, to, pinned_defense, dir_index);
            liberty = !enough_defense && is_empty;
        }

        if (!liberty) {
            return false;
        }

        Bitboard long_attackers = pos.attackersTo(attack_color, king);
        while (long_attackers.isAny()) {
            const Square from = long_attackers.firstOneFromSQ11();
            if (!is_osl_long_king_effect_attacker(pos, king, from)) {
                continue;
            }
            const int lf = static_cast<int>(makeFile(from)) - static_cast<int>(makeFile(king));
            const int lr = static_cast<int>(makeRank(from)) - static_cast<int>(makeRank(king));
            const int lfs = lf == 0 ? 0 : (lf > 0 ? 1 : -1);
            const int lrs = lr == 0 ? 0 : (lr > 0 ? 1 : -1);
            if (osl_dir_index_from_delta(attack_color, lfs, lrs) == dir_index) {
                return false;
            }
        }
        return true;
    }

    // 角, 飛車の場合
    template <MoveType MT, PieceType PT, Color US, bool ALL>
    FORCE_INLINE ExtMove* generateBishopOrRookMoves(ExtMove* moveList, const Position& pos,
                                                    const Bitboard& target, const Square /*ksq*/)
    {
        Bitboard fromBB = pos.bbOf(PT, US);
        while (fromBB) {
            const Square from = fromBB.firstOneFromSQ11();
            const bool fromCanPromote = canPromote(US, makeRank(from));
            Bitboard toBB = pos.attacksFrom<PT>(US, from) & target;
            FOREACH_BB(toBB, const Square to, {
                const bool toCanPromote = canPromote(US, makeRank(to));
                if (fromCanPromote | toCanPromote) {
                    (*moveList++).move = makePromoteMove<MT>(PT, from, to, pos);
                    if (MT == NonEvasion || ALL)
                        (*moveList++).move = makeNonPromoteMove<MT>(PT, from, to, pos);
                }
                else // 角、飛車は成れるなら成り、不成は生成しない。
                    (*moveList++).move = makeNonPromoteMove<MT>(PT, from, to, pos);
            });
        }
        return moveList;
    }

    // 駒打ちの場合
    // 歩以外の持ち駒は、loop の前に持ち駒の種類の数によって switch で展開している。
    // ループの展開はコードが膨れ上がる事によるキャッシュヒット率の低下と、演算回数のバランスを取って決める必要がある。
    // NPSに影響が出ないならシンプルにした方が良さそう。
    template <Color US>
    ExtMove* generateDropMoves(ExtMove* moveList, const Position& pos, const Bitboard& target) {
        const Hand hand = pos.hand(US);
        // まず、歩に対して指し手を生成
        if (hand.exists<HPawn>()) {
            Bitboard toBB = target;
            // 一段目には打てない
            const Rank TRank1 = (US == Black ? Rank1 : Rank9);
            toBB.andEqualNot(rankMask<TRank1>());

            // 二歩の回避
            Bitboard pawnsBB = pos.bbOf(Pawn, US);
            Square pawnsSquare;
            foreachBB(pawnsBB, pawnsSquare, [&](const int part) {
                    toBB.set(part, toBB.p(part) & ~squareFileMask(pawnsSquare).p(part));
                });

            // 打ち歩詰めの回避
            const Rank TRank9 = (US == Black ? Rank9 : Rank1);
            const SquareDelta TDeltaS = (US == Black ? DeltaS : DeltaN);

            const Square ksq = pos.kingSquare(oppositeColor(US));
            // 相手玉が九段目なら、歩で王手出来ないので、打ち歩詰めを調べる必要はない。
            if (makeRank(ksq) != TRank9) {
                const Square pawnDropCheckSquare = ksq + TDeltaS;
                assert(isInSquare(pawnDropCheckSquare));
                if (toBB.isSet(pawnDropCheckSquare) && pos.piece(pawnDropCheckSquare) == Empty) {
                    if (!pos.isPawnDropCheckMate(US, pawnDropCheckSquare))
                        // ここで clearBit だけして MakeMove しないことも出来る。
                        // 指し手が生成される順番が変わり、王手が先に生成されるが、後で問題にならないか?
                        (*moveList++).move = makeDropMove(Pawn, pawnDropCheckSquare);
                    toBB.xorBit(pawnDropCheckSquare);
                }
            }

            Square to;
            FOREACH_BB(toBB, to, {
                    (*moveList++).move = makeDropMove(Pawn, to);
                });
        }

        // 歩 以外の駒を持っているか
        if (hand.exceptPawnExists()) {
            PieceType haveHand[6]; // 歩以外の持ち駒。vector 使いたいけど、速度を求めるので使わない。
            int haveHandNum = 0; // 持ち駒の駒の種類の数

            // 桂馬、香車、それ以外の順番で格納する。(駒を打てる位置が限定的な順)
            if (hand.exists<HKnight>()) haveHand[haveHandNum++] = Knight;
            const int noKnightIdx      = haveHandNum; // 桂馬を除く駒でループするときのループの初期値
            if (hand.exists<HLance >()) haveHand[haveHandNum++] = Lance;
            const int noKnightLanceIdx = haveHandNum; // 桂馬, 香車を除く駒でループするときのループの初期値
            if (hand.exists<HSilver>()) haveHand[haveHandNum++] = Silver;
            if (hand.exists<HGold  >()) haveHand[haveHandNum++] = Gold;
            if (hand.exists<HBishop>()) haveHand[haveHandNum++] = Bishop;
            if (hand.exists<HRook  >()) haveHand[haveHandNum++] = Rook;

            const Rank TRank2 = (US == Black ? Rank2 : Rank8);
            const Rank TRank1 = (US == Black ? Rank1 : Rank9);
            const Bitboard TRank2BB = rankMask<TRank2>();
            const Bitboard TRank1BB = rankMask<TRank1>();

            Bitboard toBB;
            Square to;
            // 桂馬、香車 以外の持ち駒があれば、
            // 一段目に対して、桂馬、香車以外の指し手を生成。
            switch (haveHandNum - noKnightLanceIdx) {
            case 0: break; // 桂馬、香車 以外の持ち駒がない。
            case 1: toBB = target & TRank1BB; FOREACH_BB(toBB, to, { Unroller<1>()([&](const int i) { (*moveList++).move = makeDropMove(haveHand[noKnightLanceIdx + i], to); }); }); break;
            case 2: toBB = target & TRank1BB; FOREACH_BB(toBB, to, { Unroller<2>()([&](const int i) { (*moveList++).move = makeDropMove(haveHand[noKnightLanceIdx + i], to); }); }); break;
            case 3: toBB = target & TRank1BB; FOREACH_BB(toBB, to, { Unroller<3>()([&](const int i) { (*moveList++).move = makeDropMove(haveHand[noKnightLanceIdx + i], to); }); }); break;
            case 4: toBB = target & TRank1BB; FOREACH_BB(toBB, to, { Unroller<4>()([&](const int i) { (*moveList++).move = makeDropMove(haveHand[noKnightLanceIdx + i], to); }); }); break;
            default: UNREACHABLE;
            }

            // 桂馬以外の持ち駒があれば、
            // 二段目に対して、桂馬以外の指し手を生成。
            switch (haveHandNum - noKnightIdx) {
            case 0: break; // 桂馬 以外の持ち駒がない。
            case 1: toBB = target & TRank2BB; FOREACH_BB(toBB, to, { Unroller<1>()([&](const int i) { (*moveList++).move = makeDropMove(haveHand[noKnightIdx + i], to); }); }); break;
            case 2: toBB = target & TRank2BB; FOREACH_BB(toBB, to, { Unroller<2>()([&](const int i) { (*moveList++).move = makeDropMove(haveHand[noKnightIdx + i], to); }); }); break;
            case 3: toBB = target & TRank2BB; FOREACH_BB(toBB, to, { Unroller<3>()([&](const int i) { (*moveList++).move = makeDropMove(haveHand[noKnightIdx + i], to); }); }); break;
            case 4: toBB = target & TRank2BB; FOREACH_BB(toBB, to, { Unroller<4>()([&](const int i) { (*moveList++).move = makeDropMove(haveHand[noKnightIdx + i], to); }); }); break;
            case 5: toBB = target & TRank2BB; FOREACH_BB(toBB, to, { Unroller<5>()([&](const int i) { (*moveList++).move = makeDropMove(haveHand[noKnightIdx + i], to); }); }); break;
            default: UNREACHABLE;
            }

            // 一、二段目以外に対して、全ての持ち駒の指し手を生成。
            toBB = target & ~(TRank2BB | TRank1BB);
            switch (haveHandNum) {
            case 0: assert(false); break; // 最適化の為のダミー
            case 1: FOREACH_BB(toBB, to, { Unroller<1>()([&](const int i) { (*moveList++).move = makeDropMove(haveHand[i], to); }); }); break;
            case 2: FOREACH_BB(toBB, to, { Unroller<2>()([&](const int i) { (*moveList++).move = makeDropMove(haveHand[i], to); }); }); break;
            case 3: FOREACH_BB(toBB, to, { Unroller<3>()([&](const int i) { (*moveList++).move = makeDropMove(haveHand[i], to); }); }); break;
            case 4: FOREACH_BB(toBB, to, { Unroller<4>()([&](const int i) { (*moveList++).move = makeDropMove(haveHand[i], to); }); }); break;
            case 5: FOREACH_BB(toBB, to, { Unroller<5>()([&](const int i) { (*moveList++).move = makeDropMove(haveHand[i], to); }); }); break;
            case 6: FOREACH_BB(toBB, to, { Unroller<6>()([&](const int i) { (*moveList++).move = makeDropMove(haveHand[i], to); }); }); break;
            default: UNREACHABLE;
            }
        }

        return moveList;
    }

    // 金, 成り金、馬、竜の指し手生成
    template <MoveType MT, PieceType PT, Color US, bool ALL> struct GeneratePieceMoves {
        FORCE_INLINE ExtMove* operator () (ExtMove* moveList, const Position& pos, const Bitboard& target, const Square /*ksq*/) {
            static_assert(PT == GoldHorseDragon, "");
            // 金、成金、馬、竜のbitboardをまとめて扱う。
            Bitboard fromBB = (pos.goldsBB() | pos.bbOf(Horse, Dragon)) & pos.bbOf(US);
            while (fromBB) {
                const Square from = fromBB.firstOneFromSQ11();
                // from にある駒の種類を判別
                const PieceType pt = pieceToPieceType(pos.piece(from));
                Bitboard toBB = pos.attacksFrom(pt, US, from) & target;
                FOREACH_BB(toBB, const Square to, {
                    (*moveList++).move = makeNonPromoteMove<MT>(pt, from, to, pos);
                });
            }
            return moveList;
        }
    };
    // 歩の場合
    template <MoveType MT, Color US, bool ALL> struct GeneratePieceMoves<MT, Pawn, US, ALL> {
        FORCE_INLINE ExtMove* operator () (ExtMove* moveList, const Position& pos, const Bitboard& target, const Square /*ksq*/) {
            // Txxx は先手、後手の情報を吸収した変数。数字は先手に合わせている。
            const Rank TRank4 = (US == Black ? Rank4 : Rank6);
            const Bitboard TRank123BB = inFrontMask<US, TRank4>();
            const SquareDelta TDeltaS = (US == Black ? DeltaS : DeltaN);

            Bitboard toBB = pawnAttack<US>(pos.bbOf(Pawn, US)) & target;

            // 成り
            if (MT != NonCaptureMinusPro) {
                Bitboard toOn123BB = toBB & TRank123BB;
                if (toOn123BB) {
                    toBB.andEqualNot(TRank123BB);
                    Square to;
                    FOREACH_BB(toOn123BB, to, {
                            const Square from = to + TDeltaS;
                            (*moveList++).move = makePromoteMove<MT>(Pawn, from, to, pos);
                            if (MT == NonEvasion || ALL) {
                                const Rank TRank1 = (US == Black ? Rank1 : Rank9);
                                if (makeRank(to) != TRank1)
                                    (*moveList++).move = makeNonPromoteMove<MT>(Pawn, from, to, pos);
                            }
                        });
                }
            }
            else
                assert(!(target & TRank123BB));

            // 残り(不成)
            // toBB は 8~4 段目まで。
            Square to;
            FOREACH_BB(toBB, to, {
                    const Square from = to + TDeltaS;
                    (*moveList++).move = makeNonPromoteMove<MT>(Pawn, from, to, pos);
                });
            return moveList;
        }
    };
    // 香車の場合
    template <MoveType MT, Color US, bool ALL> struct GeneratePieceMoves<MT, Lance, US, ALL> {
        FORCE_INLINE ExtMove* operator () (ExtMove* moveList, const Position& pos, const Bitboard& target, const Square /*ksq*/) {
            Bitboard fromBB = pos.bbOf(Lance, US);
            while (fromBB) {
                const Square from = fromBB.firstOneFromSQ11();
                Bitboard toBB = pos.attacksFrom<Lance>(US, from) & target;
                do {
                    if (toBB) {
                        // 駒取り対象は必ず一つ以下なので、toBB のビットを 0 にする必要がない。
                        const Square to = (MT == Capture || MT == CapturePlusPro ? toBB.constFirstOneFromSQ11() : toBB.firstOneFromSQ11());
                        const bool toCanPromote = canPromote(US, makeRank(to));
                        if (toCanPromote) {
                            (*moveList++).move = makePromoteMove<MT>(Lance, from, to, pos);
                            if (MT == NonEvasion || ALL) {
                                if (isBehind<US, Rank1, Rank9>(makeRank(to))) // 1段目の不成は省く
                                    (*moveList++).move = makeNonPromoteMove<MT>(Lance, from, to, pos);
                            }
                            else if (MT != NonCapture && MT != NonCaptureMinusPro) { // 駒を取らない3段目の不成を省く
                                if (isBehind<US, Rank2, Rank8>(makeRank(to))) // 2段目の不成を省く
                                    (*moveList++).move = makeNonPromoteMove<MT>(Lance, from, to, pos);
                            }
                        }
                        else
                            (*moveList++).move = makeNonPromoteMove<MT>(Lance, from, to, pos);
                    }
                    // 駒取り対象は必ず一つ以下なので、loop は不要。最適化で do while が無くなると良い。
                } while (!(MT == Capture || MT == CapturePlusPro) && toBB);
            }
            return moveList;
        }
    };
    // 桂馬の場合
    template <MoveType MT, Color US, bool ALL> struct GeneratePieceMoves<MT, Knight, US, ALL> {
        FORCE_INLINE ExtMove* operator () (ExtMove* moveList, const Position& pos, const Bitboard& target, const Square /*ksq*/) {
            Bitboard fromBB = pos.bbOf(Knight, US);
            while (fromBB) {
                const Square from = fromBB.firstOneFromSQ11();
                Bitboard toBB = pos.attacksFrom<Knight>(US, from) & target;
                FOREACH_BB(toBB, const Square to, {
                    const bool toCanPromote = canPromote(US, makeRank(to));
                    if (toCanPromote) {
                        (*moveList++).move = makePromoteMove<MT>(Knight, from, to, pos);
                        if (isBehind<US, Rank2, Rank8>(makeRank(to))) // 1, 2段目の不成は省く
                            (*moveList++).move = makeNonPromoteMove<MT>(Knight, from, to, pos);
                    }
                    else
                        (*moveList++).move = makeNonPromoteMove<MT>(Knight, from, to, pos);
                });
            }
            return moveList;
        }
    };
    // 銀の場合
    template <MoveType MT, Color US, bool ALL> struct GeneratePieceMoves<MT, Silver, US, ALL> {
        FORCE_INLINE ExtMove* operator () (ExtMove* moveList, const Position& pos, const Bitboard& target, const Square /*ksq*/) {
            Bitboard fromBB = pos.bbOf(Silver, US);
            while (fromBB) {
                const Square from = fromBB.firstOneFromSQ11();
                const bool fromCanPromote = canPromote(US, makeRank(from));
                Bitboard toBB = pos.attacksFrom<Silver>(US, from) & target;
                FOREACH_BB(toBB, const Square to, {
                    const bool toCanPromote = canPromote(US, makeRank(to));
                    if (fromCanPromote | toCanPromote)
                        (*moveList++).move = makePromoteMove<MT>(Silver, from, to, pos);
                    (*moveList++).move = makeNonPromoteMove<MT>(Silver, from, to, pos);
                });
            }
            return moveList;
        }
    };
    template <MoveType MT, Color US, bool ALL> struct GeneratePieceMoves<MT, Bishop, US, ALL> {
        FORCE_INLINE ExtMove* operator () (ExtMove* moveList, const Position& pos, const Bitboard& target, const Square ksq) {
            return generateBishopOrRookMoves<MT, Bishop, US, ALL>(moveList, pos, target, ksq);
        }
    };
    template <MoveType MT, Color US, bool ALL> struct GeneratePieceMoves<MT, Rook, US, ALL> {
        FORCE_INLINE ExtMove* operator () (ExtMove* moveList, const Position& pos, const Bitboard& target, const Square ksq) {
            return generateBishopOrRookMoves<MT, Rook, US, ALL>(moveList, pos, target, ksq);
        }
    };
    // 玉の場合
    // 必ず盤上に 1 枚だけあることを前提にすることで、while ループを 1 つ無くして高速化している。
    template <MoveType MT, Color US, bool ALL> struct GeneratePieceMoves<MT, King, US, ALL> {
        FORCE_INLINE ExtMove* operator () (ExtMove* moveList, const Position& pos, const Bitboard& target, const Square /*ksq*/) {
            const Square from = pos.kingSquare(US);
            Bitboard toBB = pos.attacksFrom<King>(US, from) & target;
            FOREACH_BB(toBB, const Square to, {
                (*moveList++).move = makeNonPromoteMove<MT>(King, from, to, pos);
            });
            return moveList;
        }
    };

    // pin は省かない。
    FORCE_INLINE ExtMove* generateRecaptureMoves(ExtMove* moveList, const Position& pos, const Square to, const Color us) {
        Bitboard fromBB = pos.attackersTo(us, to);
        while (fromBB) {
            const Square from = fromBB.firstOneFromSQ11();
            const PieceType pt = pieceToPieceType(pos.piece(from));
            switch (pt) {
            case Empty    : assert(false); break; // 最適化の為のダミー
            case Pawn     : case Lance    : case Knight   : case Silver   : case Bishop   : case Rook     :
                (*moveList++).move = ((canPromote(us, makeRank(to)) | canPromote(us, makeRank(from))) ?
                                      makePromoteMove<Capture>(pt, from, to, pos) :
                                      makeNonPromoteMove<Capture>(pt, from, to, pos));
                break;
            case Gold     : case King     : case ProPawn  : case ProLance : case ProKnight: case ProSilver: case Horse    : case Dragon   :
                (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                break;
            default       : UNREACHABLE;
            }
        }
        return moveList;
    }

    // 指し手生成 functor
    // テンプレート引数が複数あり、部分特殊化したかったので、関数ではなく、struct にした。
    // ALL == true のとき、歩、飛、角の不成、香の2段目の不成、香の3段目の駒を取らない不成も生成する。
    template <MoveType MT, Color US, bool ALL = false> struct GenerateMoves {
        ExtMove* operator () (ExtMove* moveList, const Position& pos) {
            static_assert(MT == Capture || MT == NonCapture || MT == CapturePlusPro || MT == NonCaptureMinusPro, "");
            // Txxx は先手、後手の情報を吸収した変数。数字は先手に合わせている。
            const Rank TRank4 = (US == Black ? Rank4 : Rank6);
            const Rank Trank3 = (US == Black ? Rank3 : Rank7);
            const Rank TRank2 = (US == Black ? Rank2 : Rank8);
            const Bitboard TRank123BB = inFrontMask<US, TRank4>();
            const Bitboard TRank4_9BB = inFrontMask<oppositeColor(US), Trank3>();

            const Bitboard targetPawn =
                (MT == Capture           ) ? pos.bbOf(oppositeColor(US))                                             :
                (MT == NonCapture        ) ? pos.emptyBB()                                                           :
                (MT == CapturePlusPro    ) ? pos.bbOf(oppositeColor(US)) | (pos.occupiedBB().notThisAnd(TRank123BB)) :
                (MT == NonCaptureMinusPro) ? pos.occupiedBB().notThisAnd(TRank4_9BB)                                 :
                allOneBB(); // error
            const Bitboard targetOther =
                (MT == Capture           ) ? pos.bbOf(oppositeColor(US)) :
                (MT == NonCapture        ) ? pos.emptyBB()               :
                (MT == CapturePlusPro    ) ? pos.bbOf(oppositeColor(US)) :
                (MT == NonCaptureMinusPro) ? pos.emptyBB()               :
                allOneBB(); // error
            const Square ksq = pos.kingSquare(oppositeColor(US));

            moveList = GeneratePieceMoves<MT, Pawn           , US, ALL>()(moveList, pos, targetPawn, ksq);
            moveList = GeneratePieceMoves<MT, Lance          , US, ALL>()(moveList, pos, targetOther, ksq);
            moveList = GeneratePieceMoves<MT, Knight         , US, ALL>()(moveList, pos, targetOther, ksq);
            moveList = GeneratePieceMoves<MT, Silver         , US, ALL>()(moveList, pos, targetOther, ksq);
            moveList = GeneratePieceMoves<MT, Bishop         , US, ALL>()(moveList, pos, targetOther, ksq);
            moveList = GeneratePieceMoves<MT, Rook           , US, ALL>()(moveList, pos, targetOther, ksq);
            moveList = GeneratePieceMoves<MT, GoldHorseDragon, US, ALL>()(moveList, pos, targetOther, ksq);
            moveList = GeneratePieceMoves<MT, King           , US, ALL>()(moveList, pos, targetOther, ksq);

            return moveList;
        }
    };

    // 部分特殊化
    // 駒打ち生成
    template <Color US> struct GenerateMoves<Drop, US> {
        FORCE_INLINE ExtMove* operator () (ExtMove* moveList, const Position& pos) {
            const Bitboard target = pos.emptyBB();
            moveList = generateDropMoves<US>(moveList, pos, target);
            return moveList;
        }
    };

    // checkSq にある駒で王手されたとき、玉はその駒の利きの位置には移動できないので、移動できない位置を bannnedKingToBB に格納する。
    // 両王手のときには二度連続で呼ばれるため、= ではなく |= を使用している。
    // 最初に呼ばれたときは、bannedKingToBB == allZeroBB() である。
    // todo: FOECE_INLINE と template 省いてNPS比較
    template <Color THEM>
    FORCE_INLINE void makeBannedKingTo(Bitboard& bannedKingToBB, const Position& pos,
                                       const Square checkSq, const Square ksq)
    {
        switch (pos.piece(checkSq)) {
//      case Empty: assert(false); break; // 最適化の為のダミー
        case (THEM == Black ? BPawn      : WPawn):
        case (THEM == Black ? BKnight    : WKnight):
            // 歩、桂馬で王手したときは、どこへ逃げても、その駒で取られることはない。
            // よって、ここでは何もしない。
            assert(
                pos.piece(checkSq) == (THEM == Black ? BPawn   : WPawn) ||
                pos.piece(checkSq) == (THEM == Black ? BKnight : WKnight)
                );
        break;
        case (THEM == Black ? BLance     : WLance):
            bannedKingToBB |= lanceAttackToEdge(THEM, checkSq);
            break;
        case (THEM == Black ? BSilver    : WSilver):
            bannedKingToBB |= silverAttack(THEM, checkSq);
            break;
        case (THEM == Black ? BGold      : WGold):
        case (THEM == Black ? BProPawn   : WProPawn):
        case (THEM == Black ? BProLance  : WProLance):
        case (THEM == Black ? BProKnight : WProKnight):
        case (THEM == Black ? BProSilver : WProSilver):
            bannedKingToBB |= goldAttack(THEM, checkSq);
        break;
        case (THEM == Black ? BBishop    : WBishop):
            bannedKingToBB |= bishopAttackToEdge(checkSq);
            break;
        case (THEM == Black ? BHorse     : WHorse):
            bannedKingToBB |= horseAttackToEdge(checkSq);
            break;
        case (THEM == Black ? BRook      : WRook):
            bannedKingToBB |= rookAttackToEdge(checkSq);
            break;
        case (THEM == Black ? BDragon    : WDragon):
            if (squareRelation(checkSq, ksq) & DirecDiag) {
                // 斜めから王手したときは、玉の移動先と王手した駒の間に駒があることがあるので、
                // dragonAttackToEdge(checkSq) は使えない。
                bannedKingToBB |= pos.attacksFrom<Dragon>(checkSq);
            }
            else {
                bannedKingToBB |= dragonAttackToEdge(checkSq);
            }
            break;
        default:
            UNREACHABLE;
        }
    }

    // 部分特殊化
    // 王手回避生成
    // 王手をしている駒による王手は避けるが、
    // 玉の移動先に敵の利きがある場合と、pinされている味方の駒を動かした場合、非合法手を生成する。
    // そのため、pseudo legal である。
    template <Color US, bool ALL> struct GenerateMoves<Evasion, US, ALL> {
        /*FORCE_INLINE*/ ExtMove* operator () (ExtMove* moveList, const Position& pos) {
            assert(pos.isOK());
            assert(pos.inCheck());

            const Square ksq = pos.kingSquare(US);
            constexpr Color Them = oppositeColor(US);
            const Bitboard checkers = pos.checkersBB();
            Bitboard bb = checkers;
            Bitboard bannedKingToBB = allZeroBB();
            int checkersNum = 0;
            Square checkSq;

            // 玉が逃げられない位置の bitboard を生成する。
            // 絶対に王手が掛かっているので、while ではなく、do while
            do {
                checkSq = bb.firstOneFromSQ11();
                assert(pieceToColor(pos.piece(checkSq)) == Them);
                ++checkersNum;
                makeBannedKingTo<Them>(bannedKingToBB, pos, checkSq, ksq);
            } while (bb);

            // 玉が移動出来る移動先を格納。
            bb = bannedKingToBB.notThisAnd(pos.bbOf(US).notThisAnd(kingAttack(ksq)));
            FOREACH_BB(bb, const Square to, {
                // 移動先に相手駒の利きがあるか調べずに指し手を生成する。
                // attackersTo() が重いので、movePicker か search で合法手か調べる。
                (*moveList++).move = makeNonPromoteMove<Capture>(King, ksq, to, pos);
            });

            // 両王手なら、玉を移動するしか回避方法は無い。
            // 玉の移動は生成したので、ここで終了
            if (1 < checkersNum)
                return moveList;

            // 王手している駒を玉以外で取る手の生成。
            // pin されているかどうかは movePicker か search で調べる。
            const Bitboard target1 = betweenBB(checkSq, ksq);
            const Bitboard target2 = target1 | checkers;
            moveList = GeneratePieceMoves<Evasion, Pawn,   US, ALL>()(moveList, pos, target2, ksq);
            moveList = GeneratePieceMoves<Evasion, Lance,  US, ALL>()(moveList, pos, target2, ksq);
            moveList = GeneratePieceMoves<Evasion, Knight, US, ALL>()(moveList, pos, target2, ksq);
            moveList = GeneratePieceMoves<Evasion, Silver, US, ALL>()(moveList, pos, target2, ksq);
            moveList = GeneratePieceMoves<Evasion, Bishop, US, ALL>()(moveList, pos, target2, ksq);
            moveList = GeneratePieceMoves<Evasion, Rook,   US, ALL>()(moveList, pos, target2, ksq);
            moveList = GeneratePieceMoves<Evasion, GoldHorseDragon,   US, ALL>()(moveList, pos, target2, ksq);

            if (target1)
                moveList = generateDropMoves<US>(moveList, pos, target1);

            return moveList;
        }
    };

    // 部分特殊化
    // 王手が掛かっていないときの指し手生成
    // これには、玉が相手駒の利きのある地点に移動する自殺手と、pin されている駒を動かす自殺手を含む。
    // ここで生成した手は pseudo legal
    template <Color US> struct GenerateMoves<NonEvasion, US> {
        /*FORCE_INLINE*/ ExtMove* operator () (ExtMove* moveList, const Position& pos) {
            Bitboard target = pos.emptyBB();

            moveList = generateDropMoves<US>(moveList, pos, target);
            target |= pos.bbOf(oppositeColor(US));
            const Square ksq = pos.kingSquare(oppositeColor(US));

            moveList = GeneratePieceMoves<NonEvasion, Pawn           , US, false>()(moveList, pos, target, ksq);
            moveList = GeneratePieceMoves<NonEvasion, Lance          , US, false>()(moveList, pos, target, ksq);
            moveList = GeneratePieceMoves<NonEvasion, Knight         , US, false>()(moveList, pos, target, ksq);
            moveList = GeneratePieceMoves<NonEvasion, Silver         , US, false>()(moveList, pos, target, ksq);
            moveList = GeneratePieceMoves<NonEvasion, Bishop         , US, false>()(moveList, pos, target, ksq);
            moveList = GeneratePieceMoves<NonEvasion, Rook           , US, false>()(moveList, pos, target, ksq);
            moveList = GeneratePieceMoves<NonEvasion, GoldHorseDragon, US, false>()(moveList, pos, target, ksq);
            moveList = GeneratePieceMoves<NonEvasion, King           , US, false>()(moveList, pos, target, ksq);

            return moveList;
        }
    };

    // 部分特殊化
    // 連続王手の千日手以外の反則手を排除した合法手生成
    // そんなに速度が要求されるところでは呼ばない。
    template <Color US> struct GenerateMoves<Legal, US> {
        FORCE_INLINE ExtMove* operator () (ExtMove* moveList, const Position& pos) {
            ExtMove* curr = moveList;
            const Bitboard pinned = pos.pinnedBB();

            moveList = pos.inCheck() ?
                GenerateMoves<Evasion, US>()(moveList, pos) : GenerateMoves<NonEvasion, US>()(moveList, pos);

            // 玉の移動による自殺手と、pinされている駒の移動による自殺手を削除
            while (curr != moveList) {
                if (!pos.pseudoLegalMoveIsLegal<false, false>(curr->move, pinned))
                    curr->move = (--moveList)->move;
                else
                    ++curr;
            }

            return moveList;
        }
    };

    // 部分特殊化
    // Evasion のときに歩、飛、角と、香の2段目の不成も生成する。
    template <Color US> struct GenerateMoves<LegalAll, US> {
        FORCE_INLINE ExtMove* operator () (ExtMove* moveList, const Position& pos) {
            ExtMove* curr = moveList;
            const Bitboard pinned = pos.pinnedBB();

            moveList = pos.inCheck() ?
                GenerateMoves<Evasion, US, true>()(moveList, pos) : GenerateMoves<NonEvasion, US>()(moveList, pos);

            // 玉の移動による自殺手と、pinされている駒の移動による自殺手を削除
            while (curr != moveList) {
                if (!pos.pseudoLegalMoveIsLegal<false, false>(curr->move, pinned))
                    curr->move = (--moveList)->move;
                else
                    ++curr;
            }

            return moveList;
        }
    };

    // 部分特殊化
    // 玉の移動による自殺手と、pinされている駒の移動による自殺手を削除しない
    template <Color US> struct GenerateMoves<PseudoLegal, US> {
        FORCE_INLINE ExtMove* operator () (ExtMove* moveList, const Position& pos) {
            ExtMove* curr = moveList;

            moveList = pos.inCheck() ?
                GenerateMoves<Evasion, US, true>()(moveList, pos) : GenerateMoves<NonEvasion, US>()(moveList, pos);

            return moveList;
        }
    };

    // 王手用
    template <Color US, bool ALL>
    FORCE_INLINE ExtMove* generatCheckMoves(ExtMove* moveList, const PieceType pt, const Position& pos, const Square from, const Square to) {
        switch (pt) {
        case Empty: assert(false); break; // 最適化の為のダミー
        case Pawn:
            if (canPromote(US, makeRank(to))) {
                (*moveList++).move = makePromoteMove<Capture>(pt, from, to, pos);
                // 不成で移動する升
                if (ALL) {
                    if (isBehind<US, Rank1, Rank9>(makeRank(to))) // 1段目の不成は省く
                        (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                }
            }
            else
                (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
            break;
        case Lance:
            if (canPromote(US, makeRank(to))) {
                (*moveList++).move = makePromoteMove<Capture>(pt, from, to, pos);
                // 不成で移動する升
                if (ALL) {
                    if (isBehind<US, Rank1, Rank9>(makeRank(to))) // 1段目の不成は省く
                        (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                } else if (isBehind<US, Rank2, Rank8>(makeRank(to))) // 1, 2段目の不成を省く
                    (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
            }
            else
                (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
            break;
        case Knight:
            if (canPromote(US, makeRank(to))) {
                (*moveList++).move = makePromoteMove<Capture>(pt, from, to, pos);
                // 不成で移動する升
                if (isBehind<US, Rank2, Rank8>(makeRank(to))) // 1, 2段目の不成は省く
                    (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
            }
            else
                (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
            break;
        case Silver:
            if (canPromote(US, makeRank(to)) | canPromote(US, makeRank(from))) {
                (*moveList++).move = makePromoteMove<Capture>(pt, from, to, pos);
            }
            (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
            break;
        case Gold: case King: case ProPawn: case ProLance: case ProKnight: case ProSilver: case Horse: case Dragon:
            (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
            break;
        case Bishop: case Rook:
            if (canPromote(US, makeRank(to)) | canPromote(US, makeRank(from))) {
                (*moveList++).move = makePromoteMove<Capture>(pt, from, to, pos);
                // 不成で移動する升
                if (ALL) {
                    (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                }
            }
            else
                (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
            break;
        default: UNREACHABLE;
        }
        return moveList;
    }

    enum class OslCheckPhase : int {
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

    int orient_for_attacker(const Color attacker, const int delta) {
        // OSL DirectionPlayerTraits keeps DirectionTraits::blackOffset() for
        // BLACK and negates it for WHITE.
        return attacker == Black ? delta : -delta;
    }

    int osl_adjacent_dir_index(const Color attacker, const Square king, const Square to) {
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

        // OSL AddEffectWithEffect enumerates pos = target - offset.  The
        // direction is therefore king - move.to, not move.to - king.
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

    int osl_adjacent_phase_group(const int dir_index) {
        switch (dir_index) {
        case 1: return static_cast<int>(OslCheckPhase::U);
        case 0: return static_cast<int>(OslCheckPhase::UL);
        case 2: return static_cast<int>(OslCheckPhase::UR);
        case 3: return static_cast<int>(OslCheckPhase::L);
        case 4: return static_cast<int>(OslCheckPhase::R);
        case 6: return static_cast<int>(OslCheckPhase::D);
        case 5: return static_cast<int>(OslCheckPhase::DL);
        case 7: return static_cast<int>(OslCheckPhase::DR);
        default: return static_cast<int>(OslCheckPhase::Other);
        }
    }

    int osl_knight_side_index(const Color attacker, const Square king, const Square to) {
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

    int osl_line_dir_index(const Color attacker, const Square king, const Square to) {
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

    int osl_gold_drop_subphase(const int dir_index) {
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

    int osl_silver_drop_subphase(const int dir_index) {
        switch (dir_index) {
        case 5: return 0;
        case 7: return 1;
        case 1: return 2;
        case 0: return 3;
        case 2: return 4;
        default: return 5;
        }
    }

    int osl_bishop_drop_subphase(const int dir_index) {
        switch (dir_index) {
        case 5: return 0;
        case 7: return 1;
        case 0: return 2;
        case 2: return 3;
        default: return 4;
        }
    }

    int osl_rook_drop_subphase(const int dir_index) {
        switch (dir_index) {
        case 1: return 0;
        case 3: return 1;
        case 4: return 2;
        case 6: return 3;
        default: return 4;
        }
    }

    int osl_square_index_for_sort(const Square square) {
        const int x = static_cast<int>(makeFile(square)) + 1;
        const int y = static_cast<int>(makeRank(square)) + 1;
        return x * 16 + y + 1;
    }

    PieceType osl_basic_piece_type(const PieceType pt) {
        switch (pt) {
        case ProPawn: return Pawn;
        case ProLance: return Lance;
        case ProKnight: return Knight;
        case ProSilver: return Silver;
        case Horse: return Bishop;
        case Dragon: return Rook;
        default: return pt;
        }
    }

    int osl_piece_number_group_key(const PieceType pt) {
        switch (osl_basic_piece_type(pt)) {
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

    int osl_escape_block_piece_order(const PieceType pt) {
        switch (osl_basic_piece_type(pt)) {
        case Pawn: return 0;
        case Lance: return 1;
        case Knight: return 2;
        case Silver: return 3;
        case Gold: return 4;
        case Bishop: return 5;
        case Rook: return 6;
        default: return 128;
        }
    }

    int immediate_osl_piece_number_from_position(const Position& pos, const Square square) {
        if (!isInSquare(square)) {
            return 1024;
        }
        const Piece target_piece = pos.piece(square);
        if (target_piece == Empty) {
            return 1024;
        }

        std::array<int, SquareNum> board_number;
        board_number.fill(-1);
        std::array<bool, 40> used{};
        const auto assign_piece = [&](const Color owner, const Square sq, const PieceType pt) {
            const int begin = osl_piece_number_group_key(pt);
            const int end = pt == Pawn ? 18 : begin + (pt == King ? 2 : (pt == Bishop || pt == Rook ? 2 : 4));
            for (int num = begin; num < end; ++num) {
                if (used[static_cast<size_t>(num)]) {
                    continue;
                }
                if (pt == King && num != 30 + static_cast<int>(owner)) {
                    continue;
                }
                used[static_cast<size_t>(num)] = true;
                board_number[sq] = num;
                return;
            }
        };

        for (int rank = static_cast<int>(Rank1); rank <= static_cast<int>(Rank9); ++rank) {
            for (int file = static_cast<int>(File9); file >= static_cast<int>(File1); --file) {
                const Square sq = makeSquare(static_cast<File>(file), static_cast<Rank>(rank));
                const Piece piece = pos.piece(sq);
                if (piece != Empty) {
                    assign_piece(pieceToColor(piece), sq, osl_basic_piece_type(pieceToPieceType(piece)));
                }
            }
        }
        return board_number[square] >= 0 ? board_number[square] : 1024;
    }

    int osl_piece_iteration_key(const Move move) {
        if (move.isDrop()) {
            return 0;
        }
        // OSL AddEffectWithEffect enumerates state.effectSetAt(to) by fixed
        // piece-number groups, not by board square. This puts silver before
        // lance for the same adjacent target square, for example.
        return osl_piece_number_group_key(move.pieceTypeFrom()) * 256
            + osl_square_index_for_sort(move.from());
    }

    PieceType osl_check_piece_type_after_move(const Position& pos, const Move move) {
        if (move.isDrop()) {
            return move.pieceTypeDropped();
        }
        PieceType piece_type = pieceToPieceType(pos.piece(move.from()));
        if (move.isPromotion() && (piece_type & PTPromote) == 0) {
            piece_type = static_cast<PieceType>(piece_type + PTPromote);
        }
        return piece_type;
    }

    bool osl_move_is_direct_check(const Position& pos, const Color attacker, const Move move) {
        const Square to = move.to();
        const Square king = pos.kingSquare(oppositeColor(attacker));
        const PieceType piece_type = osl_check_piece_type_after_move(pos, move);

        switch (piece_type) {
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
            if (piece_type == Horse && kingAttack(to).isSet(king)) {
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
            if (piece_type == Dragon && kingAttack(to).isSet(king)) {
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

    int osl_knight_subphase(const Position& attacker_pos, const Color attacker, const Move move) {
        const int side = osl_knight_side_index(attacker, attacker_pos.kingSquare(oppositeColor(attacker)), move.to());
        if (side < 2) {
            return side * 2 + (move.isDrop() ? 1 : 0);
        }
        return 4 + (move.isDrop() ? 1 : 0);
    }

    int osl_discovered_phase(const Position& pos, const Color attacker, const Move move) {
        if (move.isDrop()) {
            return -1;
        }
        if (!pos.discoveredCheckBB().isSet(move.from())) {
            return -1;
        }
        const Square king = pos.kingSquare(oppositeColor(attacker));

        // Dfpn::generateCheck calls AddEffectWithEffect with isAttackToKing=true.
        // In that path generateKing excludes pin/open pieces from the adjacent
        // direction generators; discovered checks are emitted by the later
        // rook/bishop long generators.
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
                return static_cast<int>(OslCheckPhase::RookLong);
            }
        }

        Bitboard bishop_pinners = pos.bbOf(attacker) & pos.bbOf(Bishop, Horse)
            & bishopAttack(king, occupied_without_blocker);
        while (bishop_pinners) {
            const Square pinner = bishop_pinners.firstOneFromSQ11();
            bishop_pinners.clearBit(pinner);
            if ((betweenBB(pinner, king) & pos.occupiedBB()) == setMaskBB(move.from())) {
                return static_cast<int>(OslCheckPhase::BishopLong);
            }
        }
        return -1;
    }

    int osl_move_phase(const Position& pos, const Color attacker, const Move move, const bool direct_check) {
        const Square king = pos.kingSquare(oppositeColor(attacker));
        if (move.isDrop()) {
            switch (move.pieceTypeDropped()) {
            case Pawn:
            case Lance:
                return static_cast<int>(OslCheckPhase::U);
            case Knight:
                return static_cast<int>(OslCheckPhase::Knight);
            case Gold:
                return static_cast<int>(OslCheckPhase::DropGold);
            case Silver:
                return static_cast<int>(OslCheckPhase::DropSilver);
            case Bishop:
                return static_cast<int>(OslCheckPhase::DropBishop);
            case Rook:
                return static_cast<int>(OslCheckPhase::DropRook);
            default:
                return static_cast<int>(OslCheckPhase::Other);
            }
        }

        const PieceType piece_type_to = osl_check_piece_type_after_move(pos, move);
        const bool direct_unpromoted_long =
            direct_check && (piece_type_to == Bishop || piece_type_to == Rook);

        const int discovered_phase = osl_discovered_phase(pos, attacker, move);
        if (discovered_phase >= 0 && !direct_unpromoted_long) {
            return discovered_phase;
        }

        if (direct_check) {
            if (piece_type_to == Knight) {
                return static_cast<int>(OslCheckPhase::Knight);
            }
            const int adjacent_dir = osl_adjacent_dir_index(attacker, king, move.to());
            if (adjacent_dir >= 0) {
                return osl_adjacent_phase_group(adjacent_dir);
            }
            if (piece_type_to == Bishop) {
                return static_cast<int>(OslCheckPhase::BishopLong);
            }
            if (piece_type_to == Rook) {
                return static_cast<int>(OslCheckPhase::RookLong);
            }
            if (piece_type_to == Horse) {
                return static_cast<int>(OslCheckPhase::BishopLong);
            }
            if (piece_type_to == Dragon) {
                return static_cast<int>(OslCheckPhase::RookLong);
            }

            const int line_dir = osl_line_dir_index(attacker, king, move.to());
            if (line_dir >= 0) {
                return osl_adjacent_phase_group(line_dir);
            }
        }

        // OSL AddEffectWithEffect::generateKing emits non-adjacent long-piece
        // checks in the rook/bishop long generator loops.  Preserve that phase
        // for checks that are not caught by the direct/discovered classifiers,
        // otherwise the later Dfpn::sort oldPtype segments differ from OSL.
        const PieceType moving_type = pieceToPieceType(pos.piece(move.from()));
        if (moving_type == Bishop || moving_type == Horse) {
            return static_cast<int>(OslCheckPhase::BishopLong);
        }
        if (moving_type == Rook || moving_type == Dragon) {
            return static_cast<int>(OslCheckPhase::RookLong);
        }

        return static_cast<int>(OslCheckPhase::Other);
    }

    int osl_move_subphase(const Position& pos, const Color attacker, const Move move, const int phase) {
        if (move.isDrop()) {
            const Square king = pos.kingSquare(oppositeColor(attacker));
            switch (move.pieceTypeDropped()) {
            case Pawn:
                return 1;
            case Lance:
                return 2;
            case Knight:
                return osl_knight_subphase(pos, attacker, move);
            case Gold:
                return osl_gold_drop_subphase(osl_adjacent_dir_index(attacker, king, move.to()));
            case Silver:
                return osl_silver_drop_subphase(osl_adjacent_dir_index(attacker, king, move.to()));
            case Bishop:
                return osl_bishop_drop_subphase(osl_line_dir_index(attacker, king, move.to()));
            case Rook:
                return osl_rook_drop_subphase(osl_line_dir_index(attacker, king, move.to()));
            default:
                return 0;
            }
        }

        if (phase == static_cast<int>(OslCheckPhase::Knight)) {
            return osl_knight_subphase(pos, attacker, move);
        }

        return 0;
    }

    uint32_t pack_osl_check_move_order_key(const int phase, const int subphase, const int to_key,
        const int drop_key, const int from_key, const int promote_key) {
        return (static_cast<uint32_t>(phase) << 27)
            | (static_cast<uint32_t>(subphase) << 24)
            | (static_cast<uint32_t>(to_key) << 16)
            | (static_cast<uint32_t>(drop_key) << 15)
            | (static_cast<uint32_t>(from_key) << 1)
            | static_cast<uint32_t>(promote_key);
    }

    uint32_t osl_check_move_order_key(const Position& pos, const Color attacker, const Move move) {
        const bool direct_check = osl_move_is_direct_check(pos, attacker, move);
        const int phase = osl_move_phase(pos, attacker, move, direct_check);
        const int subphase = osl_move_subphase(pos, attacker, move, phase);
        const int from_key = osl_piece_iteration_key(move);
        const int to_key = osl_square_index_for_sort(move.to());
        const int promote_key = move.isPromotion() ? 0 : 1;
        return pack_osl_check_move_order_key(
            phase, subphase, to_key, move.isDrop() ? 1 : 0, from_key, promote_key);
    }

    struct OslmateCheckOrderEntry {
        ExtMove ext{};
        uint32_t key = 0;
    };

    auto osl_dfpn_move_sort_key(const Position& pos, const Color turn, const Move move) {
        const int attack_support = pos.attackersTo(turn, move.to()).popCount() + (move.isDrop() ? 1 : 0);
        const int defense_support = pos.attackersTo(oppositeColor(turn), move.to()).popCount();
        const int turn_sign = turn == Black ? 1 : -1;
        const int file = static_cast<int>(makeFile(move.to())) + 1;
        const int rank = static_cast<int>(makeRank(move.to())) + 1;
        const int to_y = turn_sign * rank;
        const int to_x = (5 - std::abs(5 - file)) * 2 + (file > 5 ? 1 : 0);
        int from_to = (to_y * 16 + to_x) * 256;
        if (move.isDrop()) {
            from_to += static_cast<int>(move.pieceTypeDropped());
        }
        else {
            from_to += osl_square_index_for_sort(move.from());
        }
        return std::make_tuple(attack_support > defense_support, from_to, move.isPromotion());
    }

    template <Color US>
    void sort_check_moves_like_osl_dfpn(const Position& pos, ExtMove* first, ExtMove* last) {
        size_t last_sorted = 0;
        size_t cur = 0;
        PieceType last_piece_type = Occupied;
        const size_t count = static_cast<size_t>(last - first);
        const auto sort_segment = [&](const size_t begin, const size_t end) {
            std::sort(first + static_cast<std::ptrdiff_t>(begin),
                first + static_cast<std::ptrdiff_t>(end),
                [&](const ExtMove& lhs, const ExtMove& rhs) {
                    return osl_dfpn_move_sort_key(pos, US, lhs.move)
                        > osl_dfpn_move_sort_key(pos, US, rhs.move);
                });
        };

        for (; cur < count; ++cur) {
            const Move move = first[cur].move;
            const PieceType piece_type = move.isDrop() ? Occupied : move.pieceTypeFrom();
            if (move.isDrop() || piece_type == last_piece_type) {
                continue;
            }
            sort_segment(last_sorted, cur);
            last_sorted = cur;
            last_piece_type = piece_type;
        }
        sort_segment(last_sorted, cur);
    }

    int osl_check_sort_ptype(const PieceType pt) {
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

    int osl_check_square_index(const Square square) {
        if (square == SquareNum) {
            return 0;
        }
        return (static_cast<int>(makeFile(square)) + 1) * 16
            + (static_cast<int>(makeRank(square)) + 1) + 1;
    }

    int osl_check_effect_count(const Position& pos, const Color color, const Square sq) {
        return pos.attackersTo(color, sq).popCount();
    }

    std::tuple<int, int, int> osl_check_move_sort_key(const Position& pos, const Color attacker, const Move move) {
        const int attack_support = osl_check_effect_count(pos, attacker, move.to()) + (move.isDrop() ? 1 : 0);
        const int defense_support = osl_check_effect_count(pos, oppositeColor(attacker), move.to());
        const int turn_sign = attacker == Black ? 1 : -1;
        const int file = static_cast<int>(makeFile(move.to())) + 1;
        const int to_y = turn_sign * (static_cast<int>(makeRank(move.to())) + 1);
        const int to_x = (5 - std::abs(5 - file)) * 2 + (file > 5 ? 1 : 0);
        int from_to = (to_y * 16 + to_x) * 256;
        if (move.isDrop()) {
            from_to += osl_check_sort_ptype(move.pieceTypeDropped());
        }
        else {
            from_to += osl_check_square_index(move.from());
        }
        return std::make_tuple(attack_support > defense_support, from_to, move.isPromotion());
    }

    struct OslmateCheckSortEntry {
        ExtMove ext{};
        std::tuple<int, int, int> key{};
    };

    template <Color US>
    void sort_check_moves_oslmate(const Position& pos, ExtMove* first, ExtMove* last) {
        std::array<OslmateCheckSortEntry, MaxLegalMoves> sorted;
        size_t last_sorted = 0;
        size_t cur = 0;
        PieceType last_piece_type = Occupied;
        const size_t size = static_cast<size_t>(last - first);
        const auto sort_segment = [&](const size_t begin, const size_t end) {
            for (size_t i = begin; i < end; ++i) {
                sorted[i - begin] = { first[i], osl_check_move_sort_key(pos, US, first[i].move) };
            }
            std::sort(sorted.data(), sorted.data() + static_cast<std::ptrdiff_t>(end - begin),
                [](const OslmateCheckSortEntry& lhs, const OslmateCheckSortEntry& rhs) {
                    return lhs.key > rhs.key;
                });
            for (size_t i = begin; i < end; ++i) {
                first[i] = sorted[i - begin].ext;
            }
        };
        for (; cur < size; ++cur) {
            const PieceType piece_type = first[cur].move.isDrop()
                ? Occupied
                : first[cur].move.pieceTypeFrom();
            if (first[cur].move.isDrop() || piece_type == last_piece_type) {
                continue;
            }
            sort_segment(last_sorted, cur);
            last_sorted = cur;
            last_piece_type = piece_type;
        }
        sort_segment(last_sorted, cur);
    }

    template <Color US, bool ALL>
    FORCE_INLINE void append_oslmate_generated_variants(ExtMove* raw, size_t& count, const Position& pos, const Square from, const Square to) {
        const PieceType pt = pieceToPieceType(pos.piece(from));
        std::array<ExtMove, 2> variants{};
        ExtMove* last = generatCheckMoves<US, ALL>(variants.data(), pt, pos, from, to);
        const Bitboard pinned = pos.pinnedBB();
        for (ExtMove* it = variants.data(); it != last; ++it) {
            if (!pos.moveGivesCheck(it->move)) {
                continue;
            }
            if (!pos.pseudoLegalMoveIsLegal<true, false>(it->move, pinned)) {
                continue;
            }
            raw[count++] = *it;
        }
    }

    template <Color US>
    bool osl_ignored_unpromote_candidate(const Position& pos, const Move move) {
        if (move.isDrop() || move.isPromotion()) {
            return false;
        }
        // OSL's Move::hasIgnoredUnpromote is based on the moving piece ptype.
        // Promoted pieces have canPromote(ptype)==false there, even though
        // cshogi Move::pieceTypeFrom() may report the unpromoted base type.
        if ((pieceToPieceType(pos.piece(move.from())) & PTPromote) != 0) {
            return false;
        }
        switch (move.pieceTypeFrom()) {
        case Pawn:
            return canPromote(US, makeRank(move.to()));
        case Bishop:
        case Rook:
            return canPromote(US, makeRank(move.to())) || canPromote(US, makeRank(move.from()));
        case Lance:
            return makeRank(move.to()) == (US == Black ? Rank2 : Rank8);
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

    Move promote_counterpart(const Move move) {
        return Move(move.value() | Move::PromoteFlag);
    }

    struct OslIgnoredUnpromoteSignature {
        Square from = SquareNum;
        Square to = SquareNum;
        PieceType piece_type = Occupied;
        PieceType captured = Occupied;

        bool operator<(const OslIgnoredUnpromoteSignature& other) const {
            return std::tie(from, to, piece_type, captured)
                < std::tie(other.from, other.to, other.piece_type, other.captured);
        }
    };

    OslIgnoredUnpromoteSignature osl_ignored_unpromote_signature(const Move move) {
        return { move.from(), move.to(), move.pieceTypeFrom(), move.cap() };
    }

    bool osl_invalid_promoted_piece_promotion(const Position& pos, const Move move) {
        if (!move.isPromotion() || move.isDrop()) {
            return false;
        }
        return (pieceToPieceType(pos.piece(move.from())) & PTPromote) != 0;
    }

    template <Color US>
    bool osl_unpromoted_ptype_has_effect_to_king(const Move move, const Square king) {
        const Square to = move.to();
        switch (move.pieceTypeFrom()) {
        case Pawn:
            return pawnAttack(US, to).isSet(king);
        case Lance:
            return lanceAttackToEdge(US, to).isSet(king);
        case Bishop:
            return bishopAttackToEdge(to).isSet(king);
        case Rook:
            return rookAttackToEdge(to).isSet(king);
        default:
            return false;
        }
    }

    template <Color US>
    bool osl_should_append_ignored_unpromote_check(const Position& pos, const Move promoted, const Move unpromoted) {
        if (!promoted.isPromotion() || !osl_ignored_unpromote_candidate<US>(pos, unpromoted)) {
            return false;
        }
        const Square king = pos.kingSquare(oppositeColor(US));
        return osl_unpromoted_ptype_has_effect_to_king<US>(unpromoted, king)
            || osl_pin_or_open_shadow(pos, US, promoted.from());
    }

    template <Color US, bool ALL>
    FORCE_INLINE ExtMove* generate_check_moves_oslmate(ExtMove* moveList, const Position& pos) {
        std::array<ExtMove, MaxLegalMoves> all;
        ExtMove* all_last = GenerateMoves<Check, US, ALL>()(all.data(), pos);
        const size_t all_count = static_cast<size_t>(all_last - all.data());

        // OSL's Dfpn::generateCheck does not sort here, but its source
        // generator AddEffectWithEffect::generateKing emits checks in fixed
        // phase order (U, Knight, UL, ..., rook long, bishop long, drops).
        // cshogi's generic Check generator has a different raw order, so
        // reorder to the OSL source generator order before applying
        // hasIgnoredUnpromote handling.
        std::array<OslmateCheckOrderEntry, MaxLegalMoves> ordered;
        size_t ordered_count = 0;
        for (size_t i = 0; i < all_count; ++i) {
            if (osl_invalid_promoted_piece_promotion(pos, all[i].move)) {
                continue;
            }
            ordered[ordered_count++] = { all[i], osl_check_move_order_key(pos, US, all[i].move) };
        }
        std::sort(ordered.data(), ordered.data() + static_cast<std::ptrdiff_t>(ordered_count),
            [](const OslmateCheckOrderEntry& lhs, const OslmateCheckOrderEntry& rhs) {
                return lhs.key < rhs.key;
            });
        size_t count = 0;
        for (size_t i = 0; i < ordered_count; ++i) {
            const Move move = ordered[i].ext.move;
            if (osl_ignored_unpromote_candidate<US>(pos, move)) {
                continue;
            }
            moveList[count++] = ordered[i].ext;
        }
        return moveList + count;
    }

    template <Color US, bool ALL>
    FORCE_INLINE ExtMove* generate_fixed_depth_check_moves_oslmate(ExtMove* moveList, const Position& pos) {
        std::array<ExtMove, MaxLegalMoves> all;
        ExtMove* all_last = GenerateMoves<Check, US, ALL>()(all.data(), pos);
        const size_t all_count = static_cast<size_t>(all_last - all.data());

        std::array<OslmateCheckOrderEntry, MaxLegalMoves> ordered;
        size_t ordered_count = 0;
        for (size_t i = 0; i < all_count; ++i) {
            if (osl_invalid_promoted_piece_promotion(pos, all[i].move)) {
                continue;
            }
            ordered[ordered_count++] = { all[i], osl_check_move_order_key(pos, US, all[i].move) };
        }
        std::sort(ordered.data(), ordered.data() + static_cast<std::ptrdiff_t>(ordered_count),
            [](const OslmateCheckOrderEntry& lhs, const OslmateCheckOrderEntry& rhs) {
                return lhs.key < rhs.key;
            });

        size_t count = 0;
        for (size_t i = 0; i < ordered_count; ++i) {
            const Move move = ordered[i].ext.move;
            if (osl_ignored_unpromote_candidate<US>(pos, move)) {
                continue;
            }
            moveList[count++] = ordered[i].ext;
        }
        return moveList + count;
    }

    // 部分特殊化
    // 王手をかける手を生成する。
    template <Color US, bool ALL> struct GenerateMoves<Check, US, ALL> {
        FORCE_INLINE ExtMove* operator () (ExtMove* moveList, const Position& pos) {
            ExtMove* curr = moveList;

            // やねうら王の実装を参考にした
            // https://github.com/yaneurao/YaneuraOu/blob/master/source/movegen.cpp

            // --- 駒の移動による王手

            // 王手になる指し手
            //  1) 成らない移動による直接王手
            //  2) 成る移動による直接王手
            //  3) pinされている駒の移動による間接王手
            // 集合としては1),2) <--> 3)は被覆している可能性があるのでこれを除外できるような指し手生成をしなくてはならない。
            // これを綺麗に実装するのは結構難しい。

            // x = 直接王手となる候補
            // y = 間接王手となる候補

            // ほとんどのケースにおいて y == emptyなのでそれを前提に最適化をする。
            // yと、yを含まないxとに分けて処理する。
            // すなわち、y と (x | y)^y

            constexpr Color opp = oppositeColor(US);
            const Square ksq = pos.kingSquare(opp);

            // 以下の方法だとxとして飛(龍)は100%含まれる。角・馬は60%ぐらいの確率で含まれる。事前条件でもう少し省ければ良いのだが…。
            const Bitboard x =
                (
                (pos.bbOf(Pawn)   & pawnCheckTable(US, ksq)) |
                    (pos.bbOf(Lance)  & lanceCheckTable(US, ksq)) |
                    (pos.bbOf(Knight) & knightCheckTable(US, ksq)) |
                    (pos.bbOf(Silver) & silverCheckTable(US, ksq)) |
                    (pos.goldsBB() & goldCheckTable(US, ksq)) |
                    (pos.bbOf(Bishop) & bishopCheckTable(US, ksq)) |
                    (pos.bbOf(Rook, Dragon)) | // ROOK,DRAGONは無条件全域
                    (pos.bbOf(Horse)  & horseCheckTable(US, ksq))
                    ) & pos.bbOf(US);

            // ここには王を敵玉の8近傍に移動させる指し手も含まれるが、王が近接する形はレアケースなので
            // 指し手生成の段階では除外しなくても良いと思う。

            const Bitboard y = pos.discoveredCheckBB();
            const Bitboard target = ~pos.bbOf(US); // 自駒がない場所が移動対象升

            // yのみ。ただしxかつyである可能性もある。
            auto src = y;
            while (src)
            {
                const Square from = src.firstOneFromSQ11();

                // 両王手候補なので指し手を生成してしまう。

                // いまの敵玉とfromを通る直線上の升と違うところに移動させれば開き王手が確定する。
                const PieceType pt = pieceToPieceType(pos.piece(from));
                Bitboard toBB = pos.attacksFrom(pt, US, from) & target;
                while (toBB) {
                    const Square to = toBB.firstOneFromSQ11();
                    if (!isAligned<true>(from, to, ksq)) {
                        moveList = generatCheckMoves<US, ALL>(moveList, pt, pos, from, to);
                    }
                    // 直接王手にもなるのでx & fromの場合、直線上の升への指し手を生成。
                    else if (x.isSet(from)) {
                        const PieceType pt = pieceToPieceType(pos.piece(from));
                        switch (pt) {
                        case Pawn: // 歩
                        {
                            if (pawnAttack(US, from).isSet(to)) {
                                // 成って王手
                                if (canPromote(US, makeRank(to))) {
                                    (*moveList++).move = makePromoteMove<Capture>(pt, from, to, pos);
                                    // 成らない手を後に生成
                                    if (ALL) {
                                        (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                                    }
                                }
                                else
                                    (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                            }
                            break;
                        }
                        case Silver: // 銀
                        {
                            if ((silverAttack(opp, ksq) & silverAttack(US, from)).isSet(to)) {
                                (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                            }
                            // 成って王手
                            if ((goldAttack(opp, ksq) & silverAttack(US, from)).isSet(to)) {
                                if (canPromote(US, makeRank(to)) | canPromote(US, makeRank(from))) {
                                    (*moveList++).move = makePromoteMove<Capture>(pt, from, to, pos);
                                }
                            }
                            break;
                        }
                        case Gold: // 金
                        case ProPawn: // と金
                        case ProLance: // 成香
                        case ProKnight: // 成桂
                        case ProSilver: // 成銀
                        {
                            if ((goldAttack(opp, ksq) & goldAttack(US, from)).isSet(to)) {
                                (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                            }
                            break;
                        }
                        case Horse: // 馬
                        {
                            // 玉が対角上にない場合
                            assert(abs(makeFile(ksq) - makeFile(from)) != abs(makeRank(ksq) - makeRank(from)));
                            if ((horseAttack(ksq, pos.occupiedBB()) & horseAttack(from, pos.occupiedBB())).isSet(to)) {
                                (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                            }
                            break;
                        }
                        case Dragon: // 竜
                        {
                            // 玉が直線上にない場合
                            assert(makeFile(ksq) != makeFile(from) && makeRank(ksq) != makeRank(from));
                            if ((dragonAttack(ksq, pos.occupiedBB()) & dragonAttack(from, pos.occupiedBB())).isSet(to)) {
                                (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                            }
                            break;
                        }
                        case Lance: // 香車
                        case Knight: // 桂馬
                        case Bishop: // 角
                        case Rook: // 飛車
                        {
                            assert(false);
                            break;
                        }
                        default: UNREACHABLE;
                        }
                    }
                }
            }

            // yに被覆しないx
            src = (x | y) ^ y;
            while (src)
            {
                const Square from = src.firstOneFromSQ11();

                // 直接王手のみ。
                const PieceType pt = pieceToPieceType(pos.piece(from));
                switch (pt) {
                case Pawn: // 歩
                {
                    // 成って王手
                    Bitboard toBB = pawnAttack(US, from) & target;
                    FOREACH_BB(toBB, const Square to, {
                        if (canPromote(US, makeRank(to))) {
                            (*moveList++).move = makePromoteMove<Capture>(pt, from, to, pos);
                            // 成らない手を後に生成
                            if (ALL) {
                                if (pawnAttack(opp, ksq).isSet(to))
                                    (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                            }
                        }
                        else
                            (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                    });
                    break;
                }
                case Lance: // 香車
                {
                    // 玉と筋が異なる場合
                    if (makeFile(ksq) != makeFile(from)) {
                        Bitboard toBB = goldAttack(opp, ksq) & lanceAttack(US, from, pos.occupiedBB()) & target;
                        FOREACH_BB(toBB, const Square to, {
                            // 成る
                            if (canPromote(US, makeRank(to))) {
                                (*moveList++).move = makePromoteMove<Capture>(pt, from, to, pos);
                            }
                        });
                    }
                    // 筋が同じ場合
                    else {
                        // 間にある駒が一つで、敵駒の場合
                        Bitboard dstBB = betweenBB(from, ksq) & pos.occupiedBB();
                        if (dstBB.isOneBit() && dstBB & pos.bbOf(opp)) {
                            const Square to = dstBB.firstOneFromSQ11();
                            // 成れる場合
                            if (pawnAttack(opp, ksq).isSet(to) && canPromote(US, makeRank(to))) {
                                (*moveList++).move = makePromoteMove<Capture>(pt, from, to, pos);
                                // 成らない手を後に生成
                                if (ALL) {
                                    (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                                }
                                else if (isBehind<US, Rank2, Rank8>(makeRank(to))) // 1, 2段目の不成を省く
                                    (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                            }
                            else {
                                (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                            }
                        }
                    }
                    break;
                }
                case Knight: // 桂馬
                {
                    Bitboard toBB = knightAttack(opp, ksq) & knightAttack(US, from) & target;
                    FOREACH_BB(toBB, const Square to, {
                        (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                    });
                    // 成って王手
                    toBB = goldAttack(opp, ksq) & knightAttack(US, from) & target;
                    FOREACH_BB(toBB, const Square to, {
                        if (canPromote(US, makeRank(to))) {
                            (*moveList++).move = makePromoteMove<Capture>(pt, from, to, pos);
                        }
                    });
                    break;
                }
                case Silver: // 銀
                {
                    Bitboard toBB = silverAttack(opp, ksq) & silverAttack(US, from) & target;
                    FOREACH_BB(toBB, const Square to, {
                        (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                    });
                    // 成って王手
                    toBB = goldAttack(opp, ksq) & silverAttack(US, from) & target;
                    FOREACH_BB(toBB, const Square to, {
                        if (canPromote(US, makeRank(to)) | canPromote(US, makeRank(from))) {
                            (*moveList++).move = makePromoteMove<Capture>(pt, from, to, pos);
                        }
                    });
                    break;
                }
                case Gold: // 金
                case ProPawn: // と金
                case ProLance: // 成香
                case ProKnight: // 成桂
                case ProSilver: // 成銀
                {
                    Bitboard toBB = goldAttack(opp, ksq) & goldAttack(US, from) & target;
                    FOREACH_BB(toBB, const Square to, {
                        (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                    });
                    break;
                }
                case Bishop: // 角
                {
                    // 玉が対角上にない場合
                    if (abs(makeFile(ksq) - makeFile(from)) != abs(makeRank(ksq) - makeRank(from))) {
                        Bitboard toBB = horseAttack(ksq, pos.occupiedBB()) & bishopAttack(from, pos.occupiedBB()) & target;
                        const Bitboard bishopBB = bishopAttack(ksq, pos.occupiedBB());
                        FOREACH_BB(toBB, const Square to, {
                            // 成る
                            if (canPromote(US, makeRank(to)) | canPromote(US, makeRank(from))) {
                                (*moveList++).move = makePromoteMove<Capture>(pt, from, to, pos);
                                if (ALL) {
                                    if (bishopBB.isSet(to)) {
                                        (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                                    }

                                }
                            }
                            else if (bishopBB.isSet(to)) {
                                (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                            }
                        });
                    }
                    // 対角上にある場合
                    else {
                        // 間にある駒が一つで、敵駒の場合
                        Bitboard dstBB = betweenBB(from, ksq) & pos.occupiedBB();
                        if (dstBB.isOneBit() && dstBB & pos.bbOf(opp)) {
                            const Square to = dstBB.firstOneFromSQ11();
                            // 成って王手
                            if (canPromote(US, makeRank(to)) | canPromote(US, makeRank(from))) {
                                (*moveList++).move = makePromoteMove<Capture>(pt, from, to, pos);
                                // 成らない手を後に生成
                                if (ALL) {
                                    (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                                }
                            }
                            else {
                                (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                            }
                        }
                    }
                    break;
                }
                case Rook: // 飛車
                {
                    // 玉が直線上にない場合
                    if (makeFile(ksq) != makeFile(from) && makeRank(ksq) != makeRank(from)) {
                        Bitboard toBB = dragonAttack(ksq, pos.occupiedBB()) & rookAttack(from, pos.occupiedBB()) & target;
                        const Bitboard rookBB = rookAttack(ksq, pos.occupiedBB());
                        FOREACH_BB(toBB, const Square to, {
                            // 成る
                            if (canPromote(US, makeRank(to)) | canPromote(US, makeRank(from))) {
                                (*moveList++).move = makePromoteMove<Capture>(pt, from, to, pos);
                                if (ALL) {
                                    if (rookBB.isSet(to)) {
                                        (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                                    }
                                }
                            }
                            else if (rookBB.isSet(to)) {
                                (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                            }
                        });
                    }
                    // 直線上にある場合
                    else {
                        // 間にある駒が一つで、敵駒の場合
                        Bitboard dstBB = betweenBB(from, ksq) & pos.occupiedBB();
                        if (dstBB.isOneBit() && dstBB & pos.bbOf(opp)) {
                            const Square to = dstBB.firstOneFromSQ11();
                            // 成って王手
                            if (canPromote(US, makeRank(to)) | canPromote(US, makeRank(from))) {
                                (*moveList++).move = makePromoteMove<Capture>(pt, from, to, pos);
                                // 成らない手を後に生成
                                if (ALL) {
                                    (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                                }
                            }
                            else {
                                (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                            }
                        }
                    }
                    break;
                }
                case Horse: // 馬
                {
                    // 玉が対角上にない場合
                    if (abs(makeFile(ksq) - makeFile(from)) != abs(makeRank(ksq) - makeRank(from))) {
                        Bitboard toBB = horseAttack(ksq, pos.occupiedBB()) & horseAttack(from, pos.occupiedBB()) & target;
                        FOREACH_BB(toBB, const Square to, {
                            (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                        });
                    }
                    // 対角上にある場合
                    else {
                        // 間にある駒が一つで、敵駒の場合
                        Bitboard dstBB = betweenBB(from, ksq) & pos.occupiedBB();
                        if (dstBB.isOneBit() && dstBB & pos.bbOf(opp)) {
                            const Square to = dstBB.firstOneFromSQ11();
                            (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                        }
                    }
                    break;
                }
                case Dragon: // 竜
                {
                    // 玉が直線上にない場合
                    if (makeFile(ksq) != makeFile(from) && makeRank(ksq) != makeRank(from)) {
                        Bitboard toBB = dragonAttack(ksq, pos.occupiedBB()) & dragonAttack(from, pos.occupiedBB()) & target;
                        FOREACH_BB(toBB, const Square to, {
                            (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                        });
                    }
                    // 直線上にある場合
                    else {
                        Bitboard toBB = kingAttack(ksq) & kingAttack(from) & target;
                        // 間にある駒が一つで、敵駒の場合
                        const Bitboard dstBB = betweenBB(from, ksq) & pos.occupiedBB();
                        if (dstBB.isOneBit() && dstBB & pos.bbOf(opp)) {
                            toBB |= dstBB;
                        }
                        FOREACH_BB(toBB, const Square to, {
                            (*moveList++).move = makeNonPromoteMove<Capture>(pt, from, to, pos);
                        });
                    }
                    break;
                }
                default: UNREACHABLE;
                }
            }

            const Bitboard pinned = pos.pinnedBB();

            // pinされている駒の移動による自殺手を削除
            while (curr != moveList) {
                if (!pos.pseudoLegalMoveIsLegal<true, false>(curr->move, pinned))
                    curr->move = (--moveList)->move;
                else
                    ++curr;
            }

            // --- 駒打ちによる王手

            const Bitboard dropTarget = pos.nOccupiedBB(); // emptyBB() ではないので注意して使うこと。
            const Hand ourHand = pos.hand(US);

            // 歩打ち
            if (ourHand.exists<HPawn>()) {
                Bitboard toBB = dropTarget & pawnAttack(opp, ksq);
                // 二歩の回避
                Bitboard pawnsBB = pos.bbOf(Pawn, US);
                Square pawnsSquare;
                foreachBB(pawnsBB, pawnsSquare, [&](const int part) {
                    toBB.set(part, toBB.p(part) & ~squareFileMask(pawnsSquare).p(part));
                });

                // 打ち歩詰めの回避
                constexpr Rank TRank9 = (US == Black ? Rank9 : Rank1);
                constexpr SquareDelta TDeltaS = (US == Black ? DeltaS : DeltaN);

                // 相手玉が九段目なら、歩で王手出来ないので、打ち歩詰めを調べる必要はない。
                if (makeRank(ksq) != TRank9) {
                    const Square pawnDropCheckSquare = ksq + TDeltaS;
                    assert(isInSquare(pawnDropCheckSquare));
                    if (toBB.isSet(pawnDropCheckSquare) && pos.piece(pawnDropCheckSquare) == Empty) {
                        if (!pos.isPawnDropCheckMate(US, pawnDropCheckSquare))
                            // ここで clearBit だけして MakeMove しないことも出来る。
                            // 指し手が生成される順番が変わり、王手が先に生成されるが、後で問題にならないか?
                            (*moveList++).move = makeDropMove(Pawn, pawnDropCheckSquare);
                        toBB.xorBit(pawnDropCheckSquare);
                    }
                }

                Square to;
                FOREACH_BB(toBB, to, {
                    (*moveList++).move = makeDropMove(Pawn, to);
                });
            }

            // 香車打ち
            if (ourHand.exists<HLance>()) {
                Bitboard toBB = dropTarget & lanceAttack(opp, ksq, pos.occupiedBB());
                Square to;
                FOREACH_BB(toBB, to, {
                    (*moveList++).move = makeDropMove(Lance, to);
                });
            }

            // 桂馬打ち
            if (ourHand.exists<HKnight>()) {
                Bitboard toBB = dropTarget & knightAttack(opp, ksq);
                Square to;
                FOREACH_BB(toBB, to, {
                    (*moveList++).move = makeDropMove(Knight, to);
                });
            }

            // 銀打ち
            if (ourHand.exists<HSilver>()) {
                Bitboard toBB = dropTarget & silverAttack(opp, ksq);
                Square to;
                FOREACH_BB(toBB, to, {
                    (*moveList++).move = makeDropMove(Silver, to);
                });
            }

            // 金打ち
            if (ourHand.exists<HGold>()) {
                Bitboard toBB = dropTarget & goldAttack(opp, ksq);
                Square to;
                FOREACH_BB(toBB, to, {
                    (*moveList++).move = makeDropMove(Gold, to);
                });
            }

            // 角打ち
            if (ourHand.exists<HBishop>()) {
                Bitboard toBB = dropTarget & bishopAttack(ksq, pos.occupiedBB());
                Square to;
                FOREACH_BB(toBB, to, {
                    (*moveList++).move = makeDropMove(Bishop, to);
                });
            }

            // 飛車打ち
            if (ourHand.exists<HRook>()) {
                Bitboard toBB = dropTarget & rookAttack(ksq, pos.occupiedBB());
                Square to;
                FOREACH_BB(toBB, to, {
                    (*moveList++).move = makeDropMove(Rook, to);
                });
            }

            return moveList;
        }
    };

    // 部分特殊化
    // Check のときに歩、飛、角と、香の2段目の不成も生成する。
    template <Color US> struct GenerateMoves<CheckAll, US> {
        FORCE_INLINE ExtMove* operator () (ExtMove* moveList, const Position& pos) {
            return GenerateMoves<Check, US, true>()(moveList, pos);
        }
    };

    template <Color US> struct GenerateMoves<CheckAllOslmate, US> {
        FORCE_INLINE ExtMove* operator () (ExtMove* moveList, const Position& pos) {
            return generate_check_moves_oslmate<US, true>(moveList, pos);
        }
    };

    template <Color US> struct GenerateMoves<CheckAllOslmateFixedRaw, US> {
        FORCE_INLINE ExtMove* operator () (ExtMove* moveList, const Position& pos) {
            return generate_fixed_depth_check_moves_oslmate<US, true>(moveList, pos);
        }
    };
}

template <MoveType MT>
ExtMove* generateMoves(ExtMove* moveList, const Position& pos) {
    return (pos.turn() == Black ?
            GenerateMoves<MT, Black>()(moveList, pos) : GenerateMoves<MT, White>()(moveList, pos));
}
template <MoveType MT>
ExtMove* generateMoves(ExtMove* moveList, const Position& pos, const Square to) {
    return generateRecaptureMoves(moveList, pos, to, pos.turn());
}

namespace {
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
        if (square == SquareNum) {
            return 0;
        }
        return (static_cast<int>(makeFile(square)) + 1) * 16
            + (static_cast<int>(makeRank(square)) + 1) + 1;
    }

    int effect_count(const Position& pos, const Color color, const Square sq) {
        return pos.attackersTo(color, sq).popCount();
    }

    auto move_sort_key(const Position& pos, const Color turn, const Move move) {
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

    void sort_oslmate_escape_moves(const Position& pos, const Color turn, ExtMove* first, ExtMove* last) {
        size_t last_sorted = 0;
        size_t cur = 0;
        PieceType last_piece_type = Occupied;
        const size_t size = static_cast<size_t>(last - first);
        for (; cur < size; ++cur) {
            const PieceType piece_type = first[cur].move.isDrop()
                ? Occupied
                : first[cur].move.pieceTypeFrom();
            if (first[cur].move.isDrop() || piece_type == last_piece_type) {
                continue;
            }
            std::sort(first + static_cast<std::ptrdiff_t>(last_sorted), first + static_cast<std::ptrdiff_t>(cur),
                [&](const ExtMove& lhs, const ExtMove& rhs) {
                    return move_sort_key(pos, turn, lhs.move) > move_sort_key(pos, turn, rhs.move);
                });
            last_sorted = cur;
            last_piece_type = piece_type;
        }
        std::sort(first + static_cast<std::ptrdiff_t>(last_sorted), first + static_cast<std::ptrdiff_t>(cur),
            [&](const ExtMove& lhs, const ExtMove& rhs) {
                return move_sort_key(pos, turn, lhs.move) > move_sort_key(pos, turn, rhs.move);
            });
    }

    bool contains_ext_move(const ExtMove* first, const ExtMove* last, const Move move) {
        for (const ExtMove* it = first; it != last; ++it) {
            if (it->move == move) {
                return true;
            }
        }
        return false;
    }

    bool has_drop_piece(const Hand hand, const PieceType pt) {
        switch (pt) {
        case Pawn: return hand.exists<HPawn>();
        case Lance: return hand.exists<HLance>();
        case Knight: return hand.exists<HKnight>();
        case Silver: return hand.exists<HSilver>();
        case Gold: return hand.exists<HGold>();
        case Bishop: return hand.exists<HBishop>();
        case Rook: return hand.exists<HRook>();
        default: return false;
        }
    }

    template <Color US>
    bool can_oslmate_drop_to(const Position& pos, const PieceType pt, const Square to) {
        const Rank rank = makeRank(to);
        if (pt == Pawn) {
            const Rank last_rank = (US == Black ? Rank1 : Rank9);
            if (rank == last_rank) {
                return false;
            }
            Bitboard pawns = pos.bbOf(Pawn, US);
            while (pawns) {
                const Square pawn = pawns.firstOneFromSQ11();
                if (makeFile(pawn) == makeFile(to)) {
                    return false;
                }
            }
            return true;
        }
        if (pt == Lance) {
            const Rank last_rank = (US == Black ? Rank1 : Rank9);
            return rank != last_rank;
        }
        if (pt == Knight) {
            const Rank last_rank = (US == Black ? Rank1 : Rank9);
            const Rank second_last_rank = (US == Black ? Rank2 : Rank8);
            return rank != last_rank && rank != second_last_rank;
        }
        return true;
    }

    template <Color US>
    void append_escape_if_legal(const Position& pos, const Bitboard& pinned,
        const Move move, const ExtMove* first, ExtMove*& last) {
        if (!pos.moveIsPseudoLegal<false>(move)) {
            return;
        }
        if (!pos.pseudoLegalMoveIsLegal<false, false>(move, pinned)) {
            return;
        }
        if (contains_ext_move(first, last, move)) {
            return;
        }
        (*last++).move = move;
    }

    template <Color US>
    bool osl_pin_or_open_piece(const Position& pos, const Square from) {
        const Piece piece = pos.piece(from);
        if (piece == Empty) {
            return false;
        }
        const Color us = pieceToColor(piece);
        const Color them = oppositeColor(us);
        const Square king = pos.kingSquare(us);
        const int file_delta = static_cast<int>(makeFile(from)) - static_cast<int>(makeFile(king));
        const int rank_delta = static_cast<int>(makeRank(from)) - static_cast<int>(makeRank(king));
        if (!(file_delta == 0 || rank_delta == 0 || std::abs(file_delta) == std::abs(rank_delta))) {
            return false;
        }
        if ((betweenBB(king, from) & pos.occupiedBB()).isAny()) {
            return false;
        }

        const int file_step = file_delta == 0 ? 0 : (file_delta > 0 ? 1 : -1);
        const int rank_step = rank_delta == 0 ? 0 : (rank_delta > 0 ? 1 : -1);
        for (int file = static_cast<int>(makeFile(from)) + file_step,
                 rank = static_cast<int>(makeRank(from)) + rank_step;
             isInSquare(static_cast<File>(file), static_cast<Rank>(rank));
             file += file_step, rank += rank_step) {
            const Square sq = makeSquare(static_cast<File>(file), static_cast<Rank>(rank));
            const Piece behind = pos.piece(sq);
            if (behind == Empty) {
                continue;
            }
            if (pieceToColor(behind) != them) {
                return false;
            }
            const PieceType behind_type = pieceToPieceType(behind);
            if (file_step != 0 && rank_step != 0) {
                return (behind_type == Bishop || behind_type == Horse)
                    && bishopAttack(sq, pos.occupiedBB()).isSet(from);
            }
            if (behind_type == Rook || behind_type == Dragon) {
                return rookAttack(sq, pos.occupiedBB()).isSet(from);
            }
            if (file_step == 0 && behind_type == Lance) {
                return lanceAttack(them, sq, pos.occupiedBB()).isSet(from);
            }
            return false;
        }
        return false;
    }

    template <Color US>
    bool osl_pin_or_open_can_move_to(const Position& pos, const Square from, const Square to) {
        if (!osl_pin_or_open_piece<US>(pos, from)) {
            return true;
        }
        const Square king = pos.kingSquare(US);
        return isAligned<true>(from, to, king);
    }

    template <Color US>
    void append_escape_if_osl_capture_or_block(const Position& pos, const Bitboard& pinned,
        const Move move, const ExtMove* first, ExtMove*& last) {
        (void)pinned;
        if (!pos.moveIsPseudoLegal<false>(move)) {
            return;
        }
        if (!move.isDrop() && !osl_pin_or_open_can_move_to<US>(pos, move.from(), move.to())) {
            return;
        }
        if (contains_ext_move(first, last, move)) {
            return;
        }
        (*last++).move = move;
    }

    template <Color US>
    void append_escape_piece_variants(const Position& pos, const Bitboard& pinned,
        const Square from, const Square to, const ExtMove* first, ExtMove*& last) {
        const PieceType pt = pieceToPieceType(pos.piece(from));
        const auto append_non_promote = [&]() {
            append_escape_if_osl_capture_or_block<US>(pos, pinned, makeNonPromoteMove<Evasion>(pt, from, to, pos), first, last);
        };
        const auto append_promote = [&]() {
            append_escape_if_osl_capture_or_block<US>(pos, pinned, makePromoteMove<Evasion>(pt, from, to, pos), first, last);
        };
        const bool from_can_promote = canPromote(US, makeRank(from));
        const bool to_can_promote = canPromote(US, makeRank(to));

        switch (pt) {
        case Pawn:
            if (to_can_promote) {
                append_promote();
            }
            else {
                append_non_promote();
            }
            break;
        case Lance:
            if (to_can_promote) {
                append_promote();
                if (isBehind<US, Rank2, Rank8>(makeRank(to))) {
                    append_non_promote();
                }
            }
            else {
                append_non_promote();
            }
            break;
        case Knight:
            if (to_can_promote) {
                append_promote();
                if (isBehind<US, Rank2, Rank8>(makeRank(to))) {
                    append_non_promote();
                }
            }
            else {
                append_non_promote();
            }
            break;
        case Silver:
            if (from_can_promote || to_can_promote) {
                append_promote();
            }
            append_non_promote();
            break;
        case Bishop:
        case Rook:
            if (from_can_promote || to_can_promote) {
                append_promote();
            }
            else {
                append_non_promote();
            }
            break;
        case Gold:
        case King:
        case ProPawn:
        case ProLance:
        case ProKnight:
        case ProSilver:
        case Horse:
        case Dragon:
            append_non_promote();
            break;
        default:
            break;
        }
    }

    template <Color US>
    void append_escape_moves_to_target(const Position& pos, const Bitboard& pinned,
        const Square target, const Square excluded_from, const ExtMove* first, ExtMove*& last) {
        Bitboard attackers = pos.attackersTo(US, target);
        if (excluded_from != SquareNum) {
            attackers &= ~setMaskBB(excluded_from);
        }
        std::array<Square, 32> from_squares{};
        size_t from_count = 0;
        while (attackers) {
            const Square from = attackers.firstOneFromSQ11();
            if (from != pos.kingSquare(US)) {
                from_squares[from_count++] = from;
            }
        }
        std::stable_sort(from_squares.begin(), from_squares.begin() + static_cast<std::ptrdiff_t>(from_count),
            [&](const Square lhs, const Square rhs) {
                const PieceType lhs_type = pieceToPieceType(pos.piece(lhs));
                const PieceType rhs_type = pieceToPieceType(pos.piece(rhs));
                const int lhs_key = osl_piece_number_group_key(lhs_type) * 256 + osl_square_index_for_sort(lhs);
                const int rhs_key = osl_piece_number_group_key(rhs_type) * 256 + osl_square_index_for_sort(rhs);
                return lhs_key < rhs_key;
            });
        for (size_t i = 0; i < from_count; ++i) {
            append_escape_piece_variants<US>(pos, pinned, from_squares[i], target, first, last);
        }
    }

    template <Color US>
    void append_escape_cheapest_block_move_to_target(const Position& pos, const Bitboard& pinned,
        const Square target, const ExtMove* first, ExtMove*& last) {
        const Bitboard all_attackers = pos.attackersTo(US, target);
        if (all_attackers.popCount() < 2) {
            return;
        }

        Bitboard attackers = all_attackers & ~pos.bbOf(King, US);
        Square selected = SquareNum;
        int selected_key = 1 << 30;
        while (attackers) {
            const Square from = attackers.firstOneFromSQ11();
            const PieceType pt = pieceToPieceType(pos.piece(from));
            const int order = osl_escape_block_piece_order(pt);
            if (order >= 128) {
                continue;
            }
            const int key = order * 1024 + immediate_osl_piece_number_from_position(pos, from);
            if (key < selected_key) {
                selected_key = key;
                selected = from;
            }
        }

        if (selected != SquareNum) {
            append_escape_piece_variants<US>(pos, pinned, selected, target, first, last);
        }
    }

    template <Color US, bool CheapOnly>
    void append_escape_drops_to_target(const Position& pos, const Bitboard& pinned,
        const Square target, const ExtMove* first, ExtMove*& last) {
        static constexpr std::array<PieceType, 7> kDropOrder = {
            Pawn, Lance, Knight, Silver, Gold, Bishop, Rook
        };

        const Hand our_hand = pos.hand(US);
        for (const PieceType pt : kDropOrder) {
            if (!has_drop_piece(our_hand, pt)) {
                continue;
            }
            if (!can_oslmate_drop_to<US>(pos, pt, target)) {
                continue;
            }
            const Move move = makeDropMove(pt, target);
            if (!pos.moveIsPseudoLegal<false>(move)) {
                continue;
            }
            if (contains_ext_move(first, last, move)) {
                continue;
            }
            (*last++).move = move;
            if (CheapOnly) {
                return;
            }
        }
    }

    template <Color US>
    void append_escape_king_moves(const Position& pos, const Bitboard& pinned,
        const ExtMove* first, ExtMove*& last) {
        const Square king = pos.kingSquare(US);
        // OSL PieceOnBoard::generateKing emits king moves in
        // UL, DR, U, D, UR, DL, L, R order, with directions oriented by side.
        static constexpr std::array<std::pair<int, int>, 8> kBlackOrder = { {
            { 1, -1 }, { -1, 1 }, { 0, -1 }, { 0, 1 },
            { -1, -1 }, { 1, 1 }, { 1, 0 }, { -1, 0 }
        } };
        static constexpr std::array<std::pair<int, int>, 8> kWhiteOrder = { {
            { -1, 1 }, { 1, -1 }, { 0, 1 }, { 0, -1 },
            { 1, 1 }, { -1, -1 }, { -1, 0 }, { 1, 0 }
        } };
        const auto& order = US == Black ? kBlackOrder : kWhiteOrder;
        for (const auto& [file_delta, rank_delta] : order) {
            const int file = static_cast<int>(makeFile(king)) + file_delta;
            const int rank = static_cast<int>(makeRank(king)) + rank_delta;
            if (!isInSquare(static_cast<File>(file), static_cast<Rank>(rank))) {
                continue;
            }
            const Square to = makeSquare(static_cast<File>(file), static_cast<Rank>(rank));
            if (pos.bbOf(US).isSet(to)) {
                continue;
            }
            if (!osl_king8_liberty(pos, US, to)) {
                continue;
            }
            append_escape_if_legal<US>(pos, pinned, makeCaptureMove(King, king, to, pos), first, last);
        }
    }

    template <Color US, bool CheapOnly>
    ExtMove* generate_oslmate_escape_moves_impl(ExtMove* moveList, const Position& pos, const bool sortMoves) {
        assert(pos.inCheck());

        const ExtMove* first = moveList;
        ExtMove* last = moveList;
        const Bitboard pinned = pos.pinnedBB();
        const Square king = pos.kingSquare(US);
        Bitboard checkers = pos.checkersBB();
        const int checker_count = checkers.popCount();

        if (checker_count != 1) {
            append_escape_king_moves<US>(pos, pinned, first, last);
            if (sortMoves) {
                sort_oslmate_escape_moves(pos, US, moveList, last);
            }
            return last;
        }

        const Square checker = checkers.firstOneFromSQ11();
        const auto trace_issue56_escape_stage = [&](const char* phase) {
#ifndef NDEBUG
            const char* enabled = std::getenv("CSHOGI_DFPN_DEBUG");
            if (!enabled || !*enabled) {
                return;
            }
            bool has_2b3b = false;
            bool has_s4c = false;
            for (const ExtMove* it = first; it != last; ++it) {
                has_2b3b = has_2b3b || it->move.toUSI() == "2b3b";
                has_s4c = has_s4c || it->move.toUSI() == "S*4c";
            }
            if (!has_2b3b && !has_s4c) {
                return;
            }
            std::fprintf(stderr,
                "cshogi issue56 escape gen phase=%s sfen=%s checker=%s count=%zu cheap=%d sort=%d\n",
                phase,
                pos.toSFEN().c_str(),
                squareToStringUSI(checker).c_str(),
                static_cast<size_t>(last - first),
                CheapOnly ? 1 : 0,
                sortMoves ? 1 : 0);
            for (const ExtMove* it = first; it != last; ++it) {
                std::fprintf(stderr, "  %s\n", it->move.toUSI().c_str());
            }
#else
            (void)phase;
#endif
        };
        append_escape_moves_to_target<US>(pos, pinned, checker, king, first, last);
        trace_issue56_escape_stage("capture-checker");
        append_escape_king_moves<US>(pos, pinned, first, last);
        trace_issue56_escape_stage("king");

        const int king_file = static_cast<int>(makeFile(king));
        const int king_rank = static_cast<int>(makeRank(king));
        const int checker_file = static_cast<int>(makeFile(checker));
        const int checker_rank = static_cast<int>(makeRank(checker));
        const int file_step = checker_file == king_file ? 0 : (checker_file > king_file ? 1 : -1);
        const int rank_step = checker_rank == king_rank ? 0 : (checker_rank > king_rank ? 1 : -1);
        std::array<Square, 8> blocking_squares{};
        size_t blocking_count = 0;
        for (int file = king_file + file_step, rank = king_rank + rank_step;
            file != checker_file || rank != checker_rank;
            file += file_step, rank += rank_step) {
            if (!isInSquare(static_cast<File>(file), static_cast<Rank>(rank))) {
                break;
            }
            const Square block_sq = makeSquare(static_cast<File>(file), static_cast<Rank>(rank));
            if (betweenBB(checker, king).isSet(block_sq)) {
                blocking_squares[blocking_count++] = block_sq;
            }
        }
        for (size_t i = 0; i < blocking_count; ++i) {
            const Square block_sq = blocking_squares[i];
            // OSL Escape::generateBlockingKing keeps all moving interpositions even
            // for CheapOnly; only drops are reduced to the cheapest piece.
            append_escape_moves_to_target<US>(pos, pinned, block_sq, king, first, last);
            append_escape_drops_to_target<US, CheapOnly>(pos, pinned, block_sq, first, last);
            trace_issue56_escape_stage("block");
        }

        if (sortMoves) {
            sort_oslmate_escape_moves(pos, US, moveList, last);
            trace_issue56_escape_stage("sort");
        }

        return last;
    }

    template <Color US>
    ExtMove* generate_oslmate_escape_nonblock_moves_impl(ExtMove* moveList, const Position& pos, const bool sortMoves) {
        assert(pos.inCheck());

        const ExtMove* first = moveList;
        ExtMove* last = moveList;
        const Bitboard pinned = pos.pinnedBB();
        const Square king = pos.kingSquare(US);
        Bitboard checkers = pos.checkersBB();
        const int checker_count = checkers.popCount();

        if (checker_count == 1) {
            const Square checker = checkers.firstOneFromSQ11();
            append_escape_moves_to_target<US>(pos, pinned, checker, king, first, last);
        }
        append_escape_king_moves<US>(pos, pinned, first, last);

        if (sortMoves) {
            sort_oslmate_escape_moves(pos, US, moveList, last);
        }

        return last;
    }

    template <Color US>
    ExtMove* generate_oslmate_cheap_king_escape_moves_impl(ExtMove* moveList, const Position& pos, const bool sortMoves) {
        return generate_oslmate_escape_moves_impl<US, true>(moveList, pos, sortMoves);
    }
}

ExtMove* generateOslmateEscapeMoves(ExtMove* moveList, const Position& pos, const bool cheapOnly, const bool sortMoves) {
    if (!cheapOnly && sortMoves) {
        std::array<ExtMove, MaxLegalMoves> full{};
        ExtMove* cheap_last = pos.turn() == Black
            ? generate_oslmate_escape_moves_impl<Black, true>(moveList, pos, true)
            : generate_oslmate_escape_moves_impl<White, true>(moveList, pos, true);
        ExtMove* full_last = pos.turn() == Black
            ? generate_oslmate_escape_moves_impl<Black, false>(full.data(), pos, true)
            : generate_oslmate_escape_moves_impl<White, false>(full.data(), pos, true);

        const ExtMove* first = moveList;
        const ExtMove* original_last = cheap_last;
        ExtMove* out = cheap_last;
        for (ExtMove* it = full.data(); it != full_last; ++it) {
            if (!contains_ext_move(first, original_last, it->move)) {
                (*out++).move = it->move;
            }
        }
        return out;
    }

    if (pos.turn() == Black) {
        return cheapOnly
            ? generate_oslmate_escape_moves_impl<Black, true>(moveList, pos, sortMoves)
            : generate_oslmate_escape_moves_impl<Black, false>(moveList, pos, sortMoves);
    }
    return cheapOnly
        ? generate_oslmate_escape_moves_impl<White, true>(moveList, pos, sortMoves)
        : generate_oslmate_escape_moves_impl<White, false>(moveList, pos, sortMoves);
}

ExtMove* generateOslmateCheapKingEscapeMoves(ExtMove* moveList, const Position& pos, const bool sortMoves) {
    return pos.turn() == Black
        ? generate_oslmate_cheap_king_escape_moves_impl<Black>(moveList, pos, sortMoves)
        : generate_oslmate_cheap_king_escape_moves_impl<White>(moveList, pos, sortMoves);
}

ExtMove* generateOslmateEscapeNonblockMoves(ExtMove* moveList, const Position& pos, const bool sortMoves) {
    return pos.turn() == Black
        ? generate_oslmate_escape_nonblock_moves_impl<Black>(moveList, pos, sortMoves)
        : generate_oslmate_escape_nonblock_moves_impl<White>(moveList, pos, sortMoves);
}

// 明示的なインスタンス化
// これが無いと、他のファイルから呼んだ時に、
// 実体が無いためにリンクエラーになる。
// ちなみに、特殊化されたテンプレート関数は、明示的なインスタンス化の必要はない。
// 実装を cpp に置くことで、コンパイル時間の短縮が出来る。
//template ExtMove* generateMoves<Capture           >(ExtMove* moveList, const Position& pos);
//template ExtMove* generateMoves<NonCapture        >(ExtMove* moveList, const Position& pos);
template ExtMove* generateMoves<Drop              >(ExtMove* moveList, const Position& pos);
template ExtMove* generateMoves<CapturePlusPro    >(ExtMove* moveList, const Position& pos);
template ExtMove* generateMoves<NonCaptureMinusPro>(ExtMove* moveList, const Position& pos);
template ExtMove* generateMoves<Evasion           >(ExtMove* moveList, const Position& pos);
template ExtMove* generateMoves<NonEvasion        >(ExtMove* moveList, const Position& pos);
template ExtMove* generateMoves<Legal             >(ExtMove* moveList, const Position& pos);
template ExtMove* generateMoves<LegalAll          >(ExtMove* moveList, const Position& pos);
template ExtMove* generateMoves<PseudoLegal       >(ExtMove* moveList, const Position& pos);
template ExtMove* generateMoves<Recapture         >(ExtMove* moveList, const Position& pos, const Square to);
template ExtMove* generateMoves<Check             >(ExtMove* moveList, const Position& pos);
template ExtMove* generateMoves<CheckAll          >(ExtMove* moveList, const Position& pos);
template ExtMove* generateMoves<CheckAllOslmate   >(ExtMove* moveList, const Position& pos);
template ExtMove* generateMoves<CheckAllOslmateFixedRaw>(ExtMove* moveList, const Position& pos);
