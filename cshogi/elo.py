import math

def elo_diff(p):
    return -400.0 * math.log10(1.0 / p - 1.0)

def erf_inv(x):
    pi = 3.1415926535897

    a = 8.0 * (pi - 3.0) / (3.0 * pi * (4.0 - pi))
    y = math.log(1.0 - x * x)
    z = 2.0 / (pi * a) + y / 2.0

    ret = math.sqrt(math.sqrt(z * z - y / a) - z)

    if x < 0.0:
        return -ret
    return ret

def phi_inv(p):
    return math.sqrt(2.0) * erf_inv(2.0 * p - 1.0)

class Elo:
    """A class for calculating Elo ratings based on wins, losses, and draws.

    :param wins: Number of wins.
    :param losses: Number of losses.
    :param draws: Number of draws.
    """

    def __init__(self, wins: int, losses: int, draws: int):
        self.wins = wins
        self.losses = losses
        self.draws = draws

        n = wins + losses + draws
        w = wins / n
        l = losses / n
        d = draws / n
        self.mu = w + d / 2.0

        dev_w = w * math.pow(1.0 - self.mu, 2.0)
        dev_l = l * math.pow(0.0 - self.mu, 2.0)
        dev_d = d * math.pow(0.5 - self.mu, 2.0)
        self.stdev = math.sqrt(dev_w + dev_l + dev_d) / math.sqrt(n)

    def point_ratio(self) -> float:
        """Calculates the point ratio.

        :return: The point ratio.
        """
        total = (self.wins + self.losses + self.draws) * 2
        return ((self.wins * 2) + self.draws) / total

    def draw_ratio(self) -> float:
        """Calculates the draw ratio.

        :return: The draw ratio.
        """
        n = self.wins + self.losses + self.draws
        return self.draws / n

    def diff(self) -> float:
        """Calculates the Elo difference.

        :return: The Elo difference.
        """
        return elo_diff(self.mu)

    def error_margin(self) -> float:
        """Calculates the error margin.

        :return: The error margin.
        """
        mu_min = self.mu + phi_inv(0.025) * self.stdev
        mu_max = self.mu + phi_inv(0.975) * self.stdev
        return (elo_diff(mu_max) - elo_diff(mu_min)) / 2.0

    def los(self) -> float:
        """Calculates the likelihood of superiority.

        :return: The likelihood of superiority, in percentage.
        """
        return 100 * (0.5 + 0.5 * math.erf((self.wins - self.losses) / math.sqrt(2.0 * (self.wins + self.losses))))
