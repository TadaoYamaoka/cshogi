import cshogi
from cshogi import Parser

COLOR_SYMBOLS = ['+', '-']
JAPANESE_END_GAMES = {
    '%TORYO': '投了',
    '%CHUDAN': '中断',
    '%SENNICHITE': '千日手',
    '%TIME_UP': '切れ負け',
    '%ILLEGAL_MOVE': '反則負け',
    '%JISHOGI': '持将棋',
    '%KACHI': '入力宣言',
}

class Exporter:
    def __init__(self, path=None, append=False, encoding=None):
        if path:
            self.open(path, append, encoding=encoding)
        else:
            self.f = None

    def open(self, path, append=False, encoding=None):
        self.f = open(path, 'a' if append else 'w', newline='\n', encoding=encoding)

    def close(self):
        self.f.close()

    def info(self, init_board=None, names=None, var_info=None, comment=None, version=None):
        if self.f.tell() != 0:
            self.f.write('/\n')
        if version:
                self.f.write(version)
                self.f.write('\n')
        if names:
            for name, turn in zip(names, ['+', '-']):
                self.f.write('N' + turn + name)
                self.f.write('\n')
        if comment:
            if comment[0] == "'":
                self.f.write(comment)
            else:
                self.f.write("'")
                self.f.write(comment)
                self.f.write('\n')
        if var_info:
            for k, v in var_info.items():
                self.f.write('$' + k + ':' + v)
                self.f.write('\n')
        if init_board:
            csa_pos = init_board.csa_pos()
            if csa_pos == 'P1-KY-KE-GI-KI-OU-KI-GI-KE-KY\nP2 * -HI *  *  *  *  * -KA * \nP3-FU-FU-FU-FU-FU-FU-FU-FU-FU\nP4 *  *  *  *  *  *  *  *  * \nP5 *  *  *  *  *  *  *  *  * \nP6 *  *  *  *  *  *  *  *  * \nP7+FU+FU+FU+FU+FU+FU+FU+FU+FU\nP8 * +KA *  *  *  *  * +HI * \nP9+KY+KE+GI+KI+OU+KI+GI+KE+KY\n+\n':
                self.f.write('PI\n+\n')
            else:
                self.f.write(csa_pos)
            self.turn = init_board.turn
        else:
            self.f.write('PI\n+\n')
            self.turn = cshogi.BLACK

    def move(self, move, time=None, comment=None, sep='\n'):
        self.f.write(COLOR_SYMBOLS[self.turn])
        self.f.write(cshogi.move_to_csa(move))
        if time is not None:
            self.f.write(sep)
            self.f.write('T' + str(time))
        if comment:
            self.f.write(sep)
            self.f.write("'" + comment)
        self.f.write('\n')
        self.turn = cshogi.opponent(self.turn)

    def endgame(self, endgame, time=None):
        self.f.write(endgame)
        self.f.write('\n')
        if time is not None:
            self.f.write('T' + str(time))
            self.f.write('\n')
        self.f.flush()
