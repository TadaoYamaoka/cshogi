import random
import math
from collections import defaultdict

from cshogi import *
from cshogi.usi import Engine
from cshogi import PGN
from cshogi.elo import Elo

try:
    is_jupyter = get_ipython().__class__.__name__ != 'TerminalInteractiveShell'
    if is_jupyter:
        from IPython.display import SVG, display
except NameError:
    is_jupyter = False

def main(engine1, engine2, options1={}, options2={}, names=None, games=1, resign=None, byoyomi=1000, draw=256,
         opening=None, opening_moves=24, opening_seed=None, pgn=None, no_pgn_moves=False, is_display=True, debug=True):
    engine1 = Engine(engine1, connect=False)
    engine2 = Engine(engine2, connect=False)

    # debug
    if debug:
        class Listener:
            def __init__(self, id):
                self.id = id

            def __call__(self, line):
                print(self.id + ':' + line)

        listener1 = Listener('1')
        listener2 = Listener('2')
    else:
        listener1 = None
        listener2 = None

    # PGN
    if pgn:
        pgn_exporter = PGN.Exporter(pgn)

    # 初期局面読み込み
    if opening_seed is not None:
        random.seed(opening_seed)
    if opening:
        opening_list = []
        with open(opening) as f:
            opening_list = [line.strip()[15:].split(' ') for line in f]
        # シャッフル
        random.shuffle(opening_list)

    board = Board()
    engine1_won = [0, 0, 0, 0, 0, 0]
    engine2_won = [0, 0, 0, 0, 0, 0]
    draw_count = 0
    WIN_DRAW = 2
    for n in range(games):
        # 先後入れ替え
        if n % 2 == 0:
            engines_order = (engine1, engine2)
            options_order = (options1, options2)
            listeners_order = (listener1, listener2)
        else:
            engines_order = (engine2, engine1)
            options_order = (options2, options1)
            listeners_order = (listener2, listener1)

        # 接続とエンジン設定
        for engine, options, listener in zip(engines_order, options_order, listeners_order):
            engine.connect(listener=listener)
            for name, value in options.items():
                engine.setoption(name, value)
            engine.isready(listener=listener)

        if names:
            if names[0]: engine1.name = names[0]
            if names[1]: engine2.name = names[1]

        # 初期局設定
        board.reset()
        moves = []
        usi_moves = []
        repetition_hash = defaultdict(int)
        if opening:
            for move_usi in opening_list[n // 2 % len(opening_list)]:
                moves.append(board.push_usi(move_usi))
                usi_moves.append(move_usi)
                repetition_hash[board.zobrist_hash()] += 1
                if board.move_number > opening_moves:
                    break

        # 盤面表示
        if is_display:
            print('開始局面')
            if is_jupyter:
                display(SVG(board.to_svg()))
            else:
                print(board)

        # 新規ゲーム
        for engine, listener in zip(engines_order, listeners_order):
            engine.usinewgame(listener=listener)

        # 対局
        is_game_over = False
        is_nyugyoku = False
        is_illegal = False
        is_repetition_win = False
        is_repetition_lose = False
        is_fourfold_repetition = False
        while not is_game_over:
            for engine, listener in zip(engines_order, listeners_order):
                # 持将棋
                if board.move_number > draw:
                    is_game_over = True
                    break

                # position
                engine.position(usi_moves, listener=listener)

                # go
                bestmove, _ = engine.go(byoyomi=byoyomi, listener=listener)

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
                        board.push(move)
                        moves.append(move)
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

                # 盤面表示
                if is_display:
                    print('{}手目'.format(len(usi_moves)))
                    if is_jupyter:
                        display(SVG(board.to_svg(move)))
                    else:
                        print(board)

                # 終局判定
                if board.is_game_over():
                    is_game_over = True
                    break

        # エンジン終了
        for engine, listener in zip(engines_order, listeners_order):
            engine.quit(listener=listener)

        # 結果出力
        if not board.is_game_over() and board.move_number > draw:
            win = WIN_DRAW
            print('まで{}手で持将棋'.format(board.move_number - 1))
        elif is_fourfold_repetition:
            win = WIN_DRAW
            print('まで{}手で千日手'.format(board.move_number - 1))
        elif is_nyugyoku:
            win = board.turn
            print('まで{}手で入玉宣言'.format(board.move_number - 1))
        elif is_illegal:
            win = opponent(board.turn)
            print('まで{}手で{}の反則負け'.format(board.move_number - 1, '先手' if win == WHITE else '後手'))
        elif is_repetition_win:
            win = board.turn
            print('まで{}手で{}の反則勝ち'.format(board.move_number - 1, '先手' if win == BLACK else '後手'))
        elif is_repetition_lose:
            win = opponent(board.turn)
            print('まで{}手で{}の反則負け'.format(board.move_number - 1, '先手' if win == WHITE else '後手'))
        else:
            win = opponent(board.turn)
            print('まで{}手で{}の勝ち'.format(board.move_number - 1, '先手' if win == BLACK else '後手'))

        # 勝敗カウント
        if win == WIN_DRAW:
            draw_count += 1
            engine1_won[4 + n % 2] += 1
            engine2_won[4 + (n + 1) % 2] += 1
        elif n % 2 == 0 and win == BLACK or n % 2 == 1 and win == WHITE:
            engine1_won[n % 2] += 1
            engine2_won[2 + (n + 1) % 2] += 1
        else:
            engine2_won[(n + 1) % 2] += 1
            engine1_won[2 + n % 2] += 1

        black_won = engine1_won[0] + engine2_won[0]
        white_won = engine1_won[1] + engine2_won[1]
        engine1_won_sum = engine1_won[0] + engine1_won[1]
        engine2_won_sum = engine2_won[0] + engine2_won[1]
        total_count = engine1_won_sum + engine2_won_sum + draw_count

        # 勝敗状況表示
        print('{} of {} games finished.'.format(n + 1, games))
        print('{} vs {}: {}-{}-{} ({:.1f}%)'.format(
            engine1.name, engine2.name, engine1_won_sum, engine2_won_sum, draw_count,
            (engine1_won_sum + draw_count / 2) / total_count * 100))
        print('Black vs White: {}-{}-{} ({:.1f}%)'.format(
            black_won, white_won, draw_count,
            (black_won + draw_count / 2) / total_count * 100))
        print('{} playing Black: {}-{}-{} ({:.1f}%)'.format(
            engine1.name,
            engine1_won[0], engine1_won[2], engine1_won[4],
            (engine1_won[0] + engine1_won[4] / 2) / (engine1_won[0] + engine1_won[2] + engine1_won[4]) * 100))
        print('{} playing White: {}-{}-{} ({:.1f}%)'.format(
            engine1.name,
            engine1_won[1], engine1_won[3], engine1_won[5],
            (engine1_won[1] + engine1_won[5] / 2) / (engine1_won[1] + engine1_won[3] + engine1_won[5]) * 100 if n > 0 else 0))
        print('{} playing Black: {}-{}-{} ({:.1f}%)'.format(
            engine2.name,
            engine2_won[0], engine2_won[2], engine2_won[4],
            (engine2_won[0] + engine2_won[4] / 2) / (engine2_won[0] + engine2_won[2] + engine2_won[4]) * 100 if n > 0 else 0))
        print('{} playing White: {}-{}-{} ({:.1f}%)'.format(
            engine2.name,
            engine2_won[1], engine2_won[3], engine2_won[5],
            (engine2_won[1] + engine2_won[5] / 2) / (engine2_won[1] + engine2_won[3] + engine2_won[5]) * 100))
        elo = Elo(engine1_won_sum, engine2_won_sum, draw_count)
        if engine1_won_sum > 0 and engine2_won_sum > 0:
            try:
                error_margin = elo.error_margin()
            except ValueError:
                error_margin = math.nan
            print('Elo difference: {:.1f} +/- {:.1f}, LOS: {:.1f} %, DrawRatio: {:.1f} %'.format(
                elo.diff(), error_margin, elo.los(), elo.draw_ratio()))

        # PGN
        if pgn:
            if win == BLACK:
                result = BLACK_WIN
            elif win == WHITE:
                result = WHITE_WIN
            else:
                result = DRAW
            pgn_exporter.tag_pair([engine.name for engine in engines_order], result, round=n+1)
            if not no_pgn_moves:
                pgn_exporter.movetext(moves)

    # PGN
    if pgn:
        pgn_exporter.close()

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('engine1')
    parser.add_argument('engine2')
    parser.add_argument('--options1', type=str, default='')
    parser.add_argument('--options2', type=str, default='')
    parser.add_argument('--name1', type=str)
    parser.add_argument('--name2', type=str)
    parser.add_argument('--games', type=int, default=1)
    parser.add_argument('--resign', type=int)
    parser.add_argument('--byoyomi', type=int, default=1000)
    parser.add_argument('--draw', type=int, default=256)
    parser.add_argument('--opening', type=str)
    parser.add_argument('--opening-moves', type=int, default=24)
    parser.add_argument('--opening-seed', type=int)
    parser.add_argument('--pgn', type=str)
    parser.add_argument('--no-pgn-moves', action='store_true')
    parser.add_argument('--display', action='store_true')
    parser.add_argument('--debug', action='store_true')
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

    main(args.engine1, args.engine2,
        options_list[0], options_list[1],
        [args.name1, args.name2],
        args.games, args.resign, args.byoyomi, args.draw,
        args.opening, args.opening_moves, args.opening_seed,
        args.pgn,
        args.no_pgn_moves,
        args.display, args.debug)
