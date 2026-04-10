from typing import List
from impl.arrival_time_gen import DistributionType
from impl.workload_base import BaseWorkloadGenerator, OperationType, WorkloadOperation


class WorkloadC(BaseWorkloadGenerator):
    """Workload C generates a total of N operations:
    The index build would be triggered k times regularly over the time.

    The remaining (N - k) = M operations consists of Vector Insertion and Search along with the r, which is a ratio of each operation.
    If the r is a ratio of Vector Insertion, then the following makes sence:
    - The (M * r) operations: Vector Insertion, where the r is a ratio
    - The (M * (1 - r)) operations: Vector Search
    """

    def generate(self, **params) -> List[WorkloadOperation]:
        n_requests = params["req"]
        index_build_period = params.get("build_period", 0.1)
        n_builds = max(1, int(n_requests / (index_build_period * n_requests)))
        n_effective_requests = n_requests - n_builds

        n_inserts = int(n_effective_requests * params["portion_insert"])
        n_searchs = n_effective_requests - n_inserts

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

        insert_ops = [op for op in operations if op.op_type == OperationType.VECTOR_INSERT]
        if insert_ops:
            insert_ops.sort(key=lambda x: x.arrival_time)
            ops_per_build = max(1, len(insert_ops) / n_builds)

            for i in range(n_builds):
                build_index = int(min((i + 1) * ops_per_build - 1, len(insert_ops) - 1))
                build_time = insert_ops[build_index].arrival_time + 0.1
                operations.append(WorkloadOperation(OperationType.INDEX_BUILD, build_time))
        else:
            pass

        return sorted(operations, key=lambda x: x.arrival_time)
