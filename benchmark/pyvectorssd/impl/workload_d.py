from typing import List
from impl.arrival_time_gen import DistributionType
from impl.workload_base import BaseWorkloadGenerator, OperationType, WorkloadOperation


class WorkloadD(BaseWorkloadGenerator):
    """Workload D generates a total of N operations:
    - The (N/2) operations: Vector Search
    - The (N/2) operations: Vector Insertion

    These two operations are separately issued.
    """

    def generate(self, **params) -> List[WorkloadOperation]:
        n_requests = params["req"]
        n_searchs = int(n_requests / 2)
        n_inserts = n_requests - n_searchs

        distribution_search = DistributionType(params["distribution_search"])
        distribution_insert = DistributionType(params["distribution_insert"])

        top_k = params.get("top_k_search", 10)

        search_params = {
            k.replace("_search", ""): v
            for k, v in params.items()
            if k.endswith("_search") and k != "distribution_search"
        }
        insert_params = {
            k.replace("_insert", ""): v
            for k, v in params.items()
            if k.endswith("_insert") and k != "distribution_insert"
        }

        arrival_times_of_search = self.arrival_generator.generate_single_distribution(
            n_searchs, distribution_search, **search_params
        )

        operations = []
        for arrival_time in arrival_times_of_search:
            query_vector = self._generate_vector()
            operations.append(WorkloadOperation(OperationType.VECTOR_SEARCH, arrival_time, query_vector, top_k=top_k))

        if arrival_times_of_search.size > 0:
            time_pad = arrival_times_of_search[-1] + 1.0  # Time adjustment
        else:
            time_pad = 0.0

        arrival_times_of_insert = self.arrival_generator.generate_single_distribution(
            n_inserts, distribution_insert, **insert_params
        )

        for arrival_time in arrival_times_of_insert:
            key, vector = self._generate_kv()
            operations.append(WorkloadOperation(OperationType.VECTOR_INSERT, arrival_time + time_pad, key, vector))

        return sorted(operations, key=lambda x: x.arrival_time)
