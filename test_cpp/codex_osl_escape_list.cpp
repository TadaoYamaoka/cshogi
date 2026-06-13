#include "osl/checkmate/dfpn.h"
#include "osl/numEffectState.h"
#include "osl/oslConfig.h"
#include "osl/usi.h"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: codex_osl_escape_list <sfen>\n";
        return 1;
    }

    osl::OslConfig::setUp();
    osl::NumEffectState state;
    osl::usi::parse(std::string("position ") + argv[1], state);

    osl::checkmate::Dfpn::DfpnMoveVector moves;
    if (state.turn() == osl::BLACK) {
        osl::checkmate::Dfpn::generateEscape<osl::WHITE>(state, true, osl::Square(), moves);
    }
    else {
        osl::checkmate::Dfpn::generateEscape<osl::BLACK>(state, true, osl::Square(), moves);
    }

    std::cout << "sfen=" << osl::usi::show(state) << "\n";
    for (std::size_t i = 0; i < moves.size(); ++i) {
        std::cout << i << ' ' << osl::usi::show(moves[i]) << "\n";
    }
    return 0;
}
