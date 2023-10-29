#include "pch.h"

#include "../src/parser.h"

using namespace parser;

TEST(TestCsaParser, parse_position_issue38) {
    // 持ち駒のある初期局面
    initTable();

    __Parser parser;
    parser.parse_csa_str(R"(V2.2
N+A
N-B
P1-KY-KE-GI-KI-OU-KI-GI-KE-KY
P2 *  *  *  *  *  *  * -KA * 
P3-FU-FU-FU-FU-FU-FU-FU-FU-FU
P4 *  *  *  *  *  *  *  *  * 
P5 *  *  *  *  *  *  *  *  * 
P6 *  *  *  *  *  *  *  *  * 
P7+FU+FU+FU+FU+FU+FU+FU+FU+FU
P8 * +KA *  *  *  *  *  *  * 
P9+KY+KE+GI+KI+OU+KI+GI+KE+KY
P+00HI
P-00HI
+
+7776FU,T3
-3334FU,T2
%TORYO,T1
)");
    EXPECT_EQ("lnsgkgsnl/7b1/ppppppppp/9/9/9/PPPPPPPPP/1B7/LNSGKGSNL b Rr 1", parser.sfen);
    EXPECT_EQ("7g7f", Move(parser.moves[0]).toUSI());
    EXPECT_EQ("3c3d", Move(parser.moves[1]).toUSI());
}
