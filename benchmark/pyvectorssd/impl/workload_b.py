from typing import List
from impl.arrival_time_gen import DistributionType
from impl.workload_base import BaseWorkloadGenerator, OperationType, WorkloadOperation


class WorkloadB(BaseWorkloadGenerator):
    """Workload B generates a total of N operations:
    - The N operations: Vector Search

    In this case, arrival times depending on distribution may be unimportant.
    """

    def generate(self, **params) -> List[WorkloadOperation]:
        n_requests = params["req"]
        top_k = params["top_k"]

        if n_requests < 1:
            raise ValueError("n_requests must be at least 1")

        operations = []
        dist_parms = {k: v for k, v in params.items() if k not in ["req", "distribution", "top_k"]}
        arrival_times = self.arrival_generator.generate_single_distribution(
            n_requests, DistributionType(params["distribution"]), **dist_parms
        )

        for arrival_time in arrival_times:
            query_vector = self._generate_query_vector()
            operations.append(
                WorkloadOperation(OperationType.VECTOR_SEARCH, arrival_time, vector=query_vector, top_k=top_k)
            )

        return operations
