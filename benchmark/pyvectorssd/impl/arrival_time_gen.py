from typing import Callable, List, Tuple
import numpy as np
from impl.supported_config import CheckableEnum


class DistributionType(CheckableEnum):
    ZIPF = "zipf"
    NORMAL = "normal"
    UNIFORM = "uniform"
    LOGNORMAL = "lognormal"
    POISSON = "poisson"


class ArrivalTimeGenerator:
    """Arrival time generator supporting various distributions"""

    def __init__(self, base_rate: float = 10.0):
        self.base_rate = base_rate

    def _get_rate_function(self, distribution: DistributionType, **params):
        if distribution == DistributionType.ZIPF:
            a = params.get("a", 1.1)
            return lambda: np.random.zipf(a) * 0.5
        elif distribution == DistributionType.NORMAL:
            mu, sigma = params.get("mu", 0.0), params.get("sigma", 1.0)
            return lambda: max(0.1, np.random.normal(mu, sigma))
        elif distribution == DistributionType.UNIFORM:
            rate_min, rate_max = params.get("rate_min", 0.5), params.get("rate_max", 2.0)
            return lambda: np.random.uniform(rate_min, rate_max)
        elif distribution == DistributionType.LOGNORMAL:
            mu, sigma = params.get("mu", 0), params.get("sigma", 1)
            return lambda: np.random.lognormal(mu, sigma)
        elif distribution == DistributionType.POISSON:
            lam = params.get("lam", 5)
            return lambda: max(0.1, np.random.poisson(lam))
        else:
            raise ValueError(f"Unsupported distribution: {distribution}")

    def _generate_sequential_arrivals(self, n_ops: int, rate_multiplier: Callable) -> np.ndarray:
        arrival_times = []
        current_time = 0.0

        for _ in range(n_ops):
            current_rate = self.base_rate * rate_multiplier()
            inter_arrival = np.random.exponential(1.0 / current_rate)
            current_time += inter_arrival
            arrival_times.append(current_time)

        return np.array(arrival_times)

    def _get_next_arrival_time(self, current_time: float, rate_func) -> float:
        rate_multiplier = rate_func()
        current_rate = self.base_rate * rate_multiplier
        inter_arrival = np.random.exponential(1.0 / current_rate)
        return current_time + inter_arrival

    def generate_single_distribution(self, n_ops: int, distribution: DistributionType, **params) -> np.ndarray:
        rate_func = self._get_rate_function(distribution, **params)
        return self._generate_sequential_arrivals(n_ops, rate_func)

    def generate_mixed_distributions(
        self, n_ops1: int, dist1: DistributionType, params1: dict, n_ops2: int, dist2: DistributionType, params2: dict
    ) -> List[Tuple[str, float]]:
        rate_func1 = self._get_rate_function(dist1, **params1)
        rate_func2 = self._get_rate_function(dist2, **params2)

        events = []
        time1, time2 = 0.0, 0.0
        count1, count2 = 0, 0

        while count1 < n_ops1 or count2 < n_ops2:
            next_time1 = self._get_next_arrival_time(time1, rate_func1) if count1 < n_ops1 else float("inf")
            next_time2 = self._get_next_arrival_time(time2, rate_func2) if count2 < n_ops2 else float("inf")

            if next_time1 <= next_time2:
                events.append(("type1", next_time1))
                time1 = next_time1
                count1 += 1
            else:
                events.append(("type2", next_time2))
                time2 = next_time2
                count2 += 1

        return events
