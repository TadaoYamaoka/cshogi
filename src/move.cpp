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

#include "move.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace {
    const std::string HandPieceToStringTable[HandPieceNum] = {"P*", "L*", "N*", "S*", "G*", "B*", "R*"};
    inline std::string handPieceToString(const HandPiece hp) { return HandPieceToStringTable[hp]; }

    const std::string PieceTypeToStringTable[PieceTypeNum] = {
        "", "FU", "KY", "KE", "GI", "KA", "HI", "KI", "OU", "TO", "NY", "NK", "NG", "UM", "RY"
    };
    inline std::string pieceTypeToString(const PieceType pt) { return PieceTypeToStringTable[pt]; }

    struct MoveProfileCounter {
        uint64_t calls = 0;
        uint64_t ns = 0;
    };

#ifndef CSHOGI_ENABLE_MOVE_PROFILE_CODE
#define CSHOGI_ENABLE_MOVE_PROFILE_CODE 0
#endif

#if CSHOGI_ENABLE_MOVE_PROFILE_CODE
    bool move_profile_enabled() {
        static const bool enabled = []() {
            char* value = nullptr;
            size_t value_len = 0;
            const errno_t env_err = _dupenv_s(&value, &value_len, "CSHOGI_MOVE_PROFILE");
            const bool result = env_err == 0 && value != nullptr && value_len > 0;
            free(value);
            return result;
        }();
        return enabled;
    }

    uint64_t move_profile_now_ns() {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    MoveProfileCounter& to_usi_profile_counter() {
        static MoveProfileCounter counter;
        return counter;
    }

    struct MoveProfileDumper {
        ~MoveProfileDumper() {
            if (!move_profile_enabled()) {
                return;
            }
            const auto& counter = to_usi_profile_counter();
            std::fprintf(stderr, "move profile\n");
            std::fprintf(stderr, "  to_usi calls=%llu ms=%.3f\n",
                static_cast<unsigned long long>(counter.calls),
                static_cast<double>(counter.ns) / 1000000.0);
        }
    };

    MoveProfileDumper move_profile_dumper;

    class MoveProfileScope {
    public:
        explicit MoveProfileScope(MoveProfileCounter& counter)
            : counter_(counter), enabled_(move_profile_enabled()), start_(enabled_ ? move_profile_now_ns() : 0) {}

        ~MoveProfileScope() {
            if (!enabled_) {
                return;
            }
            ++counter_.calls;
            counter_.ns += move_profile_now_ns() - start_;
        }

    private:
        MoveProfileCounter& counter_;
        bool enabled_;
        uint64_t start_;
    };
#else
    MoveProfileCounter& to_usi_profile_counter() {
        static MoveProfileCounter counter;
        return counter;
    }

    class MoveProfileScope {
    public:
        explicit MoveProfileScope(MoveProfileCounter&) {}
    };
#endif
}

std::string Move::toUSI() const {
    MoveProfileScope profile(to_usi_profile_counter());
    if (!(*this)) return "None";

    const Square from = this->from();
    const Square to = this->to();
    if (this->isDrop())
        return handPieceToString(this->handPieceDropped()) + squareToStringUSI(to);
    std::string usi = squareToStringUSI(from) + squareToStringUSI(to);
    if (this->isPromotion()) usi += "+";
    return usi;
}

std::string Move::toCSA() const {
    if (!(*this)) return "None";

    std::string s = (this->isDrop() ? std::string("00") : squareToStringCSA(this->from()));
    s += squareToStringCSA(this->to()) + pieceTypeToString(this->pieceTypeTo());
    return s;
}
