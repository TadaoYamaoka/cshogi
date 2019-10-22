import random
from collections import defaultdict

from cshogi import *
from cshogi.usi import Engine

try:
    is_jupyter = get_ipython().__class__.__name__ != 'TerminalInteractiveShell'
    if is_jupyter:
        from IPython.display import SVG, display
except NameError:
    is_jupyter = False

def main(engine1, engine2, options1={}, options2={}, games=1, resign=None, byoyomi=1000, draw=256, opening=None):
    engine1 = Engine(engine1, connect=False)
    engine2 = Engine(engine2, connect=False)

    # 初期局面読み込み
    if opening:
        opening_moves_list = []
        with open(opening) as f:
            opening_moves_list = [line.strip()[15:].split(' ') for line in f]

    board = Board()
    engine1_won = [0, 0]
    engine2_won = [0, 0]
    draw_count = 0
    WIN_DRAW = 2
    for n in range(games):
        # 先後入れ替え
        if n % 2 == 0:
            engines = (engine1, engine2)
        else:
            engines = (engine2, engine1)

        # 接続とエンジン設定
        for engine, options in zip((engine1, engine2), (options1, options2)):
            engine.connect()
            for name, value in options.items():
                engine.setoption(name, value)
            engine.isready()

        # 初期局設定
        board.reset()
        moves = []
        repetition_hash = defaultdict(int)
        if opening:
            if n % 2 == 0:
                opening_moves = random.choice(opening_moves_list)
            for move_usi in opening_moves:
                board.push_usi(move_usi)
                moves.append(move_usi)
                repetition_hash[board.zobrist_hash()] += 1

        # 盤面表示
        print('開始局面')
        if is_jupyter:
            display(SVG(board.to_svg()))
        else:
            print(board)

        # 新規ゲーム
        for engine in engines:
            engine.usinewgame()

        # 対局
        is_game_over = False
        is_nyugyoku = False
        is_illegal = False
        is_repetition_lose = False
        is_fourfold_repetition = False
        while not is_game_over:
            for engine in engines:
                # 持将棋
                if board.move_number > draw:
                    is_game_over = True
                    break

                # position
                engine.position(moves)

                # go
                bestmove, _ = engine.go(byoyomi=byoyomi)

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
                        moves.append(bestmove)
                        key = board.zobrist_hash()
                        repetition_hash[key] += 1
                        # 千日手
                        if repetition_hash[key] == 4:
                            # 連続王手
                            is_draw = board.is_draw()
                            if is_draw == REPETITION_WIN:
                                is_illegal = True
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
                print('{}手目'.format(len(moves)))
                if is_jupyter:
                    display(SVG(board.to_svg(move)))
                else:
                    print(board)

                # 終局判定
                if board.is_game_over():
                    is_game_over = True
                    break

        # 結果出力
        if not board.is_game_over() and board.move_number > draw:
            win = WIN_DRAW
            print('まで{}手で持将棋'.format(board.move_number))
        elif is_fourfold_repetition:
            win = WIN_DRAW
            print('まで{}手で千日手'.format(board.move_number - 2))
        elif is_nyugyoku:
            win = board.turn
            print('まで{}手で入玉宣言'.format(board.move_number - 1))
        elif is_illegal:
            win = opponent(board.turn)
            print('まで{}手で{}の反則負け'.format(board.move_number - 1, '先手' if win == BLACK else '後手'))
        elif is_repetition_lose:
            win = board.turn
            print('まで{}手で{}の反則負け'.format(board.move_number - 1, '先手' if win == BLACK else '後手'))
        else:
            win = opponent(board.turn)
            print('まで{}手で{}の勝ち'.format(board.move_number - 1, '先手' if win == BLACK else '後手'))

        # 勝敗カウント
        if win == WIN_DRAW:
            draw_count += 1
        elif n % 2 == 0 and win == BLACK or n % 2 == 1 and win == WHITE:
            engine1_won[n % 2] += 1
        else:
            engine2_won[(n + 1) % 2] += 1

        # エンジン終了
        for engine in engines:
            engine.quit()

    # 勝率表示
    no_draw = games - draw_count
    black_won = engine1_won[0] + engine2_won[0]
    white_won = engine1_won[1] + engine2_won[1]

    print('')
    print('対局数{} 先手勝ち{}({:.0f}%) 後手勝ち{}({:.0f}%) 引き分け{}'.format(
        games,
        black_won,
        black_won / no_draw * 100 if no_draw else 0,
        white_won,
        white_won / no_draw * 100 if no_draw else 0, draw_count))
    print('')
    print(engine1.name)
    print('勝ち{}({:.0f}%) 先手勝ち{}({:.0f}%) 後手勝ち{}({:.0f}%)'.format(
        sum(engine1_won),
        sum(engine1_won) / no_draw * 100 if no_draw else 0,
        engine1_won[0],
        engine1_won[0] / no_draw * 100 if no_draw else 0,
        engine1_won[1],
        engine1_won[1] / no_draw * 100 if no_draw else 0))
    print('')
    print(engine2.name)
    print('勝ち{}({:.0f}%) 先手勝ち{}({:.0f}%) 後手勝ち{}({:.0f}%)'.format(
        sum(engine2_won),
        sum(engine2_won) / no_draw * 100 if no_draw else 0,
        engine2_won[0],
        engine2_won[0] / no_draw * 100 if no_draw else 0,
        engine2_won[1],
        engine2_won[1] / no_draw * 100 if no_draw else 0))

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('engine1')
    parser.add_argument('engine2')
    parser.add_argument('--options1', type=str, default='')
    parser.add_argument('--options2', type=str, default='')
    parser.add_argument('--games', type=int, default=1)
    parser.add_argument('--resign', type=int)
    parser.add_argument('--byoyomi', type=int, default=1000)
    parser.add_argument('--draw', type=int, default=256)
    parser.add_argument('--opening', type=str)
    args = parser.parse_args()

    options_list = [{}, {}]
    for i, kvs in enumerate([options.split(',') for options in (args.options1, args.options2)]):
        if len(kvs) == 1 and kvs[0] == '':
            continue
        for kv_str in kvs:
            kv = kv_str.split(':')
            if len(kv) != 2:
                raise ValueError('options{}'.format(i + 1))
            options_list[i][kv[0]] = kv[1]

    main(args.engine1, args.engine2,
        options_list[0], options_list[1],
        args.games, args.resign, args.byoyomi, args.draw, args.opening)
