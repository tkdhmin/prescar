# PRESCAR: Preemptive and SLO-Aware Scheduling for ISP-based Vector Search with Online Ingestion

## Overview
Vector database management systems (VDBMSs) increasingly adopt in-situ processing (ISP) to reduce host resource consumption and data movement. While recent ISP-based VDBMSs successfully offload vector search to storage, they assume an offline data ingestion model. This limits their applicability to modern data-intensive streaming platforms, where stale indices cause systems to miss recent critical data and violate freshness service-level objective (SLO). Conversely, supporting online ingestion introduces a trade-off between availability and query performance. Systems must either suspend queries to build indices, harming violates availability, or rely on brute-force scans, increasing query latency. This issue arises because time-consuming index maintenance during ingestion and latency-sensitive searches contend for limited storage resources. We present PRESCAR, an ISP-based VDBMSs that co-designs preemptive and SLO-aware scheduling with storage architecture for not only online ingestion but also SLO compliance. Our evaluation shows that PRESCAR effectively balances multiple SLOs while significantly reducing SLO violations compared to existing system.



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




## Benchmarks & experiments

The _benchmark_ directory contains API for communicating with NVMe SSD and its how to use.
In particular, PRESCAR provides user-friendly python-based API, which has been pybinded.

## License

This project is based on Cosmos+ OpenSSD, which is licensed under the GNU General Public License v3 (GPLv3).  
Therefore, this modified version is also distributed under the terms of the GPLv3.

See [LICENSE](./LICENSE) for more information.
