# TROUBLESHOOTING.md

## COSMOS + OpenSSD Firmware Flash Issues (JTAG / FPGA)

This document covers common errors encountered when running flashing operation (`sudo ./setup.sh vector1 run`) after building COSMOS + OpenSSD firmware code, especially failures during FPGA configuration via JTAG.

---

## Common Error

```
could not find configuration request.
aborting, 1 pending requests...
```

Often appears during:

```
fpga -file $bitstream_path
```

---

## What This Means

The failure occurs **before firmware flashing**, during the **FPGA bitstream download stage**.

The system could not properly detect or communicate with the FPGA over JTAG.

---

## Typical Causes

### 1. JTAG Target Not Detected

* FPGA does not appear in the JTAG chain
* Most common issue

---

### 2. `hw_server` Stuck or Conflicting Session

* Port `tcp:127.0.0.1:3121` not responding correctly
* Multiple sessions interfering

---

### 3. JTAG Driver Issues

* Cable is recognized but FPGA is not
* Missing or misconfigured drivers
---

### 4. FPGA Power / Reset State

* Board is powered off
* FPGA held in reset
---

### 5. JTAG Already in Use

* Vivado Hardware Manager or another tool is connected
---

### 6. Incorrect Initialization Order in Script
* FPGA not initialized before programming
* COSMOS script sequence broken
---

## Step-by-Step Debugging

### Step 1: Reboot the Cosmos+ OpenSSD
Power-off and -on board again

### Step 1: Verify JTAG Connection via XSDB

```bash
xsdb
connect
targets
```

Expected:

* FPGA device appears in the list

If NOT:

* Check cable, power, and drivers

---

### Step 2: Test with Vivado (Manual Check)

1. Open Vivado
2. Go to **Hardware Manager**
3. Click **Open Target → Auto Connect**
4. Program FPGA manually with bitstream

If this fails:
* Issue is NOT related to COSMOS or firmware

---

### Step 3: Re-run COSMOS Flash

```bash
sudo ./setup.sh vector1 run
```

Try to run above command several times.

---



## Final Takeaway

> If the FPGA is not visible over JTAG, firmware flashing will never start.

Fix JTAG connectivity first.
