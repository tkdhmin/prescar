import math
from typing import List
from impl.arrival_time_gen import DistributionType
from impl.workload_base import BaseWorkloadGenerator, OperationType, WorkloadOperation


class WorkloadC(BaseWorkloadGenerator):
    """Workload C generates a total of N operations:
    - The first (N - 1) operations: Vector Search
    - One operation: Index Build after Vector Search x%

    In this case, arrival times depending on distribution may be unimportant.
    """

    def generate(self, **params) -> List[WorkloadOperation]:
        n_requests = params["req"]
        top_k = params.get("top_k_search", 10)

        dist_parms = {k: v for k, v in params.items() if k not in ["req", "distribution", "top_k"]}
        arrival_times = self.arrival_generator.generate_single_distribution(
            n_requests, DistributionType(params["distribution"]), **dist_parms
        )

        operations = []
        for arrival_time in arrival_times:
            query_vector = self._generate_vector()
            operations.append(
                WorkloadOperation(OperationType.VECTOR_SEARCH, arrival_time, query_vector, top_k=top_k)
            )
        if operations:
            operations.sort(key=lambda x: x.arrival_time)

            x = params.get("build_trigger_ratio", 0.5)  # default 50%

            if not (0 < x <= 1):
                raise ValueError(f"Invalid build trigger ratio: {x}")

            trigger_idx = math.ceil(len(operations) * x) - 1
            trigger_idx = min(trigger_idx, len(operations) - 1)

            build_time = operations[trigger_idx].arrival_time + 1e-6
            operations.append(WorkloadOperation(OperationType.INDEX_BUILD, build_time))

        return sorted(operations, key=lambda x: x.arrival_time)
