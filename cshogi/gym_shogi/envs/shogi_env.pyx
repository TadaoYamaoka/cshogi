import gym
from gym import spaces
import numpy as np
import cshogi
from collections import defaultdict

class ShogiEnv(gym.Env):
    """A Shogi environment that simulates the game of Shogi.

    This environment conforms to the OpenAI Gym interface.
    """

    metadata = {'render.modes': ['human', 'svg', 'ansi', 'sfen']}

    def __init__(self):
        cdef unsigned long long key

        super().__init__()

        self.board = cshogi.Board()
        self.moves = []
        self.repetition_hash = defaultdict(int)
        key = self.board.zobrist_hash()
        self.repetition_hash[key] += 1
        self.is_draw = None

        self.observation_space = spaces.Box(0, len(cshogi.PIECES)-1, (9, 9), dtype=np.uint8)

        # actionはmoveを直接受け付ける
        # sample()は非合法手も含む
        self.action_space = gym.spaces.Discrete(16777216)

    def reset(self, sfen=None, hcp=None):
        """Reset the game environment to an initial state, or a state specified by SFEN or HCP.

        :param sfen: An SFEN string representing the desired initial board state. Optional.
        :type sfen: str, optional
        :param hcp: A HCP representing the desired initial board state. Optional.
        :type hcp: np.ndarray, optional
        :return: The reset board.
        :rtype: cshogi.Board
        """
        cdef unsigned long long key

        if not sfen is None:
            self.board.set_sfen(sfen)
        elif not hcp is None:
            self.board.set_hcp(hcp)
        else:
            self.board.reset()

        self.moves.clear()
        self.repetition_hash.clear()
        key = self.board.zobrist_hash()
        self.repetition_hash[key] += 1
        self.is_draw = None

        return self.board

    def render(self, str mode='human'):
        """Render the current game state in the specified mode.

        :param mode: The rendering mode ('human', 'svg', 'ansi', or 'sfen'). Default is 'human'.
        :type mode: str
        :return: An instance of Board if rendering in 'human' mode, an SVG if rendering in 'svg' mode, otherwise a string.
        :rtype: Board or SvgWrapper or str
        """
        if mode == 'svg':
            if len(self.moves) > 0:
                return self.board.to_svg(lastmove=self.moves[-1])
            else:
                return self.board.to_svg()
        elif mode == 'ansi':
            return str(self.board)
        elif mode == 'sfen':
            return self.board.sfen()
        else:
            return self.board

    def step(self, int move):
        """Advance the game by making the specified move.

        :param move: The move to be made.
        :type move: int
        :return: A tuple containing the reward (float), done flag (bool), and draw status (bool or None).
        :rtype: tuple
        """
        cdef unsigned long long key

        # 投了
        if move == 0:
            reward = -1.0
            done = True
            return reward, done, None

        assert self.board.is_legal(move)

        self.board.push(move)
        self.moves.append(move)

        key = self.board.zobrist_hash()
        self.repetition_hash[key] += 1
        # 千日手
        if self.repetition_hash[key] == 4:
            done = True
            # 連続王手
            self.is_draw = self.board.is_draw()
            if self.is_draw == cshogi.REPETITION_WIN:
                # 相手の手番なので報酬は反対になる
                reward = -1.0
            elif self.is_draw == cshogi.REPETITION_LOSE:
                reward = 1.0
            else:
                reward = 0.0
        else:
            done = self.board.is_game_over()
            if done:
                reward = 1.0
            else:
                reward = 0.0

        # Observationは直接フィールドを参照する
        return reward, done, self.is_draw
