# Installation Guide

## Prerequisites

### Hardware
- Cosmos+ OpenSSD
- Host machine: x86_64, 128GB RAM (recommended)

### Software
- Ubuntu 20.04 LTS (Linux kernel 5.4.0-90)
- GCC 11.4.0
- Python 3.9
- Xilinx Vivado 2019.1 (for FPGA bitstream)

## Step 1: Host Environment Setup

```bash
# Install dependencies
sudo apt-get update
sudo apt-get install build-essential python3 python3-pip libtinfo5 tio
```


## Step 2: Xilinx SDK Download
Download Xilinx SDK (Vivado 2019.1) from the following link:
https://account.amd.com/en/forms/downloads/xef-vivado.html?filename=Xilinx_Vivado_SDK_2019.1_0524_1430.tar.gz

Note:
- Xilinx SDK is now part of AMD after the acquisition of Xilinx by AMD.

After downloading, extract the archive and run the installer:

```bash
sudo ./xsetup
```


## Step 3: How to install
Installations for PL (FPGA part) and PS (ARM Cortex-A9) are needed.

```bash
source /tools/Xilinx/SDK/2019.1/settings64.sh
xsct -nodisp
setws

createhw -name cosmos_hw -hwspec vector1/cosmos_hw/system.hdf
createbsp -name cosmos_bsp -hwproject cosmos_hw -proc ps7_cortexa9_0
createapp -name cosmos_app -hwproject cosmos_hw -bsp cosmos_bsp -proc ps7_cortexa9_0

projects -build -type bsp -name cosmos_bsp
projects -build -type app -name cosmos_app

(exit xsct)
```

### Step 4: Deployment

```bash
# Build fimrware code 
sudo ./setup.sh {project_folder} build
# Flash firmware to Cosmos+ OpenSSD
sudo ./setup.sh {project_folder} run
```

```bash
# Monitor program logs from UART on another terminal
sudo ./setup.sh {project_folder} show
```
