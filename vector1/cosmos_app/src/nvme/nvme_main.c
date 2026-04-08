//////////////////////////////////////////////////////////////////////////////////
// nvme_main.c for Cosmos+ OpenSSD
// Copyright (c) 2016 Hanyang University ENC Lab.
// Contributed by Yong Ho Song <yhsong@enc.hanyang.ac.kr> Youngjin Jo
// <yjjo@enc.hanyang.ac.kr> Sangjin Lee <sjlee@enc.hanyang.ac.kr> Jaewook Kwak
// <jwkwak@enc.hanyang.ac.kr> Kibin Park <kbpark@enc.hanyang.ac.kr>
//
// This file is part of Cosmos+ OpenSSD.
//
// Cosmos+ OpenSSD is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// Cosmos+ OpenSSD is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Cosmos+ OpenSSD; see the file COPYING.
// If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Company: ENC Lab. <http://enc.hanyang.ac.kr>
// Engineer: Sangjin Lee <sjlee@enc.hanyang.ac.kr> Jaewook Kwak
// <jwkwak@enc.hanyang.ac.kr> Kibin Park <kbpark@enc.hanyang.ac.kr>
//
// Project Name: Cosmos+ OpenSSD
// Design Name: Cosmos+ Firmware
// Module Name: NVMe Main
// File Name: nvme_main.c
//
// Version: v1.2.0
//
// Description:
//   - initializes FTL and NAND
//   - handles NVMe controller
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.2.0
//   - header file for buffer is changed from "ia_lru_buffer.h" to
//   "lru_buffer.h"
//   - Low level scheduler execution is allowed when there is no i/o command
//
// * v1.1.0
//   - DMA status initialization is added
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////

#include "nvme_main.h"

#include "../memory_map.h"
#include "../gs/growingsegment.h"
#include "../ss/sealedsegment.h"
#include "../ss/super.h"
#include "debug.h"
#include "host_lld.h"
#include "io_access.h"
#include "nvme.h"
#include "nvme_admin_cmd.h"
#include "nvme_io_cmd.h"
#include "xil_printf.h"

volatile NVME_CONTEXT g_nvmeTask;
static unsigned int g_inprogess_index_build;
static unsigned int g_index_build_resume_point;
extern int build_paused;
extern int preempted;
extern int searchActivated;
static SKIPLIST_HEAD* skiphead = (SKIPLIST_HEAD*)GROWING_SEGMENT_HEAD_ADDR;
static SUPER_LEVEL_INFO* super_level_info = (SUPER_LEVEL_INFO*)SUPER_BLOCK_ADDR;
static SUPER_SEALED_SEGMENT_LIST* super_ss_list = (SUPER_SEALED_SEGMENT_LIST*)SUPER_SEALED_SEGMENT_LIST_ADDR;

AUTO_CPL_INFO auto_cpl_info[MAX_NUM_NVME_SLOT];

void checkCacheCoherency() {
  xil_printf("=== Cache Coherency Analysis ===\r\n");
#define UNCACHED_REGION_START_MB (COMPACTION_BUFFER_ADDR / MB)
#define UNCACHED_REGION_END_MB (DATA_BUFFER_MAP_ADDR / MB)
#define MB (1024*1024)

  unsigned int reserved1_mb = RESERVED1_START_ADDR / (1024 * 1024);
  unsigned int vector_index_mb = VECTOR_INDEX_START_ADDR / (1024 * 1024);
  unsigned int hnsw_entry_mb = HNSW_ONDEMAND_ENTRY_START_ADDR / (1024 * 1024);

  xil_printf("RESERVED1_START_ADDR: 0x%08X (%uMB)\r\n", RESERVED1_START_ADDR, reserved1_mb);
  xil_printf("VECTOR_INDEX_START_ADDR: 0x%08X (%uMB)\r\n", VECTOR_INDEX_START_ADDR, vector_index_mb);
  xil_printf("HNSW_ONDEMAND_ENTRY_START_ADDR: 0x%08X (%uMB)\r\n", HNSW_ONDEMAND_ENTRY_START_ADDR, hnsw_entry_mb);

  if (hnsw_entry_mb >= UNCACHED_REGION_START_MB && hnsw_entry_mb <= UNCACHED_REGION_END_MB) {
    xil_printf("[PASS] HNSW Entry: In DMA-safe uncached region (%u MB)\r\n", hnsw_entry_mb);
  }
  else {
    xil_printf("[FAIL] HNSW Entry: Not in DMA-safe uncached region (%u MB)\r\n", hnsw_entry_mb);
  }

  xil_printf("\n=== DMA Access Pattern ===\r\n");
  xil_printf("1. HNSWOndemandEntry read/write via DMA\r\n");
  xil_printf("2. VectorIndex access via CPU\r\n");
  xil_printf("3. Vector data access via CPU\r\n");
}

void checkDataBufferMapping() {
  xil_printf("=== DATA_BUFFER TLB Check ===\r\n");
  xil_printf("DATA_BUFFER_BASE_ADDR: 0x%08X\r\n", DATA_BUFFER_BASE_ADDR);

  unsigned int bufferMB = DATA_BUFFER_BASE_ADDR / (1024 * 1024);
  xil_printf("DATA_BUFFER at %u MB\r\n", bufferMB);

  if (bufferMB >= 256 && bufferMB < 384) {
    xil_printf("   DATA_BUFFER is CACHED - Cache coherency issues likely!\r\n");
    xil_printf("   Solution: Add cache invalidation after DMA\r\n");
  }
  else if (bufferMB >= 384) {
    xil_printf("   DATA_BUFFER is UNCACHED - No cache issues\r\n");
  }
}

void checkAlignment() {
  xil_printf("=== Alignment Check ===\r\n");

  if (COMPACTION_BUFFER_ADDR % BYTES_PER_DATA_REGION_OF_SLICE != 0) {
    xil_printf("WARNING: COMPACTION_BUFFER_ADDR not 16KB aligned!\r\n");
  }

  for (int i = 0; i < MAX_SEALED_LEVEL0; i++) {
    unsigned int index_addr = COMPACTION_BUFFER_ADDR_INDEX_ONDEMAND(i);
    unsigned int data_addr = COMPACTION_BUFFER_ADDR_DATA_ONDEMAND(i);

    if (index_addr % BYTES_PER_DATA_REGION_OF_SLICE != 0) {
      xil_printf("WARNING: INDEX[%d] not aligned!\r\n", i);
    }
    if (data_addr % BYTES_PER_DATA_REGION_OF_SLICE != 0) {
      xil_printf("WARNING: DATA[%d] not aligned!\r\n", i);
    }
  }
}

void checkMemoryOverlap() {
  xil_printf("=== Memory Overlap Check ===\r\n");

  unsigned int vector_start = VECTOR_INDEX_START_ADDR;
  unsigned int vector_end = VECTOR_INDEX_START_ADDR + sizeof(VectorIndex) - 1;
  unsigned int compaction_start = COMPACTION_BUFFER_ADDR;
  unsigned int compaction_end = COMPACTION_BUFFER_ADDR + COMPACTION_BUFFER_ADDR_SIZE_ONDEMAND - 1;

  xil_printf("Vector Index: 0x%08X ~ 0x%08X\r\n", vector_start, vector_end);
  xil_printf("Compaction:   0x%08X ~ 0x%08X\r\n", compaction_start, compaction_end);

  if ((vector_start <= compaction_end) && (compaction_start <= vector_end)) {
    xil_printf("ERROR: Memory overlap detected!\r\n");
  }
  else {
    xil_printf("=> No overlap\r\n");
  }

  unsigned int hnsw_start = HNSW_ONDEMAND_ENTRY_START_ADDR;
  unsigned int hnsw_end = hnsw_start + HNSW_ONDEMAND_ENTRY_SIZE - 1;

  xil_printf("HNSW Entry:   0x%08X ~ 0x%08X\r\n", hnsw_start, hnsw_end);

  if ((hnsw_start <= compaction_end) && (compaction_start <= hnsw_end)) {
    xil_printf("ERROR: HNSW overlap detected!\r\n");
  }
  else {
    xil_printf("=> No HNSW overlap\r\n");
  }
}

int tryIndexBuildIncremental(int resumePoint, int maxNodes) {
  if (HAS_NO_SS_INDEX_TARGET() || targetSsForIndexing->total_entry == 0)
    return;

  static int currentIndex = 0;

  if (resumePoint == 0) {
    currentIndex = 0;
  }
  else {
    currentIndex = resumePoint;
  }

  int endIndex = currentIndex + maxNodes;
  if (endIndex >= targetSsForIndexing->total_entry) {
    endIndex = targetSsForIndexing->total_entry - 1;
  }

  // Incrementally range-based build
  int result = buildOndemandHnsw(targetSsForIndexing, &reverse_write_pointer_lpn, currentIndex, endIndex, preempted);

  if (result == -1) { // retry after flushing data buffer
    while (sliceReqQ.reqCnt > 0) {
      ReqTransSliceToLowLevel();
    }
    SchedulingNandReq();
    SyncAllLowLevelReqDone();
    for (int i = 0; i < AVAILABLE_DATA_BUFFER_ENTRY_COUNT; i++) {
      unsigned int* dataAddr = (unsigned int*)dataBufEntry2DramAddr(i);
      memset(dataAddr, 0, BYTES_PER_DATA_REGION_OF_SLICE);
      Xil_DCacheInvalidateRange((INTPTR)dataAddr, BYTES_PER_DATA_REGION_OF_SLICE);
    }
    result = buildOndemandHnsw(targetSsForIndexing, &reverse_write_pointer_lpn, currentIndex, endIndex, preempted);
  }
  else if (result == 0) {
    return 0;
  }
  else {
    return endIndex + 1;  // Next resume point
  }
}

void nvme_main() {
  unsigned int exeLlr;
  unsigned int rstCnt = 0;

  cur_vector_seq = 0;
  shouldDelayCompaction = 0;

  /* Initialize index building flag and build target */
  g_inprogess_index_build = 0;
  g_index_build_resume_point = 0;
  numVectorSearch = 0;
  preempted = 0;
  build_paused = 0;
  searchActivated = 0;
  CLEAR_INDEX_BUILDING();
  CLEAR_SS_INDEX_TARGET();
  CLEAR_SS_SEARCH_TARGET();
  CLEAR_ONDEMAND_FIELD_INDICATOR();

  CLEAR_NEXT_SEGMENT_ID();

  raw_vector_size = 0;
  printMemoryLayout();
  /*
  AVAILABLE_AREA_1_SIZE : 151721723 B (144.69MB)
  VECTOR_INDEX_START_ADDR 925869560
  QUERY_VECTOR_BUFFER_START_ADDR 926181380
  TOP_K_CURRENT_DISTANCE_BUFFER 926197764
  RESERVED_DATA_BUFFER_BASE_ADDR 277487616
  RESERVED_DATA_BUFFER_SIZE 108388352
  DATA_BUFFER_MAP_SIZE 10240
  DATA_BUFFER_MAP_ADDR 402653184
  REQ_POOL_ADDR 675334912

  */

  xil_printf("!!! Wait until FTL reset complete !!! \r\n");

  InitFTL();

  initSkipList(skiphead);

  xil_printf("write_pointer_lba : 0x%08x, reverse_write_pointer_lba : %u\n", write_pointer_lba, reverse_write_pointer_lpn);


  // Initialize Skiplist
  skiphead->st_lba = write_pointer_lba;

  /*
   * Superblock Initialization
   * 1. Initialize Version Layout Info Free Pool
   * 2. Initialize each level list
   * */
  xil_printf("SUPER_SEALED_SEGMENT_LIST_ADDR = 0x%08X\r\n", SUPER_SEALED_SEGMENT_LIST_ADDR);
  for (int i = 0; i < 4; i++) super_level_info->level_count[i] = 0;

  for (int i = 0; i < 4; i++) {
    xil_printf("&super_ss_list[%d] = 0x%08X\r\n", i, &super_ss_list[i]);
    super_ss_list[i].head = 0;
    super_ss_list[i].tail = 0;
  }

  for (int i = 0; i < MAX_NUM_NVME_SLOT; i++) {
    auto_cpl_info[i].mark = 0;
    // auto_cpl_info[i].sqid = 0;
    // auto_cpl_info[i].cid = 0;
    auto_cpl_info[i].numOfNvmeBlock = 0;
    auto_cpl_info[i].sentNvmeBlock = 0;
    auto_cpl_info[i].kv_length = 0;
  }

  dev_irq_init();


  PRINT_VAR(RESERVED0_START_ADDR);
  PRINT_VAR(CP_INDEX_BUFFER1_ADDR);
  PRINT_VAR(CP_DATA_BUFFER1_ADDR);

  PRINT_VAR(GROWING_SEGMENT_HEAD_ADDR);
  PRINT_VAR(SUPER_BLOCK_ADDR);
  PRINT_VAR(SUPER_SEALED_SEGMENT_LIST3_END_ADDR);
  PRINT_VAR(RESERVED0_END_ADDR);
  PRINT_VAR(FTL_MANAGEMENT_START_ADDR);

  PRINT_VAR(QUERY_VECTOR_BUFFER_START_ADDR);
  PRINT_VAR(TOP_K_CURRENT_DISTANCE_BUFFER);
  PRINT_VAR(RESERVED_DATA_BUFFER_BASE_ADDR + RESERVED_DATA_BUFFER_SIZE);

  PRINT_VAR(COMPLETE_FLAG_TABLE_ADDR);
  PRINT_VAR(SEALED_SEGMENT_INDEX_BUFFER);
  PRINT_VAR(SEALED_SEGMENT_DATA_BUFFER);
  PRINT_VAR(VECTOR_INDEX_START_ADDR);
  PRINT_VAR(HNSW_ONDEMAND_ENTRY_START_ADDR);
  PRINT_VAR(HNSW_ONDEMAND_END_ADDR);
  PRINT_VAR(DATA_BUFFER_MAP_ADDR);
  PRINT_VAR(REQ_POOL_ADDR);
  PRINT_VAR(RESERVED1_START_ADDR);
  PRINT_VAR(DRAM_END_ADDR);
  checkMemoryOverlap();
  checkCacheCoherency();
  checkDataBufferMapping();

  xil_printf("Structure size verification:\r\n");
  xil_printf("  HNSWNode size: %u (expected: 114-116)\r\n", sizeof(HNSWNode));
  xil_printf("  HNSWOndemandEntry size: %u (expected: %u)\r\n",
    sizeof(HNSWOndemandEntry), BYTES_PER_DATA_REGION_OF_SLICE);
  xil_printf("  NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK: %u\r\n", NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK);
  xil_printf("  NUM_OF_PAGE_PER_INDEX: %u\r\n", NUM_OF_PAGE_PER_INDEX);

  xil_printf("\r\nFTL reset complete!!! \r\n");
  xil_printf("Turn on the host PC \r\n");


  while (1) {
    exeLlr = 1;
    if (g_nvmeTask.status == NVME_TASK_WAIT_CC_EN) {
      unsigned int ccEn;
      ccEn = check_nvme_cc_en();
      if (ccEn == 1) {
        set_nvme_admin_queue(1, 1, 1);
        set_nvme_csts_rdy(1);
        g_nvmeTask.status = NVME_TASK_RUNNING;
        xil_printf("\r\nNVMe ready!!!\r\n");
      }
    }
    else if (g_nvmeTask.status == NVME_TASK_RUNNING) {
      NVME_COMMAND nvmeCmd;
      unsigned int cmdValid;
      cmdValid = get_nvme_cmd(&nvmeCmd.qID, &nvmeCmd.cmdSlotTag, &nvmeCmd.cmdSeqNum, nvmeCmd.cmdDword);
      if (cmdValid == 1) {
        rstCnt = 0;
        if (nvmeCmd.qID == 0) {
          handle_nvme_admin_cmd(&nvmeCmd);
        }
        else {
          handle_nvme_io_cmd(&nvmeCmd);
          ReqTransSliceToLowLevel();
          exeLlr = 0;
        }
      }
    }
    else if (g_nvmeTask.status == NVME_TASK_SHUTDOWN) {
      NVME_STATUS_REG nvmeReg;
      nvmeReg.dword = IO_READ32(NVME_STATUS_REG_ADDR);
      if (nvmeReg.ccShn != 0) {
        unsigned int qID;
        set_nvme_csts_shst(1);

        for (qID = 0; qID < 8; qID++) {
          set_io_cq(qID, 0, 0, 0, 0, 0, 0);
          set_io_sq(qID, 0, 0, 0, 0, 0);
        }

        set_nvme_admin_queue(0, 0, 0);
        g_nvmeTask.cacheEn = 0;
        set_nvme_csts_shst(2);
        g_nvmeTask.status = NVME_TASK_WAIT_RESET;

        // flush grown bad block info
        UpdateBadBlockTableForGrownBadBlock(RESERVED_DATA_BUFFER_BASE_ADDR);

        xil_printf("\r\nNVMe shutdown!!!\r\n");
      }
    }
    else if (g_nvmeTask.status == NVME_TASK_WAIT_RESET) {
      unsigned int ccEn;
      ccEn = check_nvme_cc_en();
      if (ccEn == 0) {
        g_nvmeTask.cacheEn = 0;
        set_nvme_csts_shst(0);
        set_nvme_csts_rdy(0);
        g_nvmeTask.status = NVME_TASK_IDLE;
        xil_printf("\r\nNVMe disable!!!\r\n");
      }
    }
    else if (g_nvmeTask.status == NVME_TASK_RESET) {
      unsigned int qID;
      for (qID = 0; qID < 8; qID++) {
        set_io_cq(qID, 0, 0, 0, 0, 0, 0);
        set_io_sq(qID, 0, 0, 0, 0, 0);
      }

      if (rstCnt >= 5) {
        pcie_async_reset(rstCnt);
        rstCnt = 0;
        xil_printf("\r\nPcie iink disable!!!\r\n");
        xil_printf("Wait few minute or reconnect the PCIe cable\r\n");
      }
      else {
        rstCnt++;
      }

      g_nvmeTask.cacheEn = 0;
      set_nvme_admin_queue(0, 0, 0);
      set_nvme_csts_shst(0);
      set_nvme_csts_rdy(0);
      g_nvmeTask.status = NVME_TASK_IDLE;

      xil_printf("\r\nNVMe reset!!!\r\n");
    }

    if (IS_INDEX_BUILDING() && build_paused == 0) {
      if (!HAS_NO_SS_INDEX_TARGET()) {
        /**
         * M_PARAM is 8, then stride should e 139
         * M_PARAM is 16, then stride should e 76
         * M_PARAM is 32, then stride should e 40
         */
        int ret = tryIndexBuildIncremental(g_index_build_resume_point, 139);
        preempted = 0;

        if (ret == 0) { // Completed
          g_index_build_resume_point = 0;
          CLEAR_SS_INDEX_TARGET();
        }
        else {
          g_index_build_resume_point = ret;
        }
      }
      else {
        assignNextSsToIndex();
        if (HAS_NO_SS_INDEX_TARGET()) {
          CLEAR_INDEX_BUILDING();
          CLEAR_SS_INDEX_TARGET();
          g_index_build_resume_point = 0;
          g_inprogess_index_build = 0;
          xil_printf("All SSs now have own index, finalizing the process of index build.\r\n");
        }
      }
    }

    if (exeLlr && ((nvmeDmaReqQ.headReq != REQ_SLOT_TAG_NONE) || notCompletedNandReqCnt || blockedReqCnt)) {
      CheckDoneNvmeDmaReq();
      SchedulingNandReq();
    }
  }
}
