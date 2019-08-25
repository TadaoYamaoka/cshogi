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

#include "usi.hpp"
#include "position.hpp"
#include "move.hpp"
#include "generateMoves.hpp"

namespace {
    // 論理的なコア数の取得
    inline int cpuCoreCount() {
        // std::thread::hardware_concurrency() は 0 を返す可能性がある。
        // HyperThreading が有効なら論理コア数だけ thread 生成した方が強い。
        return std::max(static_cast<int>(std::thread::hardware_concurrency()), 1);
    }

    class StringToPieceTypeCSA : public std::map<std::string, PieceType> {
    public:
        StringToPieceTypeCSA() {
            (*this)["FU"] = Pawn;
            (*this)["KY"] = Lance;
            (*this)["KE"] = Knight;
            (*this)["GI"] = Silver;
            (*this)["KA"] = Bishop;
            (*this)["HI"] = Rook;
            (*this)["KI"] = Gold;
            (*this)["OU"] = King;
            (*this)["TO"] = ProPawn;
            (*this)["NY"] = ProLance;
            (*this)["NK"] = ProKnight;
            (*this)["NG"] = ProSilver;
            (*this)["UM"] = Horse;
            (*this)["RY"] = Dragon;
        }
        PieceType value(const std::string& str) const {
            return this->find(str)->second;
        }
        bool isLegalString(const std::string& str) const {
            return (this->find(str) != this->end());
        }
    };
    const StringToPieceTypeCSA g_stringToPieceTypeCSA;
}

Move usiToMoveBody(const Position& pos, const std::string& moveStr) {
    Move move;
    if (g_charToPieceUSI.isLegalChar(moveStr[0])) {
        // drop
        const PieceType ptTo = pieceToPieceType(g_charToPieceUSI.value(moveStr[0]));
        if (moveStr[1] != '*')
            return Move::moveNone();
        const File toFile = charUSIToFile(moveStr[2]);
        const Rank toRank = charUSIToRank(moveStr[3]);
        if (!isInSquare(toFile, toRank))
            return Move::moveNone();
        const Square to = makeSquare(toFile, toRank);
        move = makeDropMove(ptTo, to);
    }
    else {
        const File fromFile = charUSIToFile(moveStr[0]);
        const Rank fromRank = charUSIToRank(moveStr[1]);
        if (!isInSquare(fromFile, fromRank))
            return Move::moveNone();
        const Square from = makeSquare(fromFile, fromRank);
        const File toFile = charUSIToFile(moveStr[2]);
        const Rank toRank = charUSIToRank(moveStr[3]);
        if (!isInSquare(toFile, toRank))
            return Move::moveNone();
        const Square to = makeSquare(toFile, toRank);
        if (moveStr[4] == '\0')
            move = makeNonPromoteMove<Capture>(pieceToPieceType(pos.piece(from)), from, to, pos);
        else if (moveStr[4] == '+') {
            if (moveStr[5] != '\0')
                return Move::moveNone();
            move = makePromoteMove<Capture>(pieceToPieceType(pos.piece(from)), from, to, pos);
        }
        else
            return Move::moveNone();
    }

    if (pos.moveIsPseudoLegal<false>(move)
        && pos.pseudoLegalMoveIsLegal<false, false>(move, pos.pinnedBB()))
    {
        return move;
    }
    return Move::moveNone();
}
#if !defined NDEBUG
// for debug
Move usiToMoveDebug(const Position& pos, const std::string& moveStr) {
    for (MoveList<LegalAll> ml(pos); !ml.end(); ++ml) {
        if (moveStr == ml.move().toUSI())
            return ml.move();
    }
    return Move::moveNone();
}
Move csaToMoveDebug(const Position& pos, const std::string& moveStr) {
    for (MoveList<LegalAll> ml(pos); !ml.end(); ++ml) {
        if (moveStr == ml.move().toCSA())
            return ml.move();
    }
    return Move::moveNone();
}
#endif
Move usiToMove(const Position& pos, const std::string& moveStr) {
    const Move move = usiToMoveBody(pos, moveStr);
    assert(move == usiToMoveDebug(pos, moveStr));
    return move;
}

Move csaToMoveBody(const Position& pos, const std::string& moveStr) {
    if (moveStr.size() != 6)
        return Move::moveNone();
    const File toFile = charCSAToFile(moveStr[2]);
    const Rank toRank = charCSAToRank(moveStr[3]);
    if (!isInSquare(toFile, toRank))
        return Move::moveNone();
    const Square to = makeSquare(toFile, toRank);
    const std::string ptToString(moveStr.begin() + 4, moveStr.end());
    if (!g_stringToPieceTypeCSA.isLegalString(ptToString))
        return Move::moveNone();
    const PieceType ptTo = g_stringToPieceTypeCSA.value(ptToString);
    Move move;
    if (moveStr[0] == '0' && moveStr[1] == '0')
        // drop
        move = makeDropMove(ptTo, to);
    else {
        const File fromFile = charCSAToFile(moveStr[0]);
        const Rank fromRank = charCSAToRank(moveStr[1]);
        if (!isInSquare(fromFile, fromRank))
            return Move::moveNone();
        const Square from = makeSquare(fromFile, fromRank);
        PieceType ptFrom = pieceToPieceType(pos.piece(from));
        if (ptFrom == ptTo)
            // non promote
            move = makeNonPromoteMove<Capture>(ptFrom, from, to, pos);
        else if (ptFrom + PTPromote == ptTo)
            // promote
            move = makePromoteMove<Capture>(ptFrom, from, to, pos);
        else
            return Move::moveNone();
    }

    if (pos.moveIsPseudoLegal<false>(move)
        && pos.pseudoLegalMoveIsLegal<false, false>(move, pos.pinnedBB()))
    {
        return move;
    }
    return Move::moveNone();
}
Move csaToMove(const Position& pos, const std::string& moveStr) {
    const Move move = csaToMoveBody(pos, moveStr);
    assert(move == csaToMoveDebug(pos, moveStr));
    return move;
}
