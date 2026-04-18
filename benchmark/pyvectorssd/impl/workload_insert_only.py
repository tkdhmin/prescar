from typing import List
from impl.arrival_time_gen import DistributionType
from impl.workload_base import BaseWorkloadGenerator, OperationType, WorkloadOperation


class WorkloadInsertOnly(BaseWorkloadGenerator):
    """Workload-Insert-Only generates a total of N operations:
    - The first N operations: Vector Insertion

    In this case, arrival times depending on distribution may be unimportant.
    """

    def generate(self, **params) -> List[WorkloadOperation]:
        n_requests = params["req"]
        if n_requests < 1:
            raise ValueError("n_requests must be at least 1")

        operations = []
        if self.seed is None:
            raise AssertionError("The seed must be set.")

        dist_parms = {k: v for k, v in params.items() if k not in ["req", "distribution"]}

        arrival_time_of_insert = self.arrival_generator.generate_single_distribution(
            n_requests, DistributionType(params["distribution"]), **dist_parms
        )

        for arrival_time in arrival_time_of_insert:
            key, vector = self._generate_kv()
            operations.append(WorkloadOperation(OperationType.VECTOR_INSERT, arrival_time, key, vector))

        return sorted(operations, key=lambda x: x.arrival_time)
