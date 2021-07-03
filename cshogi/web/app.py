import argparse
import sys
from cshogi import Board, CSA, KIF, move_from_csa
from flask import Flask, render_template, Markup, request
from wsgiref.simple_server import make_server

def run(*argv):
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=8000)
    parser.add_argument('--csa')
    args = parser.parse_args(argv)

    scale = 1.5
    turn = ('▲', '△')
    names = ['', '']
    moves = []

    if args.csa:
        kif = CSA.Parser.parse_file(args.csa)[0]
        names = kif.names
        csa_turn = {'+': '▲', '-': '△'}
        for i, (move, prev_move, time, comment) in enumerate(zip(kif.moves, [None] + kif.moves[:-1], kif.times, kif.comments)):
            comment_items = comment.split(' ')
            eval = 'null'
            ahead = ''
            if len(comment_items) > 0 and comment_items[0] != '':
                eval = int(comment_items[0])
            if len(comment_items) > 1:
                for csa in comment_items[1:]:
                    ahead += csa_turn[csa[0]] + KIF.move_to_kif(move_from_csa(csa[1:]))
            moves.append({
                'number': i + 1,
                'kif_move': turn[i % 2] + KIF.move_to_kif(move, prev_move),
                'time': time,
                'move': move,
                'eval': eval,
                'ahead': ahead,
                })
        moves.append({
            'number': i + 2,
            'kif_move': turn[(i + 1) % 2] + CSA.JAPANESE_END_GAMES[kif.endgame],
            'time': 0,
            'move': 'null',
            'eval': 'null',
            'ahead': '',
        })

    app = Flask(__name__)

    @app.route("/")
    def init_board():
        return render_template('board.html', names=names, moves=moves)

    server = make_server('localhost', args.port, app)
    server.serve_forever()

if __name__ == '__main__':
    run(*sys.argv[1:])
