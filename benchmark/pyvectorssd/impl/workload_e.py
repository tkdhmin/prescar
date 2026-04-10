from typing import List
from impl.arrival_time_gen import DistributionType
from impl.workload_base import BaseWorkloadGenerator, OperationType, WorkloadOperation


class WorkloadE(BaseWorkloadGenerator):
    """Workload E generates a total of N operations:
    - The (N * r) operations: Vector Insertion, where the r is a ratio of the Vector Insertion operation
    - The (N * (1 - r)) operations: Vector Search
    """

    def generate(self, **params) -> List[WorkloadOperation]:
        n_requests = params["req"]

        n_inserts = int(n_requests * params["portion_insert"])
        n_searchs = n_requests - n_inserts

        portion_insert = float(params.get("portion_insert", 0.0))
        portion_search = float(params.get("portion_search", 0.0))
        if portion_insert + portion_search != 1.0:
            raise ValueError(f"Total ratio must be one: {portion_insert}, {portion_search}")

        distribution_insert = DistributionType(params["distribution_insert"])
        distribution_search = DistributionType(params["distribution_search"])

        top_k = params.get("top_k_search", 10)

        insert_params = {
            k.replace("_insert", ""): v
            for k, v in params.items()
            if k.endswith("_insert") and k != "distribution_insert"
        }
        search_params = {
            k.replace("_search", ""): v
            for k, v in params.items()
            if k.endswith("_search") and k != "distribution_search"
        }

        mixed_events = self.arrival_generator.generate_mixed_distributions(
            n_inserts, distribution_insert, insert_params, n_searchs, distribution_search, search_params
        )

        operations = []
        for event_type, arrival_time in mixed_events:
            if event_type == "type1":  # INSERT
                key, vector = self._generate_kv()
                operations.append(WorkloadOperation(OperationType.VECTOR_INSERT, arrival_time, key, vector))
            elif event_type == "type2":  # SEARCH
                query_vector = self._generate_vector()
                operations.append(
                    WorkloadOperation(OperationType.VECTOR_SEARCH, arrival_time, query_vector, top_k=top_k)
                )

        return sorted(operations, key=lambda x: x.arrival_time)
