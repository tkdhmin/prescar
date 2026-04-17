# PRESCAR: Preemptive and SLO-Aware Scheduling for ISP-based Vector Search with Online Ingestion

## Overview
Vector database management systems (VDBMSs) increasingly adopt in-situ processing (ISP) to reduce host resource consumption and data movement. While recent ISP-based VDBMSs successfully offload vector search to storage, they assume an offline data ingestion model. This limits their applicability to modern data-intensive streaming platforms, where stale indices cause systems to miss recent critical data and violate freshness service-level objective (SLO). Conversely, supporting online ingestion introduces a trade-off between availability and query performance. Systems must either suspend queries to build indices, harming violates availability, or rely on brute-force scans, increasing query latency. This issue arises because time-consuming index maintenance during ingestion and latency-sensitive searches contend for limited storage resources. We present PRESCAR, an ISP-based VDBMSs that co-designs preemptive and SLO-aware scheduling with storage architecture for not only online ingestion but also SLO compliance. Our evaluation shows that PRESCAR effectively balances multiple SLOs while significantly reducing SLO violations compared to existing system.

[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.19624598.svg)](https://doi.org/10.5281/zenodo.19624598)


## Key Features

- **Preemption Support**: Enables preemption for optimal resource utilization in SSD architecture.
- **In-Storage Computing**: Reduces data movement overhead by performing computations in storage.
- **SLO Compliance**: Maintains strong SLO guarantees through SLO-ware scheduling.

## Repository layout
```
/benchmark       # benchmarking harness, workloads, experiment scripts
/cosmos_hw       # hardware-related code (Cosmos SSD prototype, if present)
/util            # utility scripts
/vector1         # cosmos_app (src)
LICENSE
README.md
```


## Requirements
- Linux (Ubuntu 20.04+ recommended)
- gcc/clang toolchain for C code
- Python 3.9+ with pip
- Cosmos+ OpenSSD board (hardware)
- Xilinx SDK

### How to download Xilinx SDK
Download xilinx and install https://account.amd.com/en/forms/downloads/xef-vivado.html?filename=Xilinx_Vivado_SDK_2019.1_0524_1430.tar.gz and `sudo ./xsetup ## install` (Probabably the license is not needed.)

### How to install
```bash
mkdir {project_folder}
cd  {project_folder}
source /tools/Xilinx/SDK/2019.1/settings64.sh
xsct -nodisp
setws

createhw -name cosmos_hw -hwspec {your_path}/OpenSSD2.hdf
createbsp -name cosmos_bsp -hwproject cosmos_hw -proc ps7_cortexa9_0
createapp -name cosmos_app -hwproject cosmos_hw -bsp cosmos_bsp -proc ps7_cortexa9_0

projects -build -type bsp -name cosmos_bsp
projects -build -type app -name cosmos_app

(exit xsct)
```

## Quick start (After SDK is ready)
```bash
sudo ./setup.sh {project_folder} build
sudo ./setup.sh {project_folder} run
sudo ./setup.sh {project_folder} show
```

## Benchmarks & experiments

The _benchmark_ directory contains API for communicating with NVMe SSD and its how to use.
In particular, PRESCAR provides user-friendly python-based API, which has been pybinded.

## License

This project is based on Cosmos+ OpenSSD, which is licensed under the GNU General Public License v3 (GPLv3).  
Therefore, this modified version is also distributed under the terms of the GPLv3.

See [LICENSE](./LICENSE) for more information.
