import datetime
import math
import os
import random
import re
import sys
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed, wait
from contextlib import ExitStack, contextmanager, nullcontext, suppress
from dataclasses import dataclass, replace
from enum import Enum
from io import StringIO
from itertools import chain
from queue import SimpleQueue
from threading import Event, Lock
from time import perf_counter
from typing import Callable

from cshogi import *
from cshogi.usi import Engine
from cshogi import CSA
from cshogi import PGN
from cshogi.elo import Elo, SPRT

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

def _abort_games(engines, pending):
    """Release every reader before reaping any process. Preserve the original failure."""
    for engine in engines:
        with suppress(Exception):
            engine.kill()
    try:
        for future, _ in pending.values():
            with suppress(Exception):
                future.exception()
    finally:
        pending.clear()
        for engine in engines:
            with suppress(Exception):
                engine._reap()


def _stop_ponder(pending, listeners):
    """Send every stop before collecting the readers."""
    if not pending:
        return
    error = None
    for engine, (future, _) in pending.items():
        if not future.done():
            try:
                engine.stop(listener=listeners[engine])
            except Exception as exc:
                if error is None:
                    error = exc
    if error is not None:
        raise error
    for future in as_completed([future for future, _ in pending.values()]):
        future.result()
    pending.clear()


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


LEAGUE_PAIRS = ((0, 1), (0, 2), (1, 2))


def schedule(games, openings, no_swap, league=False):
    index = 0
    for first in range(0, games, 2):
        opening = openings[first // 2 % len(openings)]
        for a, b in (LEAGUE_PAIRS if league else ((0, 1),)):
            pair = [GameSpec(index, 1 if league else first + 1, opening, a, b)]
            if league or first + 1 < games:
                black, white = (b, a) if league or not no_swap else (a, b)
                pair.append(GameSpec(index + 1, 2 if league else first + 2, opening, black, white))
            yield tuple(pair)
            index += len(pair)


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
                with suppress(OSError):
                    engine.kill()


class Listener:
    def __init__(self, label, log):
        self.label = label
        self.log = log
        self.info = ''

    def __call__(self, line):
        if self.log:
            self.log(f'{self.label}:{line}')
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


def _close_on_exit(stack, resource):
    """Report a close failure only if cleanup started without an exception."""
    def close(exc_type, exc, tb):
        try:
            resource.close()
        except Exception:
            if exc_type is None:
                raise

    stack.push(close)


class Records:
    def __init__(self, stack, csa, multi_csa, pgn, no_pgn_moves, parallel):
        self.directory = csa if not multi_csa else None
        self.multi = CSA.Exporter(csa, append=True) if csa and multi_csa else None
        if self.multi:
            _close_on_exit(stack, self.multi)
        self.pgn = PGN.Exporter(pgn, append=True) if pgn else None
        if self.pgn:
            _close_on_exit(stack, self.pgn)
        self.no_pgn_moves = no_pgn_moves
        self.buffer_multi = self.multi is not None and parallel

    def start(self, stack, board, names):
        writer = self.multi
        if self.buffer_multi:
            writer = CSA.Exporter(stream=StringIO())
        if self.directory:
            stamp = datetime.datetime.now().strftime('%Y%m%d%H%M%S')
            while True:
                suffix = f'{random.SystemRandom().getrandbits(32):08x}'
                filename = '+'.join((*names, stamp, suffix)) + '.csa'
                try:
                    stream = open(os.path.join(self.directory, filename), 'x', newline='\n')
                    break
                except FileExistsError:
                    continue
            _close_on_exit(stack, stream)
            writer = CSA.Exporter(stream=stream)
        if writer:
            writer.info(board, names, version='V2')
        return writer

    def finish(self, result, csa_record):
        if csa_record is not None:
            if self.multi.f.tell():
                self.multi.f.write('/\n')
            self.multi.f.write(csa_record)
            self.multi.f.flush()
        if self.pgn:
            outcome = {BLACK: BLACK_WIN, WHITE: WHITE_WIN, None: DRAW}[result.winner]
            self.pgn.tag_pair(result.names, outcome, round=result.spec.round)
            if not self.no_pgn_moves:
                self.pgn.movetext(result.moves)


@dataclass(frozen=True, slots=True)
class Player:
    config: EngineConfig
    engine: Engine
    listener: Listener


class Worker:
    def __init__(self, configs, config, group, match, prefix, executor):
        self.configs = configs
        self.config = config
        self.group = group
        self.match = match
        self.prefix = prefix
        self.executor = executor
        self.players = {}
        self.pending = {}

    def _read_ponder(self, engine, listener):
        try:
            return engine._read_bestmove(listener)
        except BaseException as exc:
            self.match.fail(exc)
            raise

    def play_pairs(self, pairs):
        last_order = ()
        try:
            for spec in chain.from_iterable(pairs):
                if not self.match.try_start():
                    break
                last_order = (spec.black_id, spec.white_id)
                for engine_id in last_order:
                    if engine_id not in self.players:
                        settings = self.configs[engine_id]
                        label = f'{self.prefix}{len(self.players) + 1}'
                        listener = Listener(label, self.match.debug_log if self.config.debug else None)
                        self.players[engine_id] = Player(settings, Engine(settings.command, connect=False), listener)
                    if self.prefix and self.config.debug:
                        self.players[engine_id].listener.label = f'{self.prefix}g{spec.index + 1} {engine_id + 1}'
                with ExitStack() as stack:
                    result, csa_record = self.play_game(spec, stack)
                self.match.report(result, csa_record, self.prefix)
            # Preserve USI quit order for the final game, then release any other owned engine.
            for engine_id in dict.fromkeys((*last_order, *self.players)):
                player = self.players[engine_id]
                if player.engine.proc is not None:
                    player.engine.quit(listener=player.listener)
        except BaseException as exc:
            self.match.fail(exc)
            _abort_games([player.engine for player in self.players.values()], self.pending)
            if not isinstance(exc, Exception):
                raise
        finally:
            for player in self.players.values():
                self.group.discard(player.engine)
            self.players.clear()

    def play_game(self, spec, stack):
        config = self.config
        pending = self.pending
        black, white = self.players[spec.black_id], self.players[spec.white_id]
        engines_order = (black.engine, white.engine)
        listeners_order = (black.listener, white.listener)
        for player in (black, white):
            engine, listener = player.engine, player.listener
            if engine.proc is None:
                self.group.connect(engine, listener)
            if config.ponder:
                engine.setoption('USI_Ponder', 'true', listener=listener)
            for key, value in player.config.options.items():
                engine.setoption(key, value, listener=listener)
            engine.isready(listener=listener)
            if player.config.name:
                engine.name = player.config.name

        names = (black.engine.name, white.engine.name)
        self.match.log(f'{self.prefix}{names[0]} vs {names[1]} start.')
        initial_position, opening_usi_moves = spec.opening
        board = Board()
        if initial_position != 'startpos':
            board.set_sfen(initial_position[5:])
        moves = []
        usi_moves = []
        repetition_hash = defaultdict(int)
        repetition_hash[board.zobrist_hash()] = 1
        writer = self.match.records.start(stack, board, names)
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
            if config.resign is not None and score is not None and score <= -config.resign:
                break

            mate = config.mate_win and score == 100000
            if not mate and bestmove == 'resign':
                break
            if not mate and bestmove == 'win':
                ending = Ending.DECLARATION if board.is_nyugyoku() else Ending.ILLEGAL
                break

            move = board.move_from_usi(bestmove)
            if not board.is_legal(move):
                ending = Ending.ILLEGAL
                break
            if writer:
                writer.move(move, time=int(elapsed_time), comment=usi_info_to_csa_comment(board, listener.info))
            board.push(move)
            moves.append(move)
            usi_moves.append(bestmove)
            if mate:
                break

            key = board.zobrist_hash()
            repetition_hash[key] += 1
            if repetition_hash[key] == 4:
                ending = {REPETITION_WIN: Ending.REPETITION_WIN,
                          REPETITION_LOSE: Ending.REPETITION_LOSS}.get(board.is_draw(), Ending.REPETITION_DRAW)
                break

            # 終局判定
            game_over = board.is_game_over()

            # Send before submitting the reader so stop cannot overtake go ponder.
            if config.ponder and pondermove and not game_over and board.move_number <= config.draw:
                engine.position(usi_moves + [pondermove], sfen=initial_position, listener=listener)
                listener.info = ''
                engine._send_go(ponder=True, byoyomi=byoyomi, btime=remain_time[BLACK], wtime=remain_time[WHITE], binc=binc, winc=winc, listener=listener)
                pending[engine] = (self.executor.submit(self._read_ponder, engine, listener), pondermove)

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
        result = GameResult(spec, names, winner, code, message, tuple(moves))
        if writer:
            writer.endgame(result.csa_code)
        csa_record = writer.f.getvalue() if self.match.records.buffer_multi else None
        return result, csa_record


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


class MatchSprt:
    """Feed completed pairs into the test in schedule order, from engine A's side."""

    def __init__(self, elo0, elo1, alpha, beta, engines):
        self.test = SPRT(elo0, elo1, alpha, beta)
        self.hypotheses = (elo0, elo1)
        self.engines = tuple(engines)
        self.a, self.b = (engine - 1 for engine in self.engines)
        self.pair = tuple(sorted((self.a, self.b)))
        self.pending = defaultdict(list)
        self.next_pair = 0

    def record(self, result, pair_index):
        spec = result.spec
        colors = (spec.black_id, spec.white_id)
        score = 0.5 if result.winner is None else int(colors[result.winner] == self.a)
        self.pending[pair_index].append(score)
        while len(self.pending.get(self.next_pair, ())) == 2:
            self.test.add_pair(sum(self.pending.pop(self.next_pair)) / 2)
            self.next_pair += 1
            if self.test.status() is not None:
                self.pending.clear()
                return True
        return False

    def snapshot(self):
        counts = self.test.counts
        lower, upper = self.test.bounds()
        return dict(engines=self.engines, result=self.test.status(), llr=self.test.llr(),
                    lower=lower, upper=upper, pairs=sum(counts), pentanomial=counts)

    def print_status(self, names, *, decision=False):
        lower, upper = self.test.bounds()
        llr = f'LLR {self.test.llr():.2f} [{lower:.2f}, {upper:.2f}]'
        label = f'SPRT: {names[self.a]} vs {names[self.b]}'
        if decision:
            unit = 'pair' if self.next_pair == 1 else 'pairs'
            print(f'{label} {self.test.status()} accepted after {self.next_pair} {unit} ({llr})')
        else:
            elo0, elo1 = self.hypotheses
            counts = '-'.join(map(str, self.test.counts))
            print(f'{label} {llr} ({elo0:g}, {elo1:g}) pentanomial {counts}')


class LeagueTable:
    """Publish standings in stage order once all games in a stage are recorded."""

    games_per_stage = 2 * len(LEAGUE_PAIRS)

    def __init__(self):
        self.stats = {pair: MatchStats(pair) for pair in LEAGUE_PAIRS}
        self.pending = defaultdict(list)
        self.next_stage = 0

    def record(self, result):
        stage = result.spec.index // self.games_per_stage
        self.pending[stage].append((result.spec, result.names, result.winner))

    def publish_ready(self, games, sprt=None):
        while len(self.pending.get(self.next_stage, ())) == self.games_per_stage:
            for spec, names, winner in self.pending.pop(self.next_stage):
                ids = tuple(sorted((spec.black_id, spec.white_id)))
                self.stats[ids].add(spec, names, winner)
            self.next_stage += 1
            print(f'{self.next_stage * 2} of {games} games finished.')
            for stats in self.stats.values():
                result = stats.snapshot()
                w, l, d = result['engine1_won'], result['engine2_won'], result['draw']
                print(f"{result['engine1_name']} vs {result['engine2_name']}: "
                      f"{w}-{l}-{d} ({rate(w, l, d):.1f}%)")
            if sprt is not None:
                sprt.print_status(self.stats[sprt.pair].names)


class Match:
    def __init__(self, records, games, print_summary, callback, table=None, sprt=None):
        self.records = records
        self.games = games
        self.print_summary = print_summary
        self.show_sprt = print_summary or table is not None
        self.callback = callback
        self.stats = {pair: MatchStats(pair) for pair in (LEAGUE_PAIRS if table else ((0, 1),))}
        self.table = table
        self.sprt = sprt
        self.lock = Lock()
        self.stopped = Event()
        self.error = None
        self.debug_log = self.log

    @contextmanager
    def queued_debug_log(self):
        lines = SimpleQueue()

        def drain():
            try:
                for line in iter(lines.get, None):
                    self.log(line)
            except Exception as exc:
                self.fail(exc)

        with ThreadPoolExecutor(max_workers=1) as pool:
            writer = pool.submit(drain)
            self.debug_log = lines.put
            try:
                yield
            finally:
                lines.put(None)
                writer.result()
                self.debug_log = self.log

    def try_start(self):
        with self.lock:
            return not self.stopped.is_set()

    def take_pairs(self, schedule):
        while True:
            with self.lock:
                pair = None if self.stopped.is_set() else next(schedule, None)
            if pair is None:
                return
            yield pair

    def log(self, line):
        with self.lock:
            print(line)

    def _fail_locked(self, exc):
        if self.error is None:
            self.error = exc
        self.stopped.set()

    def fail(self, exc):
        with self.lock:
            self._fail_locked(exc)

    @contextmanager
    def _reporting(self):
        with self.lock:
            try:
                yield
            except BaseException as exc:
                self._fail_locked(exc)
                raise

    def publish_standings(self):
        with self._reporting():
            self.table.publish_ready(self.games, self.sprt)

    def snapshot(self, pair):
        snapshot = self.stats[pair].snapshot()
        if self.sprt is not None and pair == self.sprt.pair:
            snapshot['sprt'] = self.sprt.snapshot()
        return snapshot

    def report(self, result, csa_record, prefix):
        with self._reporting():
            active = not self.stopped.is_set()
            spec = result.spec
            pair = tuple(sorted((spec.black_id, spec.white_id)))
            stats = self.stats[pair]
            stats.add(spec, result.names, result.winner)
            decided = False
            if active and self.sprt is not None and pair == self.sprt.pair:
                stride = self.table.games_per_stage if self.table else 2
                decided = self.sprt.record(result, spec.index // stride)

            print(prefix + result.message)
            if decided and self.show_sprt:
                self.sprt.print_status(stats.names, decision=True)
            snapshot = self.snapshot(pair)
            if self.print_summary:
                stats.summary(snapshot, self.games)
                if self.sprt is not None:
                    self.sprt.print_status(stats.names)
            self.records.finish(result, csa_record)
            if self.table is not None:
                self.table.record(result)
            callback_stop = active and self.callback and not self.callback(snapshot)
            if decided or callback_stop:
                self.stopped.set()


def _clock_values(value, count):
    if not isinstance(value, (tuple, list)):
        return [value] * count
    if len(value) == count:
        return value
    if len(value) == 1 or (count == 3 and value):
        return [value[0]] * count
    raise _MatchConfigError(f'expected one or {count} clock values')


class _MatchConfigError(ValueError):
    pass


@dataclass(frozen=True, slots=True)
class SprtConfig:
    """Logistic Elo hypotheses for engine A against B (one-based engine numbers)."""

    elo0: float
    elo1: float
    alpha: float = 0.05
    beta: float = 0.05
    engines: tuple[int, int] | None = None

    def _for_match(self, count, games, no_swap):
        if games % 2:
            raise _MatchConfigError('SPRT requires an even --games value')
        if no_swap:
            raise _MatchConfigError('--no-swap cannot be used with SPRT')
        engines = self.engines
        if engines is None:
            if count == 3:
                raise _MatchConfigError('league SPRT requires --sprt-engines A B')
            engines = (1, 2)
        message = f'SPRT requires two distinct engine numbers between 1 and {count}'
        try:
            a, b = engines
        except (TypeError, ValueError) as exc:
            raise _MatchConfigError(message) from exc
        if any(type(i) is not int or not 1 <= i <= count for i in (a, b)) or a == b:
            raise _MatchConfigError(message)
        try:
            return MatchSprt(self.elo0, self.elo1, self.alpha, self.beta, (a, b))
        except ValueError as exc:
            raise _MatchConfigError(str(exc)) from exc


def _engine_configs(commands, names, options, byoyomi, time, inc):
    count = len(commands)
    if names is None:
        names = [None] * count
    elif len(names) != count:
        raise _MatchConfigError(f'names must have {count} entries')
    engine_options = ({} if opts is None else opts for opts in options)
    return tuple(EngineConfig(*values) for values in zip(
        commands, names, engine_options, _clock_values(byoyomi, count),
        _clock_values(time, count), _clock_values(inc, count), strict=True))


def _worker_configs(configs, concurrency, worker_options, display):
    if type(concurrency) is not int or concurrency < 1:
        raise _MatchConfigError('concurrency must be a positive integer')
    if display and concurrency > 1:
        raise _MatchConfigError('--display requires concurrency=1')
    overrides = worker_options if worker_options is not None else [None] * len(configs)
    if len(overrides) != len(configs):
        raise _MatchConfigError('worker_options must have one entry per engine')
    for engine_id, items in enumerate(overrides, 1):
        if items is not None and len(items) != concurrency:
            raise _MatchConfigError(f'--worker-options{engine_id} requires {concurrency} entries')
    if concurrency > 1:
        configs = tuple(replace(cfg, options=dict(cfg.options)) for cfg in configs)
    return [tuple(replace(cfg, options={**cfg.options, **items[i]}) if items is not None else cfg
                  for cfg, items in zip(configs, overrides))
            for i in range(concurrency)]


def run_workers(pairs, workers, config, group, match):
    def run(index):
        prefix = f'[worker{index + 1}] ' if len(workers) > 1 else ''
        try:
            with ExitStack() as stack:
                executor = stack.enter_context(ThreadPoolExecutor(max_workers=2)) if config.ponder else None
                worker = Worker(workers[index], config, group, match, prefix, executor)
                jobs = match.take_pairs(pairs)
                if match.table is None:
                    worker.play_pairs(jobs)
                else:
                    for pair in jobs:
                        worker.play_pairs((pair,))
                        match.publish_standings()
        except BaseException as exc:
            if len(workers) == 1 and isinstance(exc, KeyboardInterrupt):
                raise
            match.fail(exc)

    if len(workers) == 1:
        run(0)
    else:
        pair_count = (match.games + 1) // 2
        if match.table is not None:
            pair_count *= len(LEAGUE_PAIRS)
        with ThreadPoolExecutor(max_workers=len(workers)) as pool:
            try:
                pending = {pool.submit(run, i) for i in range(min(len(workers), pair_count))}
                while pending:
                    _, pending = wait(pending, timeout=0.5)
            except BaseException:
                match.stopped.set()
                group.close()
                raise


def main(engine1: str, engine2: str, options1: dict | None = None, options2: dict | None = None, names: list[str] | None = None,
         games: int = 1, resign: int | None = None, mate_win: bool = False,
         byoyomi: int | list[int] | None = None, time: int | list[int] | None = None, inc: int | list[int] | None = None,
         draw: int = 256, ponder: bool = False, no_swap: bool = False,
         opening: str | None = None, opening_moves: int = 24, opening_seed: int | None = None, opening_index: int | None = None,
         keep_process: bool = False,
         csa: str | None = None, multi_csa: bool = False, pgn: str | None = None, no_pgn_moves: bool = False,
         is_display: bool = False, debug: bool = False,
         print_summary: bool = True, callback: Callable | None = None, concurrency: int = 1,
         worker_options: list[list[dict] | None] | None = None, sprt: SprtConfig | None = None) -> dict:
    """Executes a series of games between two USI engines.

    :param engine1: Path to the first USI engine.
    :param engine2: Path to the second USI engine.
    :param options1: Optional engine options for engine1.
    :param options2: Optional engine options for engine2.
    :param names: Optional list with one display name per engine.
    :param games: Non-negative number of games to play.
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
    :param concurrency: Number of workers playing independently.
    :param worker_options: One entry per engine, either None or one option dictionary per worker.
    :param sprt: Optional sequential test on color-swapped pairs. Requires an even game limit.
    :return: Dictionary containing game results and statistics.
    """

    configs = _engine_configs((engine1, engine2), names, (options1, options2),
                              byoyomi, time, inc)
    config = GameConfig(resign, mate_win, draw, ponder, opening_moves, keep_process, is_display, debug)
    return _run(configs, config, games, opening, opening_seed, opening_index, no_swap,
                csa, multi_csa, pgn, no_pgn_moves, concurrency, worker_options, print_summary, callback, sprt)


def league(engine1: str, engine2: str, engine3: str, options1=None, options2=None, options3=None,
           names=None, *, games=1, resign=None, mate_win=False, byoyomi=None, time=None, inc=None,
           draw=256, ponder=False, no_swap=False, opening=None, opening_moves=24, opening_seed=None,
           opening_index=None, keep_process=False, csa=None, multi_csa=False, pgn=None,
           no_pgn_moves=False, is_display=False, debug=False, concurrency=1, worker_options=None,
           sprt: SprtConfig | None = None):
    """Play two games per pairing and opening. Return standings in AB, AC, BC order."""
    configs = _engine_configs((engine1, engine2, engine3), names,
                              (options1, options2, options3), byoyomi, time, inc)
    config = GameConfig(resign, mate_win, draw, ponder, opening_moves, keep_process, is_display, debug)
    return _run(configs, config, games, opening, opening_seed, opening_index, no_swap,
                csa, multi_csa, pgn, no_pgn_moves, concurrency, worker_options, False, None, sprt)


def _run(configs, config, games, opening, seed, index, no_swap, csa, multi_csa, pgn, no_pgn_moves,
         concurrency, worker_options, print_summary, callback, sprt_config):
    if games < 0:
        raise _MatchConfigError('games must be non-negative')
    sprt = sprt_config._for_match(len(configs), games, no_swap) if sprt_config is not None else None
    workers = _worker_configs(configs, concurrency, worker_options, config.display)
    table = LeagueTable() if len(configs) == 3 else None
    pairs = schedule(games, load_openings(opening, seed, index), no_swap, table is not None)
    with ExitStack() as stack:
        records = Records(stack, csa, multi_csa, pgn, no_pgn_moves, concurrency > 1)
        match = Match(records, games, print_summary, callback, table, sprt)
        debug_output = match.queued_debug_log() if config.debug and concurrency > 1 else nullcontext()
        with debug_output:
            run_workers(pairs, workers, config, ProcessGroup(), match)
        if match.error is not None:
            raise match.error
    if sprt is not None and match.show_sprt and not match.stopped.is_set():
        pairs = sprt.next_pair
        unit = 'pair' if pairs == 1 else 'pairs'
        print(f'SPRT: no decision after {pairs} {unit} (game limit reached)')
    if table is not None:
        return [match.snapshot(pair) for pair in LEAGUE_PAIRS]
    return match.snapshot((0, 1))


def _parse_options(value):
    options = {}
    for entry in value.split(',') if value else ():
        key, sep, option = entry.partition(':')
        if not sep or not key:
            raise ValueError(f'expected name:value, got {entry!r}')
        options[key] = option
    return options


def _cli(argv=None):
    import argparse

    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(line_buffering=True)

    parser = argparse.ArgumentParser()
    parser.add_argument('engine1')
    parser.add_argument('engine2')
    parser.add_argument('engine3', nargs='?')
    parser.add_argument('--options1', type=str, default='')
    parser.add_argument('--options2', type=str, default='')
    parser.add_argument('--options3', type=str, default='')
    parser.add_argument('-c', '--concurrency', type=int, default=1, help='number of workers (default: 1)')
    for number in (1, 2, 3):
        parser.add_argument(f'--worker-options{number}', nargs='+', metavar='OPTIONS',
                            help=f'one comma-separated name:value list per worker for engine {number}')
    parser.add_argument('--name1', type=str)
    parser.add_argument('--name2', type=str)
    parser.add_argument('--name3', type=str)
    parser.add_argument('--games', type=int, default=1)
    parser.add_argument('--sprt', nargs=2, type=float, metavar=('ELO0', 'ELO1'),
                        help='test two logistic Elo hypotheses using color-swapped pairs (requires even --games)')
    parser.add_argument('--sprt-alpha', type=float, help='type I error probability (default: 0.05)')
    parser.add_argument('--sprt-beta', type=float, help='type II error probability (default: 0.05)')
    parser.add_argument('--sprt-engines', nargs=2, type=int, metavar=('A', 'B'),
                        help='test engine A against B, numbered from 1 (required in a league)')
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
    args = parser.parse_args(argv)

    count = 3 if args.engine3 is not None else 2
    if count == 2 and args.worker_options3 is not None:
        parser.error('--worker-options3 requires a third engine')
    sprt_options = {key: value for key, value in (
        ('alpha', args.sprt_alpha), ('beta', args.sprt_beta),
        ('engines', tuple(args.sprt_engines) if args.sprt_engines else None),
    ) if value is not None}
    if sprt_options and args.sprt is None:
        parser.error('--sprt-alpha, --sprt-beta and --sprt-engines require --sprt ELO0 ELO1')
    sprt = SprtConfig(*args.sprt, **sprt_options) if args.sprt is not None else None
    try:
        options_list = [_parse_options(getattr(args, f'options{i}')) for i in range(1, count + 1)]
        worker_options = []
        for i in range(1, count + 1):
            items = getattr(args, f'worker_options{i}')
            worker_options.append([_parse_options(item) for item in items] if items else None)
    except ValueError as exc:
        parser.error(str(exc))

    if args.csa is not None and not args.multi_csa:
        os.makedirs(args.csa, exist_ok=True)

    commands = [args.engine1, args.engine2, args.engine3][:count]
    names = [args.name1, args.name2, args.name3][:count]
    run = league if count == 3 else main
    try:
        run(*commands, *options_list, names=names, games=args.games, resign=args.resign,
            mate_win=args.mate_win, byoyomi=args.byoyomi, time=args.time, inc=args.inc,
            draw=args.draw, ponder=args.ponder, no_swap=args.no_swap, opening=args.opening,
            opening_moves=args.opening_moves, opening_seed=args.opening_seed, opening_index=args.opening_index,
            keep_process=args.keep_process, csa=args.csa, multi_csa=args.multi_csa, pgn=args.pgn,
            no_pgn_moves=args.no_pgn_moves, is_display=args.display, debug=args.debug,
            concurrency=args.concurrency, worker_options=worker_options, sprt=sprt)
    except _MatchConfigError as exc:
        parser.error(str(exc))


if __name__ == '__main__':
    _cli()
