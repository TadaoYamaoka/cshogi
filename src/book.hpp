﻿/*
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

#ifndef APERY_BOOK_HPP
#define APERY_BOOK_HPP

#include "position.hpp"
#include "mt64bit.hpp"

struct BookEntry {
    Key key;
    u16 fromToPro;
    u16 count;
    int score;
};

class Book {
public:
    static void init();
    static Key bookKey(const Position& pos);
    static Key bookKeyAfter(const Position& pos, const Key key, const Move move);

private:
    static MT64bit mt64bit_; // 定跡のhash生成用なので、seedは固定でデフォルト値を使う。

    static Key ZobPiece[PieceNone][SquareNum];
    static Key ZobHand[HandPieceNum][19];
    static Key ZobTurn;
};

#endif // #ifndef APERY_BOOK_HPP
