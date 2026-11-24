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


_PAIR_SCORES = (0.0, 0.25, 0.5, 0.75, 1.0)


def _expected_scores(elo):
    """Return the expected scores for both sides, computing the small tail directly."""
    if not math.isfinite(elo):
        raise ValueError('SPRT Elo hypotheses must be finite')
    odds = 10.0 ** (-abs(elo) / 400.0)
    low, high = odds / (1.0 + odds), 1.0 / (1.0 + odds)
    return (low, high) if elo < 0 else (high, low)


def _mle_log_denominators(counts, target, complement):
    """Fit p_i = observed_p_i / (1 + lambda * (score_i - target))."""
    if target > 0.5:
        # Reflect the score bins to fit the smaller tail, then restore their order.
        return _mle_log_denominators(counts[::-1], complement, target)[::-1]
    deviation = math.fsum(n * (a - target) for n, a in zip(counts, _PAIR_SCORES))
    if deviation == 0:
        return (0.0,) * len(counts)

    # At lambda=0 all denominators are 1. At the pole in the direction of
    # the root (1/target or -1/(1-target)), one denominator is zero.
    if deviation > 0:
        at_pole = [a / target for a in _PAIR_SCORES]
    else:
        at_pole = [(1.0 - a) / complement for a in _PAIR_SCORES]

    # Bisect on the fraction of the distance back from the pole to zero.
    # Forming the denominators this way avoids cancellation near the pole.
    lo, hi = 0.0, 1.0
    while True:
        fraction = (lo + hi) / 2.0
        denominators = [fraction + (1.0 - fraction) * d for d in at_pole]
        residual = math.fsum(n * ((d - 1.0) / fitted)
                             for n, d, fitted in zip(counts, at_pole, denominators))
        if residual == 0 or fraction == lo or fraction == hi:
            return tuple(math.log(d) for d in denominators)
        if residual < 0:
            lo = fraction
        else:
            hi = fraction


class SPRT:
    """Pentanomial SPRT with logistic Elo hypotheses and fixed Wald bounds.

    A sample is the tested engine's average score over a color-swapped pair.
    The first decision freezes the LLR and the observed counts.
    """

    def __init__(self, elo0: float, elo1: float, alpha: float = 0.05, beta: float = 0.05):
        self._scores0 = _expected_scores(elo0)
        self._scores1 = _expected_scores(elo1)
        if not 0 < self._scores0[0] < self._scores1[0] < 1:
            raise ValueError('SPRT hypotheses must yield distinct increasing scores between 0 and 1')
        if not all(math.isfinite(1.0 / s) for s in (*self._scores0, *self._scores1)):
            raise ValueError('SPRT hypotheses must yield finite MLE search bounds')
        if not (0 < alpha < 1 and 0 < beta < 1 and alpha + beta < 1):
            raise ValueError('SPRT requires positive alpha and beta with alpha + beta < 1')

        self._lower = math.log(beta) - math.log1p(-alpha)
        self._upper = math.log1p(-beta) - math.log(alpha)
        self._counts = [0] * len(_PAIR_SCORES)
        self._llr = 0.0
        self._result = None

    @property
    def counts(self) -> tuple[int, ...]:
        """Observed pair counts in ascending score order, without pseudocounts."""
        return tuple(self._counts)

    def add_pair(self, score: float) -> None:
        if score not in _PAIR_SCORES:
            raise ValueError('pair score must be 0, 0.25, 0.5, 0.75, or 1')
        if self._result is not None:
            return
        self._counts[_PAIR_SCORES.index(score)] += 1
        counts = [n or 0.001 for n in self._counts]
        log_d0 = _mle_log_denominators(counts, *self._scores0)
        log_d1 = _mle_log_denominators(counts, *self._scores1)
        self._llr = math.fsum(n * (d0 - d1) for n, d0, d1 in zip(counts, log_d0, log_d1))
        if self._llr <= self._lower:
            self._result = 'H0'
        elif self._llr >= self._upper:
            self._result = 'H1'

    def llr(self) -> float:
        return self._llr

    def bounds(self) -> tuple[float, float]:
        return self._lower, self._upper

    def status(self) -> str | None:
        return self._result
