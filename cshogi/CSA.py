import cshogi
from cshogi import Parser

COLOR_SYMBOLS = ['+', '-']

class Exporter:
    def __init__(self, path=None, append=False):
        if path:
            self.open(path, append)
        else:
            self.f = None

    def open(self, path, append=False):
        self.f = open(path, 'a' if append else 'w', newline='\n')

    def close(self):
        self.f.close()

    def info(self, init_board, names=None, var_info=None, comments=None, version=None):
        if self.f.tell() != 0:
            self.f.write('/\n')
        if version:
                self.f.write(version)
                self.f.write('\n')
        if names:
            for name, turn in zip(names, ['+', '-']):
                self.f.write('N' + turn + name)
                self.f.write('\n')
        if comments:
            for comment in comments:
                self.f.write("'")
                self.f.write(comment)
                self.f.write('\n')
        if var_info:
            for k, v in var_info.items():
                self.f.write('$' + k + ':' + v)
                self.f.write('\n')
        csa_pos = init_board.csa_pos()
        if csa_pos == 'P1-KY-KE-GI-KI-OU-KI-GI-KE-KY\nP2 * -HI *  *  *  *  * -KA * \nP3-FU-FU-FU-FU-FU-FU-FU-FU-FU\nP4 *  *  *  *  *  *  *  *  * \nP5 *  *  *  *  *  *  *  *  * \nP6 *  *  *  *  *  *  *  *  * \nP7+FU+FU+FU+FU+FU+FU+FU+FU+FU\nP8 * +KA *  *  *  *  * +HI * \nP9+KY+KE+GI+KI+OU+KI+GI+KE+KY\n+\n':
            self.f.write('PI\n+\n')
        else:
            self.f.write(csa_pos)
        self.turn = init_board.turn

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

    def endgame(self, endgame):
        self.f.write(endgame)
        self.f.write('\n')
        self.f.flush()
