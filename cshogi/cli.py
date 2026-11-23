from typing import Callable
from dataclasses import dataclass
from enum import Enum
from threading import Lock
import random
import math
from time import perf_counter
import re
import os
import datetime
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
from contextlib import ExitStack, suppress

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

re_usi_info = re.compile(
    r'^info (?:(?!string\b)\S+\s+)*?score (cp|mate) ([+-]?\d+|[+-])(?=\s|$)'
    r'(?:(?!\b(?:pv|string)\b).)*'
    r'(?:\bpv (.*)|\bstring.*)?$')
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
    for usi_move in (m[3] or '').split():
        move = board2.move_from_usi(usi_move)
        if not board2.is_legal(move):
            break
        pv.append(CSA.COLOR_SYMBOLS[board2.turn] + move_to_csa(move))
        board2.push(move)

    if len(pv) > 1:
        return f"** {score} {' '.join(pv[1:])}"
    else:
        return f"** {score}"

def usi_info_to_score(info):
    m = re_usi_info.match(info)
    if m is None:
        return None

    return to_score(m)

def _stop_ponder(pending, listeners):
    """Stop all searches before joining readers; report the first error."""
    error = None
    killed = set()
    for engine, (future, _) in pending.items():
        try:
            if not future.done():
                engine.stop(listener=listeners[engine])
        except BaseException as exc:
            if error is None:
                error = exc
            if not future.done():
                # A failed stop may leave the reader blocked on engine output.
                engine.kill()
                killed.add(engine)
    for engine, (future, _) in pending.items():
        try:
            future.result()
        except BaseException as exc:
            if error is None:
                error = exc
        finally:
            if engine in killed:
                engine._reap()
    pending.clear()
    if error is not None:
        raise error


Opening = tuple[str, tuple[str, ...]]


def load_openings(opening, seed, index):
    if not opening:
        return [('startpos', ())]
    opening_list = []
    with open(opening) as f:
        for line_number, line in enumerate(f, 1):
            parts = line.split()
            if parts and parts[0] == 'startpos':
                position_end = 1
            elif len(parts) >= 5 and parts[0] == 'sfen':
                position_end = 5
            else:
                raise ValueError(f'{opening}:{line_number}: expected startpos or sfen')
            if len(parts) > position_end and parts[position_end] != 'moves':
                raise ValueError(f'{opening}:{line_number}: expected moves after position')
            initial_position = ' '.join(parts[:position_end])
            opening_usi_moves = parts[position_end + 1:]
            opening_board = Board()
            if initial_position != 'startpos':
                opening_board.set_sfen(initial_position[5:])
            for move_usi in opening_usi_moves:
                move = opening_board.move_from_usi(move_usi)
                if not opening_board.is_legal(move):
                    raise ValueError(f'{opening}:{line_number}: illegal opening move: {move_usi}')
                opening_board.push(move)
            opening_list.append((initial_position, tuple(opening_usi_moves)))
    if not opening_list:
        raise ValueError(f'{opening}: no opening positions')
    if index is not None:
        return [opening_list[index]]
    random.Random(seed).shuffle(opening_list)
    return opening_list


@dataclass(frozen=True, slots=True)
class GameSpec:
    index: int
    round: int
    opening: Opening
    black_id: int
    white_id: int


def schedule(games, openings, no_swap):
    pairs = []
    for first in range(0, games, 2):
        opening = openings[first // 2 % len(openings)]
        pair = []
        for index in range(first, min(first + 2, games)):
            black = 0 if no_swap else index % 2
            pair.append(GameSpec(index, index + 1, opening, black, 1 - black))
        pairs.append(tuple(pair))
    return pairs


@dataclass(frozen=True, slots=True)
class EngineConfig:
    command: str
    name: str | None
    options: dict
    byoyomi: int | None
    time: int | None
    inc: int | None


@dataclass(frozen=True, slots=True)
class GameConfig:
    resign: int | None
    mate_win: bool
    draw: int
    ponder: bool
    opening_moves: int
    keep_process: bool
    display: bool
    debug: bool


class ProcessGroup:
    def __init__(self):
        self.lock = Lock()
        self.engines = set()
        self.closed = False

    def connect(self, engine, listener):
        with self.lock:
            if self.closed:
                raise InterruptedError('match closed')
            self.engines.add(engine)
            engine._spawn()
        engine._handshake(listener)

    def discard(self, engine):
        with self.lock:
            self.engines.discard(engine)

    def close(self):
        with self.lock:
            self.closed = True
            for engine in self.engines:
                engine.kill()


class Listener:
    def __init__(self, label, debug):
        self.label = label
        self.debug = debug
        self.info = ''

    def __call__(self, line):
        if self.debug:
            print(f'{self.label}:{line}')
        if re_usi_info.match(line):
            self.info = line


class Ending(Enum):
    LOSS = ('%TORYO', '{}の勝ち')
    DECLARATION = ('%KACHI', '入玉宣言')
    ILLEGAL = ('%ILLEGAL_MOVE', '{}の反則負け')
    TIME = ('%TIME_UP', '{}の切れ負け')
    REPETITION_WIN = (None, '{}の反則勝ち')
    REPETITION_LOSS = (None, '{}の反則負け')
    REPETITION_DRAW = ('%SENNICHITE', '千日手')
    MOVE_LIMIT = ('%JISHOGI', '持将棋')

    def outcome(self, turn, ply):
        code, label = self.value
        if self in (Ending.REPETITION_DRAW, Ending.MOVE_LIMIT):
            winner = None
        elif self in (Ending.REPETITION_WIN, Ending.DECLARATION):
            winner = turn
        else:
            winner = opponent(turn)
        if code is None:
            code = '%-ILLEGAL_ACTION' if winner == BLACK else '%+ILLEGAL_ACTION'
        side = winner if self in (Ending.LOSS, Ending.REPETITION_WIN) else turn
        return winner, code, f'まで{ply}手で' + label.format('先手' if side == BLACK else '後手')


@dataclass(frozen=True, slots=True)
class GameResult:
    spec: GameSpec
    names: tuple[str, str]
    winner: int | None
    csa_code: str
    message: str
    moves: tuple[int, ...]


class Records:
    def __init__(self, stack, csa, multi_csa, pgn, no_pgn_moves):
        self.directory = csa if not multi_csa else None
        self.multi = CSA.Exporter(csa, append=True) if csa and multi_csa else None
        if self.multi:
            stack.callback(self.multi.close)
        self.pgn = PGN.Exporter(pgn, append=True) if pgn else None
        if self.pgn:
            stack.callback(self.pgn.close)
        self.no_pgn_moves = no_pgn_moves

    def start(self, stack, board, names):
        writer = self.multi
        if self.directory:
            suffix = f'{random.SystemRandom().getrandbits(32):08x}'
            stamp = datetime.datetime.now().strftime('%Y%m%d%H%M%S')
            filename = '+'.join((*names, stamp, suffix)) + '.csa'
            writer = CSA.Exporter(os.path.join(self.directory, filename))
            stack.callback(writer.close)
        if writer:
            writer.info(board, names, version='V2')
        return writer

    def finish(self, result, writer):
        if writer:
            writer.endgame(result.csa_code)
        if self.pgn:
            outcome = {BLACK: BLACK_WIN, WHITE: WHITE_WIN, None: DRAW}[result.winner]
            self.pgn.tag_pair(result.names, outcome, round=result.spec.round)
            if not self.no_pgn_moves:
                self.pgn.movetext(result.moves)


@dataclass
class Player:
    config: EngineConfig
    engine: Engine
    listener: Listener


def play_game(spec, players, config, group, executor, pending, records, stack, prefix):
    black, white = players[spec.black_id], players[spec.white_id]
    engines_order = (black.engine, white.engine)
    listeners_order = (black.listener, white.listener)
    for player in (black, white):
        engine, listener = player.engine, player.listener
        if engine.proc is None:
            group.connect(engine, listener)
        if config.ponder:
            engine.setoption('USI_Ponder', 'true', listener=listener)
        for key, value in player.config.options.items():
            engine.setoption(key, value, listener=listener)
        engine.isready(listener=listener)
        if player.config.name:
            engine.name = player.config.name

    names = (black.engine.name, white.engine.name)
    print(f'{prefix}{names[0]} vs {names[1]} start.')
    initial_position, opening_usi_moves = spec.opening
    board = Board()
    if initial_position != 'startpos':
        board.set_sfen(initial_position[5:])
    moves = []
    usi_moves = []
    repetition_hash = defaultdict(int)
    repetition_hash[board.zobrist_hash()] = 1
    writer = records.start(stack, board, names)
    for move_usi in opening_usi_moves:
        move = board.push_usi(move_usi)
        if writer:
            writer.move(move)
        moves.append(move)
        usi_moves.append(move_usi)
        repetition_hash[board.zobrist_hash()] += 1
        if len(usi_moves) >= config.opening_moves:
            break

    if config.display:
        print('開始局面')
        if is_jupyter:
            display(SVG(board.to_svg()))
        else:
            print(board)

    # 新規ゲーム
    for engine, listener in zip(engines_order, listeners_order):
        engine.usinewgame(listener=listener)

    # 対局
    ending = Ending.LOSS
    remain_time = [black.config.time, white.config.time]
    binc, winc = black.config.inc, white.config.inc
    inc_time = (binc, winc)
    byoyomi_order = (black.config.byoyomi, white.config.byoyomi)
    while True:
        engine_index = board.turn
        engine = engines_order[engine_index]
        listener = listeners_order[engine_index]
        byoyomi = byoyomi_order[engine_index]

        # 持将棋
        if board.move_number > config.draw:
            ending = Ending.MOVE_LIMIT
            break

        # ponder
        ponderhit = False
        if engine in pending:
            future, expected_move = pending[engine]
            if bestmove == expected_move:
                ponderhit = True
                start_time = perf_counter()
                engine.ponderhit(listener=listener)
                bestmove, pondermove = future.result()
                elapsed_time = perf_counter() - start_time
            else:
                engine.stop(listener=listener)
                future.result()
            del pending[engine]

        if not ponderhit:
            # position
            engine.position(usi_moves, sfen=initial_position, listener=listener)

            start_time = perf_counter()

            # go
            listener.info = ''
            bestmove, pondermove = engine.go(byoyomi=byoyomi, btime=remain_time[BLACK], wtime=remain_time[WHITE], binc=binc, winc=winc, listener=listener)

            elapsed_time = perf_counter() - start_time

        if remain_time[board.turn] is not None:
            if inc_time[board.turn] is not None:
                remain_time[board.turn] += inc_time[board.turn]
            remain_time[board.turn] -= math.ceil(elapsed_time * 1000)

            if remain_time[board.turn] < 0:
                # 秒読みを超えても1秒未満は切れ負けにしない
                if remain_time[board.turn] > -((byoyomi or 0) + 1000):
                    remain_time[board.turn] = 0
                else:
                    # 時間切れ負け
                    ending = Ending.TIME
                    break

        score = usi_info_to_score(listener.info)
        # 投了閾値
        if config.resign is not None:
            if score is not None and score <= -config.resign:
                # 投了
                break

        # 詰みを見つけたら終了
        if config.mate_win:
            if score is not None and score == 100000:
                move = board.move_from_usi(bestmove)
                if not board.is_legal(move):
                    ending = Ending.ILLEGAL
                    break
                if writer:
                    writer.move(move, time=int(elapsed_time), comment=usi_info_to_csa_comment(board, listener.info))
                board.push(move)
                moves.append(move)
                usi_moves.append(bestmove)
                break

        if bestmove == 'resign':
            # 投了
            break
        elif bestmove == 'win':
            # 入玉勝ち宣言
            if board.is_nyugyoku():
                ending = Ending.DECLARATION
            else:
                ending = Ending.ILLEGAL
            break
        else:
            move = board.move_from_usi(bestmove)
            if board.is_legal(move):
                if writer:
                    writer.move(move, time=int(elapsed_time), comment=usi_info_to_csa_comment(board, listener.info))
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
                        ending = Ending.REPETITION_WIN
                        break
                    elif is_draw == REPETITION_LOSE:
                        ending = Ending.REPETITION_LOSS
                        break
                    ending = Ending.REPETITION_DRAW
                    break
            else:
                ending = Ending.ILLEGAL
                break

        # 終局判定
        game_over = board.is_game_over()

        # Send before submitting the reader so stop cannot overtake go ponder.
        if config.ponder and pondermove and not game_over and board.move_number <= config.draw:
            engine.position(usi_moves + [pondermove], sfen=initial_position, listener=listener)
            listener.info = ''
            engine._send_go(ponder=True, byoyomi=byoyomi, btime=remain_time[BLACK], wtime=remain_time[WHITE], binc=binc, winc=winc, listener=listener)
            pending[engine] = (executor.submit(engine._read_bestmove, listener), pondermove)

        # 盤面表示
        if config.display:
            print('{}手目'.format(len(usi_moves)))
            if is_jupyter:
                display(SVG(board.to_svg(move)))
            else:
                print(board)

        if game_over:
            break
    _stop_ponder(pending, dict(zip(engines_order, listeners_order)))
    for player in (black, white):
        player.engine.gameover(listener=player.listener)
    if not config.keep_process:
        for player in (black, white):
            player.engine.quit(listener=player.listener)
    winner, code, message = ending.outcome(board.turn, board.move_number - 1)
    return GameResult(spec, names, winner, code, message, tuple(moves)), writer


def rate(wins, losses, draws):
    total = wins + losses + draws
    return (wins + draws / 2) / total * 100 if total else 0.0


class MatchStats:
    def __init__(self, ids=(0, 1)):
        self.ids = ids
        self.names = {}
        # Per engine and color, store wins, losses, draws in display order.
        self.counts = {i: [[0, 0, 0], [0, 0, 0]] for i in ids}

    def add(self, spec, names, winner):
        for color, engine_id in enumerate((spec.black_id, spec.white_id)):
            self.names[engine_id] = names[color]
            outcome = 2 if winner is None else int(winner != color)
            self.counts[engine_id][color][outcome] += 1

    def snapshot(self):
        a, b = self.ids
        black, white = self.counts[a]
        wins = black[0] + white[0]
        losses = black[1] + white[1]
        draws = black[2] + white[2]
        return dict(engine1_name=self.names.get(a), engine2_name=self.names.get(b),
                    engine1_won=wins, engine2_won=losses,
                    black_won=black[0] + white[1], white_won=white[0] + black[1],
                    draw=draws, total=wins + losses + draws)

    def summary(self, stats, games):
        wins, losses, draws = stats['engine1_won'], stats['engine2_won'], stats['draw']
        black, white = stats['black_won'], stats['white_won']
        print(f"{stats['total']} of {games} games finished.")
        print(f"{stats['engine1_name']} vs {stats['engine2_name']}: "
              f"{wins}-{losses}-{draws} ({rate(wins, losses, draws):.1f}%)")
        print(f'Black vs White: {black}-{white}-{draws} ({rate(black, white, draws):.1f}%)')
        for engine_id in self.ids:
            for side, counts in zip(('Black', 'White'), self.counts[engine_id]):
                w, l, d = counts
                print(f'{self.names[engine_id]} playing {side}: {w}-{l}-{d} ({rate(w, l, d):.1f}%)')
        if wins and losses:
            elo = Elo(wins, losses, draws)
            try:
                error_margin = elo.error_margin()
            except ValueError:
                error_margin = math.nan
            print('Elo difference: {:.1f} +/- {:.1f}, LOS: {:.1f} %, DrawRatio: {:.1f} %'.format(
                elo.diff(), error_margin, elo.los(), elo.draw_ratio()))


class Match:
    def __init__(self, records, games, print_summary, callback):
        self.records = records
        self.games = games
        self.print_summary = print_summary
        self.callback = callback
        self.stats = MatchStats()
        self.lock = Lock()
        self.stopped = False
        self.error = None

    def try_start(self):
        with self.lock:
            return not self.stopped

    def _fail_locked(self, exc):
        if self.error is None:
            self.error = exc
        self.stopped = True

    def fail(self, exc):
        with self.lock:
            self._fail_locked(exc)

    def report(self, result, writer, prefix):
        with self.lock:
            try:
                print(prefix + result.message)
                self.stats.add(result.spec, result.names, result.winner)
                snapshot = self.stats.snapshot()
                if self.print_summary:
                    self.stats.summary(snapshot, self.games)
                self.records.finish(result, writer)
                if self.callback and not self.stopped and not self.callback(snapshot):
                    self.stopped = True
            except BaseException as exc:
                self._fail_locked(exc)
                raise


def play_pairs(pairs, configs, config, group, match, prefix=''):
    players = {}
    pending = {}
    with ExitStack() as stack:
        executor = stack.enter_context(ThreadPoolExecutor(max_workers=2)) if config.ponder else None
        try:
            for pair in pairs:
                for spec in pair:
                    if not match.try_start():
                        break
                    for engine_id in (spec.black_id, spec.white_id):
                        if engine_id not in players:
                            settings = configs[engine_id]
                            players[engine_id] = Player(settings, Engine(settings.command, connect=False),
                                                        Listener(f'{prefix}{engine_id + 1}', config.debug))
                    with ExitStack() as game_stack:
                        result, writer = play_game(spec, players, config, group, executor, pending,
                                                   match.records, game_stack, prefix)
                        match.report(result, writer, prefix)
                else:
                    continue
                break
            for player in players.values():
                if player.engine.proc is not None:
                    player.engine.quit(listener=player.listener)
        except BaseException as exc:
            match.fail(exc)
            for player in players.values():
                with suppress(Exception):
                    player.engine.kill()
            for future, _ in pending.values():
                with suppress(BaseException):
                    future.result()
            for player in players.values():
                with suppress(Exception):
                    player.engine._reap()
        finally:
            for player in players.values():
                group.discard(player.engine)


def _clock_values(value, count):
    if not isinstance(value, (tuple, list)):
        return [value] * count
    if len(value) == count:
        return value
    if len(value) == 1 or (count == 3 and value):
        return [value[0]] * count
    raise ValueError(f'expected one or {count} clock values')


def _engine_configs(commands, names, options, byoyomi, time, inc):
    count = len(commands)
    return tuple(EngineConfig(*values) for values in zip(
        commands, names or [None] * count, options, _clock_values(byoyomi, count),
        _clock_values(time, count), _clock_values(inc, count), strict=True))


def main(engine1: str, engine2: str, options1: dict | None = None, options2: dict | None = None, names: list[str] | None = None,
         games: int = 1, resign: int | None = None, mate_win: bool = False,
         byoyomi: int | list[int] | None = None, time: int | list[int] | None = None, inc: int | list[int] | None = None,
         draw: int = 256, ponder: bool = False, no_swap: bool = False,
         opening: str | None = None, opening_moves: int = 24, opening_seed: int | None = None, opening_index: int | None = None,
         keep_process: bool = False,
         csa: str | None = None, multi_csa: bool = False, pgn: str | None = None, no_pgn_moves: bool = False,
         is_display: bool = False, debug: bool = False,
         print_summary: bool = True, callback: Callable | None = None) -> dict:
    """Executes a series of games between two USI engines.

    :param engine1: Path to the first USI engine.
    :param engine2: Path to the second USI engine.
    :param options1: Optional engine options for engine1.
    :param options2: Optional engine options for engine2.
    :param names: Optional names for the engines.
    :param games: Number of games to play.
    :param resign: Resignation threshold. If a score falls below the negative of this value, the engine resigns.
    :param mate_win: Whether a checkmate is considered a win.
    :param byoyomi: Byoyomi time in milliseconds. It can be different for each engine.
    :param time: Time control in milliseconds. It can be different for each engine.
    :param inc: Increment time in milliseconds per move. It can be different for each engine.
    :param draw: Number of moves before a draw is declared.
    :param ponder: Whether to use pondering.
    :param no_swap: Whether to disable engine swapping.
    :param opening: Path to a file containing opening positions.
    :param opening_moves: Number of opening moves to play.
    :param opening_seed: Seed for random shuffling of openings.
    :param opening_index: Specific index of an opening to use.
    :param keep_process: Whether to keep the engine process running after completion.
    :param csa: Optional path for CSA file export.
    :param multi_csa: Whether to use multi-CSA format.
    :param pgn: Optional path for PGN file export.
    :param no_pgn_moves: Whether to omit PGN move listing.
    :param is_display: Whether to display the game board.
    :param debug: Whether to enable debugging mode.
    :param print_summary: Whether to print the summary after the match.
    :param callback: Optional callback function executed after each game.
    :return: Dictionary containing game results and statistics.
    """

    configs = _engine_configs((engine1, engine2), names, (options1 or {}, options2 or {}),
                              byoyomi, time, inc)
    config = GameConfig(resign, mate_win, draw, ponder, opening_moves, keep_process, is_display, debug)
    pairs = schedule(games, load_openings(opening, opening_seed, opening_index), no_swap)
    group = ProcessGroup()
    with ExitStack() as stack:
        records = Records(stack, csa, multi_csa, pgn, no_pgn_moves)
        match = Match(records, games, print_summary, callback)
        play_pairs(pairs, configs, config, group, match)
        if match.error is not None:
            raise match.error
        return match.stats.snapshot()

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
    parser.add_argument('--mate-win', action='store_true')
    parser.add_argument('--byoyomi', type=int, nargs='+')
    parser.add_argument('--time', type=int, nargs='+')
    parser.add_argument('--inc', type=int, nargs='+')
    parser.add_argument('--draw', type=int, default=256)
    parser.add_argument('--ponder', action='store_true')
    parser.add_argument('--no-swap', action='store_true')
    parser.add_argument('--opening', type=str)
    parser.add_argument('--opening-moves', type=int, default=24)
    parser.add_argument('--opening-seed', type=int)
    parser.add_argument('--opening-index', type=int)
    parser.add_argument('--keep-process', action='store_true')
    parser.add_argument('--csa', type=str)
    parser.add_argument('--multi-csa', action='store_true')
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
            args.draw, args.ponder, args.no_swap, args.opening, args.opening_moves, args.opening_seed, args.opening_index,
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
                    args.draw, args.ponder, False, args.opening, args.opening_moves, None, opening_index,
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
