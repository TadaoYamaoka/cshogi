import cshogi
from cshogi.gym_shogi.envs import ShogiEnv

class ShogiVecEnv:
    """A vectorized Shogi environment that can manage multiple instances of Shogi games simultaneously.

    :param num_envs: The number of environments to manage.
    :type num_envs: int
    """

    metadata = {'render.modes': ['sfen']}

    def __init__(self, int num_envs):
        self.num_envs = num_envs
        self.envs = [ShogiEnv() for _ in range(num_envs)]
        
    def reset(self):
        """Reset all the environments to their initial state."""
        for env in self.envs:
            env.reset()

    def render(self, mode='sfen'):
        """Render the current game state in the specified mode for all environments.

        :param mode: The rendering mode ('sfen'). Default is 'sfen'.
        :type mode: str
        :return: A list of string representations of the environments.
        :rtype: list of str
        """
        return [env.render(mode='sfen') for env in self.envs]

    def step(self, list moves):
        """Advance the games by making the specified moves in all environments.

        :param moves: A list of moves to be made, one for each environment.
        :type moves: list of int
        :return: A tuple containing lists of rewards, done flags, and draw statuses for each environment.
        :rtype: tuple of (list of float, list of bool, list of bool or None)
        """
        cdef int i
        cdef int move

        rewards = []
        dones = []
        is_draws = []

        for i, move in enumerate(moves):
            reward, done, is_draw = self.envs[i].step(move)
            rewards.append(reward)
            dones.append(done)
            is_draws.append(is_draw)
            if done:
                self.envs[i].reset()
        
        return rewards, dones, is_draws
