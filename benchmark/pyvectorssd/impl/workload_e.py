from typing import List
from impl.arrival_time_gen import DistributionType
from impl.workload_base import BaseWorkloadGenerator, OperationType, WorkloadOperation
import math


class WorkloadE(BaseWorkloadGenerator):
    """Workload E generates a total of N operations:
    The index build will be triggered one time after 50% insertions.

    The remaining (N - 1) operations consists of Vector Insertion and Search along with the r, which is a ratio of each operation.
    If the r is a ratio of Vector Insertion, then the following makes sence:
    - The ((N - 1) * r) operations: Vector Insertion, where the r is a ratio
    - The ((N - 1) * (1 - r)) operations: Vector Search
    """

    def generate(self, **params) -> List[WorkloadOperation]:
        n_requests = params["req"]
        n_builds = 1
        n_effective_requests = n_requests - n_builds

        n_inserts = int(n_effective_requests * 0.7)
        n_searchs = n_effective_requests - n_inserts


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

        insert_ops = [op for op in operations if op.op_type == OperationType.VECTOR_INSERT]
        if insert_ops:
            insert_ops.sort(key=lambda x: x.arrival_time)
            # 50% insertion
            mid_idx = math.ceil(len(insert_ops) * 0.5) - 1
            build_time = insert_ops[mid_idx].arrival_time + 1e-6
            operations.append(WorkloadOperation(OperationType.INDEX_BUILD, build_time))
        else:
            pass

        return sorted(operations, key=lambda x: x.arrival_time)
