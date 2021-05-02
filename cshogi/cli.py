import random
import math
from time import perf_counter
import re
import os
import datetime
from collections import defaultdict

from cshogi import *
from cshogi.usi import Engine
from cshogi import CSA
from cshogi import PGN
from cshogi.elo import Elo

try:
    is_jupyter = get_ipython().__class__.__name__ != 'TerminalInteractiveShell'
    if is_jupyter:
        from IPython.display import SVG, display
except NameError:
    is_jupyter = False

re_usi_info = re.compile('^.*score (cp|mate) ([+\-0-9]+).*pv (.*)$')
def to_score(m):
    if m[1] == 'cp':
        score = int(m[2])
    elif m[1] == 'mate':
        if m[2][0] == '-':
            score = -100000
        else:
            score = 100000
    return score

def usi_info_to_csa_comment(board, info):
    m = re_usi_info.match(info)
    if m is None:
        return None

    # score
    score = to_score(m) * (1 - board.turn * 2)

    # pv
    pv = []
    board2 = board.copy()
    for usi_move in m[3].split(' '):
        move = board2.move_from_usi(usi_move)
        assert board2.is_legal(move), move
        pv.append(CSA.COLOR_SYMBOLS[board2.turn] + move_to_csa(move))
        board2.push(move)

    return f"** {score} {' '.join(pv)}"

def usi_info_to_score(info):
    m = re_usi_info.match(info)
    if m is None:
        return None

    return to_score(m)

def main(engine1, engine2, options1={}, options2={}, names=None, games=1, resign=None, mate_win=False,
         byoyomi=None, time=None, inc=None,
         draw=256, opening=None, opening_moves=24, opening_seed=None, opening_index=None,
         keep_process=False,
         csa=None, multi_csa=False, pgn=None, no_pgn_moves=False, is_display=True, debug=True,
         print_summary=True, callback=None):
    engine1 = Engine(engine1, connect=False)
    engine2 = Engine(engine2, connect=False)

    # byoyomi
    if type(byoyomi) in (list, tuple):
        if len(byoyomi) >= 2:
            byoyomi1, byoyomi2 = byoyomi
        else:
            byoyomi1 = byoyomi2 = byoyomi[0]
    else:
        byoyomi1 = byoyomi2 = byoyomi

    # time
    if type(time) in (list, tuple):
        if len(time) >= 2:
            time1, time2 = time
        else:
            time1 = time2 = time[0]
    else:
        time1 = time2 = time

    # inc
    if type(inc) in (list, tuple):
        if len(inc) >= 2:
            inc1, inc2 = inc
        else:
            inc1 = inc2 = inc[0]
    else:
        inc1 = inc2 = inc

    # debug
    if debug:
        class Listener:
            def __init__(self, id):
                self.id = id
                self.info = self.bestmove = ''

            def __call__(self, line):
                print(self.id + ':' + line)
                self.info = self.bestmove
                self.bestmove = line

        listener1 = Listener('1')
        listener2 = Listener('2')
    else:
        class Listener:
            def __init__(self):
                self.info = self.bestmove = ''

            def __call__(self, line):
                self.info = self.bestmove
                self.bestmove = line
        listener1 = listener2 = Listener()

    # CSA
    if csa and multi_csa:
        csa_exporter = CSA.Exporter(csa, append=True)

    # PGN
    if pgn:
        pgn_exporter = PGN.Exporter(pgn, append=True)

    # 初期局面読み込み
    if opening:
        opening_list = []
        with open(opening) as f:
            opening_list = [line.strip()[15:].split(' ') for line in f]
        # インデックス指定
        if opening_index is not None:
            opening_list = [opening_list[opening_index]]
        else:
            # シャッフル
            if opening_seed is not None:
                random.seed(opening_seed)
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
            byoyomi_order = (byoyomi1, byoyomi2)
            btime = time1
            wtime = time2
            binc = inc1
            winc = inc2
        else:
            engines_order = (engine2, engine1)
            options_order = (options2, options1)
            listeners_order = (listener2, listener1)
            byoyomi_order = (byoyomi2, byoyomi1)
            btime = time2
            wtime = time1
            binc = inc2
            winc = inc1

        # 接続とエンジン設定
        for engine, options, listener in zip(engines_order, options_order, listeners_order):
            if engine.proc is None:
                engine.connect(listener=listener)
            for name, value in options.items():
                engine.setoption(name, value, listener=listener)
            engine.isready(listener=listener)

        if names:
            if names[0]: engine1.name = names[0]
            if names[1]: engine2.name = names[1]

        print('{} vs {} start.'.format(engines_order[0].name, engines_order[1].name))

        # 初期局設定
        board.reset()
        moves = []
        usi_moves = []
        repetition_hash = defaultdict(int)
        if csa:
            engine_names = [engine.name for engine in engines_order]
            if not multi_csa:
                csa_exporter = CSA.Exporter(os.path.join(csa, '+'.join(engine_names) + '+' + datetime.datetime.now().strftime('%Y%m%d%H%M%S') + '.csa'), append=False)
            csa_exporter.info(board, engine_names, version='V2')
        if opening:
            for move_usi in opening_list[n // 2 % len(opening_list)]:
                move = board.push_usi(move_usi)
                if csa:
                    csa_exporter.move(move)
                moves.append(move)
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
        is_timeup = False
        remain_time = [btime, wtime]
        inc_time = (binc, winc)
        while not is_game_over:
            for engine, listener, byoyomi in zip(engines_order, listeners_order, byoyomi_order):
                # 持将棋
                if board.move_number > draw:
                    is_game_over = True
                    break

                # position
                engine.position(usi_moves, listener=listener)

                start_time = perf_counter()

                # go
                bestmove, _ = engine.go(byoyomi=byoyomi, btime=remain_time[BLACK], wtime=remain_time[WHITE], binc=binc, winc=winc, listener=listener)

                elapsed_time = perf_counter() - start_time

                if remain_time[board.turn] is not None:
                    if inc_time[board.turn] is not None:
                        remain_time[board.turn] += inc_time[board.turn]
                    remain_time[board.turn] -= math.ceil(elapsed_time * 1000)

                    if remain_time[board.turn] < 0:
                        # 1秒未満は切れ負けにしない
                        if remain_time[board.turn] > -1000:
                            remain_time[board.turn] = 0
                        else:
                            # 時間切れ負け
                            is_timeup = True
                            is_game_over = True
                            break

                score = usi_info_to_score(listener.info)
                # 投了閾値
                if resign is not None:
                    if score is not None and score <= -resign:
                        # 投了
                        is_game_over = True
                        break

                # 詰みを見つけたら終了
                if mate_win:
                    if score is not None and score == 100000:
                        move = board.move_from_usi(bestmove)
                        if csa:
                            csa_exporter.move(move, time=int(elapsed_time), comment=usi_info_to_csa_comment(board, listener.info))
                        board.push(move)
                        is_game_over = True
                        break

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
                        if csa:
                            csa_exporter.move(move, time=int(elapsed_time), comment=usi_info_to_csa_comment(board, listener.info))
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
        if not keep_process:
            for engine, listener in zip(engines_order, listeners_order):
                engine.quit(listener=listener)

        # 結果出力
        if not board.is_game_over() and board.move_number > draw:
            win = WIN_DRAW
            print('まで{}手で持将棋'.format(board.move_number - 1))
            csa_endgame = '%JISHOGI'
        elif is_fourfold_repetition:
            win = WIN_DRAW
            print('まで{}手で千日手'.format(board.move_number - 1))
            csa_endgame = '%SENNICHITE'
        elif is_nyugyoku:
            win = board.turn
            print('まで{}手で入玉宣言'.format(board.move_number - 1))
            csa_endgame = '%KACHI'
        elif is_illegal:
            win = opponent(board.turn)
            print('まで{}手で{}の反則負け'.format(board.move_number - 1, '先手' if win == WHITE else '後手'))
            csa_endgame = '%ILLEGAL_MOVE'
        elif is_repetition_win:
            win = board.turn
            print('まで{}手で{}の反則勝ち'.format(board.move_number - 1, '先手' if win == BLACK else '後手'))
            csa_endgame = '%+ILLEGAL_ACTION' if board.turn == WHITE else '%-ILLEGAL_ACTION'
        elif is_repetition_lose:
            win = opponent(board.turn)
            print('まで{}手で{}の反則負け'.format(board.move_number - 1, '先手' if win == WHITE else '後手'))
            csa_endgame = 'ILLEGAL_MOVE'
        elif is_timeup:
            win = opponent(board.turn)
            print('まで{}手で{}の切れ負け'.format(board.move_number - 1, '先手' if win == WHITE else '後手'))
            csa_endgame = '%TIME_UP'
        else:
            win = opponent(board.turn)
            print('まで{}手で{}の勝ち'.format(board.move_number - 1, '先手' if win == BLACK else '後手'))
            csa_endgame = '%TORYO'

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
        if print_summary:
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

        # CSA
        if csa:
            csa_exporter.endgame(csa_endgame)

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

        if callback:
            is_continue = callback({
                'engine1_name': engine1.name,
                'engine2_name': engine2.name,
                'engine1_won': engine1_won_sum,
                'engine2_won': engine2_won_sum,
                'black_won': black_won,
                'white_won': white_won,
                'draw': draw_count,
                'total': total_count,
                })
            if not is_continue:
                break

    # CSA
    if csa:
        csa_exporter.close()

    # PGN
    if pgn:
        pgn_exporter.close()

    # エンジン終了
    if keep_process:
        for engine, listener in zip(engines_order, listeners_order):
            engine.quit(listener=listener)

    return {
        'engine1_name': engine1.name,
        'engine2_name': engine2.name,
        'engine1_won': engine1_won_sum,
        'engine2_won': engine2_won_sum,
        'black_won': black_won,
        'white_won': white_won,
        'draw': draw_count,
        'total': total_count,
        }

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('engine1')
    parser.add_argument('engine2')
    parser.add_argument('engine3', nargs='?')
    parser.add_argument('--options1', type=str, default='')
    parser.add_argument('--options2', type=str, default='')
    parser.add_argument('--options3', type=str, default='')
    parser.add_argument('--name1', type=str)
    parser.add_argument('--name2', type=str)
    parser.add_argument('--name3', type=str)
    parser.add_argument('--games', type=int, default=1)
    parser.add_argument('--resign', type=int)
    parser.add_argument('--mate_win', action='store_true')
    parser.add_argument('--byoyomi', type=int, nargs='+')
    parser.add_argument('--time', type=int, nargs='+')
    parser.add_argument('--inc', type=int, nargs='+')
    parser.add_argument('--draw', type=int, default=256)
    parser.add_argument('--opening', type=str)
    parser.add_argument('--opening-moves', type=int, default=24)
    parser.add_argument('--opening-seed', type=int)
    parser.add_argument('--opening-index', type=int)
    parser.add_argument('--keep_process', action='store_true')
    parser.add_argument('--csa', type=str)
    parser.add_argument('--multi_csa', action='store_true')
    parser.add_argument('--pgn', type=str)
    parser.add_argument('--no-pgn-moves', action='store_true')
    parser.add_argument('--display', action='store_true')
    parser.add_argument('--debug', action='store_true')
    args = parser.parse_args()

    if args.csa is not None and not args.multi_csa:
        os.makedirs(args.csa, exist_ok=True)

    options_list = [{}, {}, {}]
    for i, kvs in enumerate([options.split(',') for options in (args.options1, args.options2, args.options3)]):
        if len(kvs) == 1 and kvs[0] == '':
            continue
        for kv_str in kvs:
            kv = kv_str.split(':', 1)
            if len(kv) != 2:
                raise ValueError('options{}'.format(i + 1))
            options_list[i][kv[0]] = kv[1]

    if args.engine3 is None:
        # 1 on 1 matches
        main(args.engine1, args.engine2,
            options_list[0], options_list[1],
            [args.name1, args.name2],
            args.games, args.resign, args.mate_win,
            args.byoyomi, args.time, args.inc,
            args.draw, args.opening, args.opening_moves, args.opening_seed, args.opening_index,
            args.keep_process,
            args.csa, args.multi_csa,
            args.pgn, args.no_pgn_moves,
            args.display, args.debug)
    else:
        # league matches
        engines = (args.engine1, args.engine2, args.engine3)
        names = (args.name1, args.name2, args.name3)
        if args.byoyomi:
            if len(args.byoyomi) == 3:
                byoyomis = (args.byoyomi[0], args.byoyomi[1], args.byoyomi[2])
            else:
                byoyomis = (args.byoyomi[0], args.byoyomi[0], args.byoyomi[0])
        else:
            byoyomis = (None, None, None)
        if args.time:
            if len(args.time) == 3:
                times = (args.time[0], args.time[1], args.time[2])
            else:
                times = (args.time[0], args.time[0], args.time[0])
        else:
            times = (None, None, None)
        if args.inc:
            if len(args.inc) == 3:
                incs = (args.inc[0], args.inc[1], args.inc[2])
            else:
                incs = (args.inc[0], args.inc[0], args.inc[0])
        else:
            incs = (None, None, None)
        combinations = ((0, 1), (0, 2), (1, 2))
        results = [
            { 'engine1_won': 0, 'engine2_won': 0, 'draw': 0, 'total': 0 },
            { 'engine1_won': 0, 'engine2_won': 0, 'draw': 0, 'total': 0 },
            { 'engine1_won': 0, 'engine2_won': 0, 'draw': 0, 'total': 0 }]

        # 初期局面
        if args.opening:
            # インデックス指定
            if args.opening_index is not None:
                opening_index_list = [args.opening_index]
            else:
                with open(args.opening) as f:
                    opening_index_list = list(range(len(f.readlines())))
                # シャッフル
                if args.opening_seed is not None:
                    random.seed(args.opening_seed)
                random.shuffle(opening_index_list)

        for n in range(0, args.games, 2):
            if args.opening:
                opening_index = opening_index_list[n // 2 % len(opening_index_list)]
            else:
                opening_index = None
            for i, (a, b) in enumerate(combinations):
                # 先後入れ替えて1回ずつ対局
                result = main(engines[a], engines[b],
                    options_list[a], options_list[b],
                    [names[a], names[b]],
                    2, args.resign, args.mate_win,
                    (byoyomis[a], byoyomis[b]), (times[a], times[b]), (incs[a], incs[b]),
                    args.draw, args.opening, args.opening_moves, None, opening_index,
                    args.keep_process,
                    args.csa, args.multi_csa,
                    args.pgn, args.no_pgn_moves,
                    args.display, args.debug,
                    print_summary=False)
                results[i]['engine1_name'] = result['engine1_name']
                results[i]['engine2_name'] = result['engine2_name']
                results[i]['engine1_won'] += result['engine1_won']
                results[i]['engine2_won'] += result['engine2_won']
                results[i]['draw'] += result['draw']
                results[i]['total'] += result['total']

            # 勝敗状況表示
            print('{} of {} games finished.'.format(n + 2, args.games))
            for i, (a, b) in enumerate(combinations):
                print('{} vs {}: {}-{}-{} ({:.1f}%)'.format(
                    results[i]['engine1_name'], results[i]['engine2_name'],
                    results[i]['engine1_won'], results[i]['engine2_won'], results[i]['draw'],
                    (results[i]['engine1_won'] + results[i]['draw'] / 2) / results[i]['total'] * 100))
