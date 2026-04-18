from abc import ABC, abstractmethod
from typing import List, Tuple

import numpy as np
from impl.arrival_time_gen import ArrivalTimeGenerator
from impl.supported_config import CheckableEnum


class OperationType(CheckableEnum):
    VECTOR_INSERT = "VECTOR_INSERT"
    VECTOR_SEARCH = "VECTOR_SEARCH"
    INDEX_BUILD = "INDEX_BUILD"
    RESUME_BUILD = "RESUME_BUILD"
    PAUSE_BUILD = "PAUSE_BUILD"


class WorkloadOperation:
    def __init__(
        self, op_type: OperationType, arrival_time: float, key: str = None, vector: np.ndarray = None, top_k: int = None
    ):
        self.op_type = op_type
        self.arrival_time = arrival_time
        self.key = key
        self.vector = vector
        self.top_k = top_k

    def __repr__(self):
        return f"WorkloadOperation({self.op_type.value}, t={self.arrival_time:.3f}, key={self.key}, vector is emitted, top_k={self.top_k})"


class BaseWorkloadGenerator(ABC):
    """Abstract class for workload generator"""

    def __init__(self, embeddings, metadata, query_embeddings, query_metadata, seed: int = None):
        self.arrival_generator = ArrivalTimeGenerator()
        self.csv_row_counter = 0
        self.query_csv_row_counter = 0
        self.embeddings = embeddings
        self.metadata = metadata
        self.query_embeddings = query_embeddings
        self.query_metadata = query_metadata
        self.seed = seed
        if seed is not None:
            np.random.seed(seed)

    @abstractmethod
    def generate(self, **params) -> List[WorkloadOperation]:
        pass

    def _generate_kv(self) -> Tuple[str, np.ndarray]:
        k = self._generate_key()
        v = self._generate_vector()
        self.csv_row_counter += 1
        return (k, v)

    def _generate_vector(self) -> np.ndarray:
        return self.embeddings[self.csv_row_counter]

    def _generate_key(self) -> str:
        return str(self.metadata.iloc[self.csv_row_counter]["vid"])
    
    def _generate_query_vector(self) -> np.ndarray:
        self.query_csv_row_counter += 1
        return self.query_embeddings[self.query_csv_row_counter - 1]
