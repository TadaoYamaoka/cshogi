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

#include "book.hpp"
#include "position.hpp"

MT64bit Book::mt64bit_; // 定跡のhash生成用なので、seedは固定でデフォルト値を使う。
Key Book::ZobPiece[PieceNone][SquareNum];
Key Book::ZobHand[HandPieceNum][19]; // 持ち駒の同一種類の駒の数ごと
Key Book::ZobTurn;

void Book::init() {
    for (Piece p = Empty; p < PieceNone; ++p) {
        for (Square sq = SQ11; sq < SquareNum; ++sq)
            ZobPiece[p][sq] = mt64bit_.random();
    }
    for (HandPiece hp = HPawn; hp < HandPieceNum; ++hp) {
        for (int num = 0; num < 19; ++num)
            ZobHand[hp][num] = mt64bit_.random();
    }
    ZobTurn = mt64bit_.random();
}

Key Book::bookKey(const Position& pos) {
    Key key = 0;
    Bitboard bb = pos.occupiedBB();

    while (bb) {
        const Square sq = bb.firstOneFromSQ11();
        key ^= ZobPiece[pos.piece(sq)][sq];
    }
    const Hand hand = pos.hand(pos.turn());
    for (HandPiece hp = HPawn; hp < HandPieceNum; ++hp)
        key ^= ZobHand[hp][hand.numOf(hp)];
    if (pos.turn() == White)
        key ^= ZobTurn;
    return key;
}

inline bool countCompare(const BookEntry& b1, const BookEntry& b2) {
    return b1.count < b2.count;
}
