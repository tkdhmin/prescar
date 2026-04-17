# Experiment Reproduction Guide

This guide reproduces the our supported workloads that have been used for key results from Section VI of the paper.


## Experiments Overview

| Workload | Description |
|--------------|-------------|
| Workload A | Vector Insertion + Index Build at last|
| Workload B | Vector Search|
| Workload C | Vector Search + Index Build after *x*% Search|
| Workload D | Vector Insertion (50%) + Vector Search (50%)|
| Workload E | Vector Insertion (70%) + Vector Search (30%) + Index Build|


## Prerequisites

1. Complete installation (see INSTALL.md)
2. Datasets prepared
3. Cosmos+ OpenSSD available

### Dataset Preparation
The sample datasets are preloaded on `/home/dhmin/prescar/benchmark/dataset`.

External datasets can be downloaded from:
- [FIQA](https://huggingface.co/datasets/BeIR/fiqa)
- [VoxCeleb](https://www.robots.ox.ac.uk/~vgg/data/voxceleb/)
- [SIFT1M](http://corpus-texmex.irisa.fr/)
- [Quora (BEIR)](https://huggingface.co/datasets/BeIR/quora)

## Experimental Script Usage

### Commands
```bash
cd pyvectorssd
./run_expr.sh
```

### Expected Output