#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include "position.hpp"
#include "usi.hpp"

namespace parser {
    std::vector<char> COLOR_SYMBOLS = { '+', '-' };

    std::string rtrim(const std::string& str, const std::string& chars = " ")
    {
        return str.substr(0, str.find_last_not_of(chars) + 1);
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
    };
    const StringToPieceTypeCSA stringToPieceTypeCSA;

    class StringToPieceCSA : public std::map<std::string, Piece> {
    public:
        StringToPieceCSA() {
            (*this)[" * "] = Empty;
            (*this)["+FU"] = BPawn;
            (*this)["+KY"] = BLance;
            (*this)["+KE"] = BKnight;
            (*this)["+GI"] = BSilver;
            (*this)["+KA"] = BBishop;
            (*this)["+HI"] = BRook;
            (*this)["+KI"] = BGold;
            (*this)["+OU"] = BKing;
            (*this)["+TO"] = BProPawn;
            (*this)["+NY"] = BProLance;
            (*this)["+NK"] = BProKnight;
            (*this)["+NG"] = BProSilver;
            (*this)["+UM"] = BHorse;
            (*this)["+RY"] = BDragon;
            (*this)["-FU"] = WPawn;
            (*this)["-KY"] = WLance;
            (*this)["-KE"] = WKnight;
            (*this)["-GI"] = WSilver;
            (*this)["-KA"] = WBishop;
            (*this)["-HI"] = WRook;
            (*this)["-KI"] = WGold;
            (*this)["-OU"] = WKing;
            (*this)["-TO"] = WProPawn;
            (*this)["-NY"] = WProLance;
            (*this)["-NK"] = WProKnight;
            (*this)["-NG"] = WProSilver;
            (*this)["-UM"] = WHorse;
            (*this)["-RY"] = WDragon;
        }
        Piece value(const std::string& str) const {
            return this->find(str)->second;
        }
    };
    const StringToPieceCSA stringToPieceCSA;

    const char* PieceToCharUSITable[PieceNone] = {
        "", "P", "L", "N", "S", "B", "R", "G", "K", "+P", "+L", "+N", "+S", "+B", "+R", "", "",
        "p", "l", "n", "s", "b", "r", "g", "k", "+p", "+l", "+n", "+s", "+b", "+r"
    };

    class __Parser
    {
    public:
        std::string version;
        std::vector<std::string> informations;
        std::string sfen;
        std::string endgame;
        std::vector<std::string> names;
        std::vector<float> ratings;
        std::vector<int> moves;
        std::vector<int> times;
        std::vector<int> scores;
        std::vector<std::string> comments;
        std::string comment;
        int win;

        __Parser() : names(2), ratings(2) {}

        void parse_csa_file(const std::string& path) {
            std::ifstream is(path);
            if (is)
                parse_csa(is);
            else
                throw std::ios_base::failure("No such file");
        }

        void parse_csa_str(const std::string& csa_str) {
            std::istringstream ss(csa_str);
            parse_csa(ss);
        }

    private:
        void parse_csa(std::istream& is) {
            int line_no = 1;

            version = "";
            informations.clear();
            sfen = "";
            endgame = "";
            names[0] = names[1] = "";
            ratings[0] = ratings[1] = 0;
            moves.clear();
            times.clear();
            scores.clear();
            comments.clear();
            comment = "";
            win = Draw;
            StateListPtr states = StateListPtr(new std::deque<StateInfo>(1));
            Position pos;
            bool pos_initialized = false;
            std::vector<std::string> position_lines;
            std::string current_turn_str;
            Color lose_color = ColorNum;
            std::string line;
            while (!is.bad() && !is.eof()) {
                std::getline(is, line);
                if (line[0] == '\0') {
                }
                else if (line[0] == '\'') {
                    // Commnet
                    // rating
                    if (line.substr(0, 12) == "'black_rate:") {
                        auto first = line.find_first_of(":", 12);
                        if (first != std::string::npos) {
                            ratings[0] = std::stof(line.substr(first + 1));
                        }
                    }
                    else if (line.substr(0, 12) == "'white_rate:") {
                        auto first = line.find_first_of(":", 12);
                        if (first != std::string::npos) {
                            ratings[1] = std::stof(line.substr(first + 1));
                        }
                    }
                    // score
                    else if (line.substr(0, 4) == "'** ") {
                        auto last = line.find_first_of(" ", 4);
                        if (last == std::string::npos)
                            last = line.size();
                        try {
                            scores[moves.size() - 1] = std::stoi(line.substr(4, last - 4));
                        }
                        catch (std::invalid_argument&) {}
                        comments[moves.size() - 1] = line.substr(4);
                    }
                    else {
                        comment += line + "\n";
                        // for floodgate
                        if (line.substr(0, 9) == "'summary:") {
                            const auto found_pos = line.find_first_of(':', 9);
                            if (found_pos != std::string::npos) {
                                const auto reason = line.substr(9, found_pos - 9);
                                if (reason == "illegal move") {
                                    endgame = "%ILLEGAL_MOVE";
                                    lose_color = pos.turn();
                                }
                                else if (reason == "max_moves") {
                                    endgame = "%JISHOGI";
                                }
                                else if (reason == "abnormal") {
                                    endgame = "%ERROR";
                                }
                            }
                        }
                    }
                }
                else if (line[0] == 'V') {
                    version = line;
                }
                else if (line[0] == 'N') {
                    auto i = std::find(COLOR_SYMBOLS.begin(), COLOR_SYMBOLS.end(), line[1]);
                    if (i != COLOR_SYMBOLS.end())
                        names[i - COLOR_SYMBOLS.begin()] = line.substr(2);
                }
                else if (line[0] == '$') {
                    informations.emplace_back(line);
                }
                else if (line[0] == 'P') {
                    position_lines.push_back(line);
                }
                else if (line[0] == COLOR_SYMBOLS[0] || line[0] == COLOR_SYMBOLS[1]) {
                    if (line.size() == 1) {
                        current_turn_str = line[0];
                        position_lines.push_back(line);
                    }
                    else {
                        if (!pos_initialized)
                            throw std::domain_error("Board infomation is not defined before a special move");
                        Move move = csaToMove(pos, line.substr(1, 6));
                        moves.push_back(move.value());
                        states->push_back(StateInfo());
                        pos.doMove(move, states->back());
                        times.resize(moves.size());
                        scores.resize(moves.size());
                        // Comment
                        comments.resize(moves.size());
                        if (line.substr(7, 2) == ",\'") {
                            comments[moves.size() - 1] = line.substr(9);
                        }
                    }
                }
                else if (line[0] == 'T') {
                    if (endgame == "")
                        times[moves.size() - 1] = std::stoi(line.substr(1));
                    else
                        times.emplace_back(std::stoi(line.substr(1)));
                }
                else if (line[0] == '%') {
                    // End of the game
                    if (!pos_initialized)
                        throw std::domain_error("Board infomation is not defined before a special move");
                    // Ignore after comma
                    const auto comma_pos = line.find_first_of(',');
                    if (comma_pos != std::string::npos)
                        line = line.substr(0, comma_pos);

                    if (line == "%TORYO" || line == "%TIME_UP" || line == "%ILLEGAL_MOVE")
                        lose_color = pos.turn();
                    else if (line == "%+ILLEGAL_ACTION")
                        lose_color = Black;
                    else if (line == "%-ILLEGAL_ACTION")
                        lose_color = White;
                    else if (line == "%KACHI")
                        lose_color = oppositeColor(pos.turn());

                    endgame = line;

                    // TODO : Support %MATTA etc.
                }
                else if (line == "/")
                    throw std::domain_error("Dont support multiple matches in str");
                else {
                    std::stringstream ss;
                    ss << "Invalid line " << line_no << ": " << line;
                    throw std::domain_error(ss.str());
                }

                if (!pos_initialized && current_turn_str != "") {
                    pos_initialized = true;
                    sfen = parse_position(position_lines);
                    pos.set(sfen);
                }

                line_no += 1;
            }

            if (lose_color == Black)
                win = WhiteWin;
            else if (lose_color == White)
                win = BlackWin;
            else
                win = Draw;
        }

        static std::string parse_position(const std::vector<std::string>& position_block_lines) {
            Color color;
            Color current_turn;
            int rank_index;
            int file_index;
            Piece piece;
            int pieces_in_hand[PieceNone] = {};
            Piece pieces_in_board[9][9];

            // ex.) P1 - KY - KE - GI - KI - OU - KI - GI - KE - KY
            for (auto line : position_block_lines) {
                if (line[0] != 'P') {
                    auto itrColor = std::find(COLOR_SYMBOLS.begin(), COLOR_SYMBOLS.end(), line[0]);
                    if (itrColor != COLOR_SYMBOLS.end()) {
                        color = (Color)(itrColor - COLOR_SYMBOLS.begin());
                        if (line.size() == 1) {
                            // duplicated data
                            current_turn = color;
                        }
                        else {
                            // move
                            throw std::domain_error("TODO: parse moves");
                        }
                    }
                    else {
                        throw std::domain_error("Invalid position line: " + line);
                    }
                }
                else {
                    auto itrColor = std::find(COLOR_SYMBOLS.begin(), COLOR_SYMBOLS.end(), line[1]);
                    if (itrColor != COLOR_SYMBOLS.end()) {
                        int index = 2;
                        while (index < line.size()) {
                            rank_index = std::stoi(line.substr(index, 1));
                            index += 1;
                            file_index = std::stoi(line.substr(index, 1));
                            index += 1;
                            piece = stringToPieceCSA.value((*itrColor) + line.substr(index, 2));
                            index += 2;
                            if (rank_index == 0 && file_index == 0) {
                                // piece in hand
                                pieces_in_hand[piece] += 1;
                            }
                            else {
                                pieces_in_board[(rank_index - 1)][(file_index - 1)] = piece;
                            }
                        }
                    }
                    else if (line[1] >= '1' && line[1] <= '9') {
                        rank_index = line[1] - '1';
                        file_index = 0;
                        for (int index = 2; index < 29; index += 3) {
                            piece = stringToPieceCSA.value(line.substr(index, 3));
                            pieces_in_board[rank_index][file_index] = piece;

                            file_index += 1;
                        }
                    }
                    else if (line[1] == 'I') { // PI
                        const Piece initial_board[9][9] = {
                            { WLance, WKnight, WSilver, WGold, WKing, WGold, WSilver, WKnight, WLance },
                            { Empty, WRook, Empty, Empty, Empty, Empty, Empty, WBishop, Empty },
                            { WPawn, WPawn, WPawn, WPawn, WPawn, WPawn, WPawn, WPawn, WPawn },
                            { Empty, Empty, Empty, Empty, Empty, Empty, Empty, Empty, Empty },
                            { Empty, Empty, Empty, Empty, Empty, Empty, Empty, Empty, Empty },
                            { Empty, Empty, Empty, Empty, Empty, Empty, Empty, Empty, Empty },
                            { BPawn, BPawn, BPawn, BPawn, BPawn, BPawn, BPawn, BPawn, BPawn },
                            { Empty, BBishop, Empty, Empty, Empty, Empty, Empty, BRook, Empty },
                            { BLance, BKnight, BSilver, BGold, BKing, BGold, BSilver, BKnight, BLance },
                        };
                        for (rank_index = 0; rank_index < 9; ++rank_index) {
                            for (file_index = 0; file_index < 9; ++file_index) {
                                pieces_in_board[rank_index][file_index] = initial_board[rank_index][file_index];
                            }
                        }

                        int index = 2;
                        while (index < line.size()) {
                            char file_char = line[index];
                            file_index = '9' - line[index];
                            index += 1;
                            char rank_char = line[index];
                            rank_index = rank_char - '1';
                            index += 1;
                            PieceType piecetype = stringToPieceTypeCSA.value(line.substr(index, 2));
                            index += 2;
                            if (rank_char == '0' && file_char == '0') {
                                // piece in hand
                                throw std::domain_error("TODO: Not implemented komaochi in komadai");
                            }
                            if (rank_index < 0 || rank_index >= 9 || file_index < 0 || file_index >= 9 ||
                                pieces_in_board[rank_index][file_index] == Empty || pieceToPieceType(pieces_in_board[rank_index][file_index]) != piecetype)
                                throw std::domain_error("Invalid piece removing on intializing a board");
                            pieces_in_board[rank_index][file_index] = Empty;
                        }
                    }
                    else {
                        throw std::domain_error("Invalid rank/piece in hand: " + line);
                    }
                }
            }

            return to_sfen(pieces_in_board, pieces_in_hand, current_turn);
        }

        static std::string to_sfen(Piece pieces_in_board[9][9], int pieces_in_hand[PieceNone], Color current_turn, int move_count = 1) {
            std::string sfen;
            int empty = 0;

            // Position part.
            for (int rank = 0; rank < 9; ++rank) {
                for (int file = 0; file < 9; ++file) {
                    Piece piece = pieces_in_board[rank][file];
                    if (piece == Empty)
                        empty += 1;
                    else {
                        if (empty > 0) {
                            sfen.append(std::to_string(empty));
                            empty = 0;
                        }
                        sfen.append(PieceToCharUSITable[(size_t)piece]);
                    }
                }

                if (empty > 0) {
                    sfen.append(std::to_string(empty));
                    empty = 0;
                }

                if (rank != 8) {
                    sfen.append("/");
                }
            }

            sfen.append(" ");

            // Side to move.
            if (current_turn == White)
                sfen.append("w");
            else
                sfen.append("b");

            sfen.append(" ");

            // Pieces in hand
            int pih_len = 0;
            for (Piece p = BPawn; p < PieceNone; ++p) {
                if (p > BDragon && p < WPawn)
                    continue;
                if (pieces_in_hand[p] >= 1) {
                    pih_len += 1;
                    if (pieces_in_hand[p] > 1) {
                        sfen.append(std::to_string(pieces_in_hand[p]));
                    }
                    sfen.append(PieceToCharUSITable[(size_t)p]);
                }
            }
            if (pih_len == 0)
                sfen.append("-");

            sfen.append(" ");

            // Move count
            sfen.append(std::to_string(move_count));

            return sfen;
        }

    };
}
