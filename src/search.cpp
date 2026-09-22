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

#include "search.hpp"
#include "usi.hpp"
#include "generateMoves.hpp"

// 入玉宣言の結果を判定
NyugyokuResult nyugyoku(const Position& pos, const NyugyokuRule rule) {
    // 入玉宣言法の共通条件を全て満たすか判定する。
    // 判定が高速に出来るものから順に判定していく事にする。

    // 一 宣言側の手番である。

    // この関数を呼び出すのは自分の手番のみとする。ponder では呼び出さない。

    // 六 宣言側の持ち時間が残っている。

    // 持ち時間が無ければ既に負けなので、何もチェックしない。

    // 五 宣言側の玉に王手がかかっていない。
    if (pos.inCheck())
        return NyugyokuNone;

    const Color us = pos.turn();
    // 敵陣のマスク
    const Bitboard opponentsField = (us == Black ? inFrontMask<Black, Rank4>() : inFrontMask<White, Rank6>());

    // 二 宣言側の玉が敵陣三段目以内に入っている。
    if (!pos.bbOf(King, us).andIsAny(opponentsField))
        return NyugyokuNone;

    // 四 宣言側の敵陣三段目以内の駒は、玉を除いて10枚以上存在する。
    const int ownPiecesCount = (pos.bbOf(us) & opponentsField).popCount() - 1;
    if (ownPiecesCount < 10)
        return NyugyokuNone;

    // 三 宣言側が、大駒5点小駒1点で計算して必要な持点がある。
    //     24点法は31点以上で勝ち、24点以上30点以下で無勝負。
    //     27点法は先手28点以上、後手27点以上で勝ち。
    //     点数の対象となるのは、宣言側の持駒と敵陣三段目以内に存在する玉を除く宣言側の駒のみである。
    const int ownBigPiecesCount = (pos.bbOf(Rook, Dragon, Bishop, Horse) & opponentsField & pos.bbOf(us)).popCount();
    const int ownSmallPiecesCount = ownPiecesCount - ownBigPiecesCount;
    const Hand hand = pos.hand(us);
    const int val = ownSmallPiecesCount
        + hand.numOf<HPawn>() + hand.numOf<HLance>() + hand.numOf<HKnight>()
        + hand.numOf<HSilver>() + hand.numOf<HGold>()
        + (ownBigPiecesCount + hand.numOf<HRook>() + hand.numOf<HBishop>()) * 5;
    if (rule == LAW_24) {
        if (val >= 31)
            return NyugyokuWin;
        if (val >= 24)
            return NyugyokuDraw;
        return NyugyokuNone;
    }

    return val >= (us == Black ? 28 : 27) ? NyugyokuWin : NyugyokuNone;
}
