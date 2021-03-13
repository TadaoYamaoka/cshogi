import cshogi
from datetime import datetime

PGN_SQUARE_NAMES = [
	'i9', 'i8', 'i7', 'i6', 'i5', 'i4', 'i3', 'i2', 'i1',
	'h9', 'h8', 'h7', 'h6', 'h5', 'h4', 'h3', 'h2', 'h1',
	'g9', 'g8', 'g7', 'g6', 'g5', 'g4', 'g3', 'g2', 'g1',
	'f9', 'f8', 'f7', 'f6', 'f5', 'f4', 'f3', 'f2', 'f1',
	'e9', 'e8', 'e7', 'e6', 'e5', 'e4', 'e3', 'e2', 'e1',
	'd9', 'd8', 'd7', 'd6', 'd5', 'd4', 'd3', 'd2', 'd1',
	'c9', 'c8', 'c7', 'c6', 'c5', 'c4', 'c3', 'c2', 'c1',
	'b9', 'b8', 'b7', 'b6', 'b5', 'b4', 'b3', 'b2', 'b1',
	'a9', 'a8', 'a7', 'a6', 'a5', 'a4', 'a3', 'a2', 'a1',
]

PGN_HAND_PIECES = [
    'P', 'L', 'N', 'S', 'G', 'B', 'R',
]

PGN_PIECE_TYPES = [
    None,
    'P', 'L', 'N', 'S', 'B', 'R', 'G', 'K',
    '+P', '+L', '+K', '+S', '+B', '+R',
]

def move_to_san(move):
    move_to = PGN_SQUARE_NAMES[cshogi.move_to(move)]

    if cshogi.move_is_drop(move):
        return PGN_HAND_PIECES[cshogi.move_drop_hand_piece(move)] + '@' + move_to

    move_from = PGN_SQUARE_NAMES[cshogi.move_from(move)]
    promotion = '+' if cshogi.move_is_promotion(move) else ''
    return PGN_PIECE_TYPES[cshogi.move_from_piece_type(move)] + move_from + move_to + promotion

class Exporter:
    def __init__(self, path=None, append=False):
        if path:
            self.open(path)
        else:
            self.f = None

    def open(self, path, append=False):
        self.f = open(path, 'a' if append else 'w', newline='\n')

    def close(self):
        self.f.close()

    def tag_pair(self, names, result, event='?', site='?', starttime=datetime.now(), round=1):
        self.f.write('[Event "' + event +'"]\n')
        self.f.write('[Site "' + site + '"]\n')
        self.f.write('[Date "' + (starttime.strftime('%Y.%m.%d') if starttime else '????.??.??') + '"]\n')
        self.f.write('[Round "' + str(round) + '"]\n')
        self.f.write('[White "' + names[0] + '"]\n')
        self.f.write('[Black "' + names[1] + '"]\n')
        if result == cshogi.BLACK_WIN:
            self.result_str = '1-0'
        elif result == cshogi.WHITE_WIN:
            self.result_str = '0-1'
        elif result == cshogi.DRAW:
            self.result_str = '1/2-1/2'
        else:
            self.result_str = '*'
        self.f.write('[Result "' + self.result_str + '"]\n\n')
        self.f.flush()

    def movetext(self, moves):
        line = ''
        for i, move in enumerate(moves):
            if i % 2 == 0:
                part = str(i // 2 + 1) + '. '
            else:
                part = ''
            part += move_to_san(moves[i])
            if i + 1 == len(moves):
                part += ' ' + self.result_str
            if len(line) + len(part) <= 80:
                if line != '':
                    line += ' '
                line += part
            else:
                self.f.write(line + '\n')
                line = part

        self.f.write(line + '\n\n')
        self.f.flush()
