import argparse
import csv
import json
import logging
import sys

sys.path.append("../build")

import vectorssd
import numpy as np
from impl.workload_a import WorkloadA
from impl.workload_b import WorkloadB
from impl.workload_c import WorkloadC
from impl.workload_d import WorkloadD
from impl.workload_e import WorkloadE
from impl.workload_base import BaseWorkloadGenerator, OperationType, WorkloadOperation
from impl.supported_config import SupportedConfiguration
from typing import Dict, List


logging.basicConfig(level=logging.INFO, format="%(asctime)s - %(name)s - %(levelname)s - %(message)s")
logger = logging.getLogger(__name__)

COSMOS_PLUS_OPENSSD_ENABLE: bool = True


class WorkloadFactory:
    @staticmethod
    def create_generator(workload_type: str, seed: int = None, dataset: list = [], **kwargs) -> BaseWorkloadGenerator:
        generators = {"a": WorkloadA, "b": WorkloadB, "c": WorkloadC, "d": WorkloadD, "e": WorkloadE}

        if workload_type not in generators:
            raise ValueError(f"Unknown workload type: {workload_type}")
        return generators[workload_type](seed=seed, dataset=dataset, **kwargs)


class DemoConfigurator:
    def __init__(self, config_path: str = None, dataset: str = None):
        self.config_path = config_path
        self.dataset_path = dataset

    def load(self) -> None:
        self.config: Dict[str, str] = self._config_load()
        self.dataset: List[dict] = self._dataset_load()

    def _config_load(self) -> dict:
        """Load and validate config file, return as dictionary."""
        try:
            with open(self.config_path, "r") as f:
                config = json.load(f)
                logger.info("The config json file has been loaded successfully.")
                for k, _ in config.items():
                    if k not in SupportedConfiguration.list():
                        raise AssertionError(f"Not supported field: {k}")
                return config
        except Exception as e:
            raise AssertionError(f"Error while loading config file: {e}")

    def _dataset_load(self) -> list:
        dataset = []
        try:
            with open(self.dataset_path, newline="") as csvfile:
                reader = csv.DictReader(csvfile, delimiter="\t")
                for row in reader:
                    op_type = row["op_type"]
                    vid = int(row["vid"])
                    vector = np.array([float(row[str(i)]) for i in range(len(row) - 2)])
                    dataset.append({"op_type": op_type, "vid": vid, "vector": vector})
        except Exception as e:
            raise AssertionError(f"Error while loading csv dataset file: {e}")
        return dataset


def demo_workload(config: Dict[str, str], dataset: list, target_workload_type: str) -> None:
    vector_dim = config.get("dimension", None)
    scenarios = config.get("scenarios", {})
    collection_name = config.get("collection_name", "None")

    if vector_dim != len(dataset[0]["vector"]):
        raise ValueError(f"Different dimension: {vector_dim} vs {len(dataset[0]['vector'])}")

    for workload_type, config in scenarios.items():
        if workload_type is not target_workload_type:
            continue
        logger.info(f"--- Workload {workload_type.upper()} with {vector_dim} ---")

        generator = WorkloadFactory.create_generator(workload_type, vector_dim=vector_dim, dataset=dataset, seed=45)
        operations = generator.generate(**config)
        if COSMOS_PLUS_OPENSSD_ENABLE:
            run_vectorssd_demo(collection_name, operations, vector_dim)
        else:
            run_virtual_demo(collection_name, operations, vector_dim)


def run_vectorssd_demo(collection_name: str, operations: List[WorkloadOperation], vector_dim) -> None:
    mydb = vectorssd.DB()
    _ = mydb.open("/dev/ng1n1", collection_name)
    for op in operations:
        if op.op_type == OperationType.VECTOR_INSERT:
            mydb.put(op.key, op.vector)
        elif op.op_type == OperationType.INDEX_BUILD:
            mydb.vector_build()
        elif op.op_type == OperationType.VECTOR_SEARCH:
            if op.top_k is None:
                raise ValueError(op.top_k)
            query_vector = np.random.randn(vector_dim).astype(np.float32)
            mydb.vector_search(query_vector, op.top_k)
        else:
            raise AssertionError("Not supported")

    mydb.close()


def run_virtual_demo(collection_name: str, operations: List[WorkloadOperation], vector_dim) -> None:
    mydb = vectorssd.DB()
    _ = mydb.open_mock("virtual_dev", collection_name)
    for op in operations:
        if op.op_type == OperationType.VECTOR_INSERT:
            mydb.put_mock(op.key, op.vector)
        elif op.op_type == OperationType.INDEX_BUILD:
            mydb.vector_build_mock()
        elif op.op_type == OperationType.VECTOR_SEARCH:
            if op.top_k is None:
                raise ValueError(op.top_k)
            query_vector = np.random.randn(vector_dim).astype(np.float32)
            mydb.vector_search_mock(query_vector, op.top_k)
        else:
            raise AssertionError("Not supported")

    mydb.close_mock()


def main():
    parser = argparse.ArgumentParser(description="VectorSSD's Realistic Workload Generator")
    parser.add_argument("--config", type=str, required=True, help="Json-typed file for configuration")
    parser.add_argument("--output_dir", type=str, default="./results", help="Results directory")
    parser.add_argument("--type", type=str, required=True, help="Specified workload scenario type for test")
    parser.add_argument("--dataset", type=str, help="Vector dataset for test")
    args = parser.parse_args()

    configurator = DemoConfigurator(args.config, args.dataset)
    configurator.load()
    demo_workload(configurator.config, configurator.dataset, args.type)


if __name__ == "__main__":
    main()
