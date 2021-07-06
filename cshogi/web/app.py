import sys
import math
from cshogi import Board, CSA, KIF, move_from_csa, BLACK, WHITE, opponent
from cshogi.usi import Engine
from cshogi.cli import usi_info_to_score, re_usi_info
from flask import Flask, render_template, Markup, request
from wsgiref.simple_server import make_server

TURN_SYMBOLS = ('▲', '△')
CSA_TURN_SYMBOLS = {'+': '▲', '-': '△'}

def usi_info_to_pv(board, info):
    m = re_usi_info.match(info)
    if m is None:
        return None

    # pv
    pv = []
    board2 = board.copy()
    for usi_move in m[3].split(' '):
        move = board2.move_from_usi(usi_move)
        if not board2.is_legal(move):
            break
        pv.append(TURN_SYMBOLS[board2.turn] + KIF.move_to_kif(move, board2.peek()))
        board2.push(move)

    return ' '.join(pv)

def match(moves, engine1=None, engine2=None, options1={}, options2={}, names=None, byoyomi=None, time=None, inc=None, draw=256):
    from collections import defaultdict
    from time import perf_counter

    class Listener:
        def __init__(self):
            self.info = self.bestmove = ''

        def __call__(self, line):
            self.info = self.bestmove
            self.bestmove = line
    listener = Listener()

    engines = [Engine(engine, connect=True) for engine in (engine1, engine2)]
    for engine, options in zip(engines, (options1, options2)):
        for name, value in options.items():
            engine.setoption(name, value, listener=listener)
        engine.isready(listener=listener)

    for i in range(2):
        if names[i]:
            engine[i].name = names[i]
        else:
            names[i] = engines[i].name

    board = Board()
    usi_moves = []
    repetition_hash = defaultdict(int)

    # 新規ゲーム
    for engine in engines:
        engine.usinewgame()

    # 対局
    is_game_over = False
    is_nyugyoku = False
    is_illegal = False
    is_repetition_win = False
    is_repetition_lose = False
    is_fourfold_repetition = False
    is_timeup = False
    remain_time = [time, time]
    while not is_game_over:
        engine_index = (board.move_number - 1) % 2
        engine = engines[engine_index]

        # 持将棋
        if board.move_number > draw:
            is_game_over = True
            break

        # position
        engine.position(usi_moves)

        start_time = perf_counter()

        # go
        bestmove, _ = engine.go(byoyomi=byoyomi, btime=remain_time[BLACK], wtime=remain_time[WHITE], binc=inc, winc=inc, listener=listener)

        elapsed_time = perf_counter() - start_time

        if remain_time[board.turn] is not None:
            if inc_time[board.turn] is not None:
                remain_time[board.turn] += inc_time[board.turn]
            remain_time[board.turn] -= math.ceil(elapsed_time * 1000)

        score = usi_info_to_score(listener.info)

        if bestmove == 'resign':
            # 投了
            is_game_over = True
            break
        elif bestmove == 'win':
            # 入玉勝ち宣言
            is_nyugyoku = True
            is_game_over = True
            break
        else:
            move = board.move_from_usi(bestmove)
            if board.is_legal(move):
                moves.append({
                    'number': board.move_number,
                    'kif_move': TURN_SYMBOLS[(board.move_number + 1) % 2] + KIF.move_to_kif(move, board.peek()),
                    'time': math.ceil(elapsed_time),
                    'move': move,
                    'eval': score * (1 if board.turn == BLACK else -1),
                    'pv': usi_info_to_pv(board, listener.info),
                })
                board.push(move)
                usi_moves.append(bestmove)
                key = board.zobrist_hash()
                repetition_hash[key] += 1
                # 千日手
                if repetition_hash[key] == 4:
                    # 連続王手
                    is_draw = board.is_draw()
                    if is_draw == REPETITION_WIN:
                        is_repetition_win = True
                        is_game_over = True
                        break
                    elif is_draw == REPETITION_LOSE:
                        is_repetition_lose = True
                        is_game_over = True
                        break
                    is_fourfold_repetition = True
                    is_game_over = True
                    break
            else:
                is_illegal = True
                is_game_over = True
                break

        # 終局判定
        if board.is_game_over():
            is_game_over = True
            break

    # エンジン終了
    for engine in engines:
        engine.quit()

    # 結果出力
    if not board.is_game_over() and board.move_number > draw:
        result = '持将棋'
    elif is_fourfold_repetition:
        result = '千日手'
    elif is_nyugyoku:
        result = TURN_SYMBOLS[board.turn] + '入玉宣言'
    elif is_illegal:
        win = opponent(board.turn)
        result = '{}の反則負け'.format('先手' if win == WHITE else '後手')
    elif is_repetition_win:
        win = board.turn
        result = '{}の反則勝ち'.format('先手' if win == BLACK else '後手')
    elif is_repetition_lose:
        win = opponent(board.turn)
        result = '{}の反則負け'.format('先手' if win == WHITE else '後手')
    elif is_timeup:
        win = opponent(board.turn)
        result = '{}の切れ負け'.format('先手' if win == WHITE else '後手')
    else:
        result = TURN_SYMBOLS[board.turn] + '投了'

    moves.append({
        'number': board.move_number,
        'kif_move': result,
        'time': 0,
        'move': 'null',
        'eval': 'null',
        'pv': '',
    })

def run(engine1=None, engine2=None, options1={}, options2={}, names=None, byoyomi=None, time=None, inc=None, draw=256, csa=None, port=8000):
    scale = 1.5

    if engine1 and engine2:
        from multiprocessing import Process, Manager
        manager = Manager()
        moves = manager.list()
        if names is None:
            names = manager.list([None, None])
        else:
            names = manager.list(names)
        match_proc = Process(target=match, args=[moves, engine1, engine2, options1, options2, names, byoyomi, time, inc, draw])
        match_proc.start()
    elif csa:
        moves = []
        kif = CSA.Parser.parse_file(csa)[0]
        names = kif.names
        for i, (move, prev_move, time, comment) in enumerate(zip(kif.moves, [None] + kif.moves[:-1], kif.times, kif.comments)):
            comment_items = comment.split(' ')
            eval = 'null'
            pv = ''
            if len(comment_items) > 0 and comment_items[0] != '':
                eval = int(comment_items[0])
            if len(comment_items) > 1:
                for csa in comment_items[1:]:
                    pv += CSA_TURN_SYMBOLS[csa[0]] + KIF.move_to_kif(move_from_csa(csa[1:]))
            moves.append({
                'number': i + 1,
                'kif_move': TURN_SYMBOLS[i % 2] + KIF.move_to_kif(move, prev_move),
                'time': time,
                'move': move,
                'eval': eval,
                'pv': pv,
                })
        moves.append({
            'number': i + 2,
            'kif_move': TURN_SYMBOLS[(i + 1) % 2] + CSA.JAPANESE_END_GAMES[kif.endgame],
            'time': 0,
            'move': 'null',
            'eval': 'null',
            'pv': '',
        })

    app = Flask(__name__)

    @app.route("/")
    def init_board():
        return render_template('board.html', names=names, moves=moves)

    server = make_server('localhost', port, app)
    server.serve_forever()

def colab(csa):
    from multiprocessing import Process
    import portpicker
    from google.colab import output

    global proc
    if 'proc' in globals():
        proc.terminate()
        proc.join()

    port = portpicker.pick_unused_port()
    proc = Process(target=run, args=(csa, port))
    proc.start()
    output.serve_kernel_port_as_iframe(port, height='680')

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--engine1')
    parser.add_argument('--engine2')
    parser.add_argument('--options1', default='')
    parser.add_argument('--options2', default='')
    parser.add_argument('--name1')
    parser.add_argument('--name2')
    parser.add_argument('--byoyomi', type=int)
    parser.add_argument('--time', type=int)
    parser.add_argument('--inc', type=int)
    parser.add_argument('--draw', type=int, default=256)
    parser.add_argument('--csa')
    parser.add_argument('--port', type=int, default=8000)
    args = parser.parse_args()

    options_list = [{}, {}]
    for i, kvs in enumerate([options.split(',') for options in (args.options1, args.options2)]):
        if len(kvs) == 1 and kvs[0] == '':
            continue
        for kv_str in kvs:
            kv = kv_str.split(':', 1)
            if len(kv) != 2:
                raise ValueError('options{}'.format(i + 1))
            options_list[i][kv[0]] = kv[1]

    run(engine1=args.engine1, engine2=args.engine2,
        options1=options_list[0], options2=options_list[1],
        names=[args.name1, args.name2],
        byoyomi=args.byoyomi, time=args.time, inc=args.inc, draw=args.draw,
        csa=args.csa, port=args.port)
