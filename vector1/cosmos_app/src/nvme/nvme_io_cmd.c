//////////////////////////////////////////////////////////////////////////////////
// nvme_io_cmd.c for Cosmos+ OpenSSD
// Copyright (c) 2016 Hanyang University ENC Lab.
// Contributed by Yong Ho Song <yhsong@enc.hanyang.ac.kr> Youngjin Jo
// <yjjo@enc.hanyang.ac.kr> Sangjin Lee <sjlee@enc.hanyang.ac.kr> Jaewook Kwak
// <jwkwak@enc.hanyang.ac.kr>
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
// <jwkwak@enc.hanyang.ac.kr>
//
// Project Name: Cosmos+ OpenSSD
// Design Name: Cosmos+ Firmware
// Module Name: NVMe IO Command Handler
// File Name: nvme_io_cmd.c
//
// Version: v1.0.1
//
// Description:
//   - handles NVMe IO command
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.0.1
//   - header file for buffer is changed from "ia_lru_buffer.h" to
//   "lru_buffer.h"
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////
#include "nvme_io_cmd.h"

#include "../ftl_config.h"
#include "../memory_map.h"
#include "../gs/growingsegment.h"
#include "../request_transform.h"
#include "../ss/sealedsegment.h"
#include "../ss/super.h"
#include "../vector/hnsw.h"
#include "../vector/index.h"
#include "debug.h"
#include "float.h"
#include "host_lld.h"
#include "io_access.h"
#include "nvme.h"
#include "xil_printf.h"
#include "xtime_l.h"

#define SEED1 0xcc9ed51
#define HASH_NUM 30
#define NUM_VICTIMS_OF_UPPER_LEVEL 2
// #define MAX_SEGMENT_SIZE

static SKIPLIST_HEAD* skiphead = (SKIPLIST_HEAD*)GROWING_SEGMENT_HEAD_ADDR;
static SUPER_LEVEL_INFO* super_level_info = (SUPER_LEVEL_INFO*)SUPER_BLOCK_ADDR;
static SUPER_SEALED_SEGMENT_LIST* super_ss_list = (SUPER_SEALED_SEGMENT_LIST*)SUPER_SEALED_SEGMENT_LIST_ADDR;
static SUPER_SEALED_SEGMENT_INFO* super_ss_list_level[4] = {
    (SUPER_SEALED_SEGMENT_INFO*)SUPER_SEALED_SEGMENT_LIST0_ADDR, (SUPER_SEALED_SEGMENT_INFO*)SUPER_SEALED_SEGMENT_LIST1_ADDR,
    (SUPER_SEALED_SEGMENT_INFO*)SUPER_SEALED_SEGMENT_LIST2_ADDR, (SUPER_SEALED_SEGMENT_INFO*)SUPER_SEALED_SEGMENT_LIST3_ADDR };

const unsigned int MAX_SEALED_LEVEL[4] = { MAX_SEALED_LEVEL0, MAX_SEALED_LEVEL1, MAX_SEALED_LEVEL2,
                                           MAX_SEALED_LEVEL3 };

const unsigned int COMPACTION_THRESHOLD_LEVEL[4] = { MAX_SEALED_LEVEL0 - 1, MAX_SEALED_LEVEL1 - 1,
                                                    MAX_SEALED_LEVEL2 - 1, MAX_SEALED_LEVEL3 - 1 };

int build_paused = 0;
int preempted = 0;
int snapshot_tail_per_level[4];
unsigned int searchActivated = 0;
static void CompactGrowingSegment();
static unsigned int MaybeDoCompaction();
static void DoCompaction(unsigned int victim_level);

extern AUTO_CPL_INFO auto_cpl_info[MAX_NUM_NVME_SLOT];

enum CMD_TYPE commandType[1024];
XTime st_read_log[AVAILABLE_OUNTSTANDING_REQ_COUNT];
XTime ed_read_log[AVAILABLE_OUNTSTANDING_REQ_COUNT];
struct STAT_PER_TYPE stat[LAST_CMD];

inline unsigned int GetTypefromCmdSlotTag(int cmdSlotTag) { return commandType[cmdSlotTag]; }


void tryVectorSearch(unsigned int topK) {
  VectorSearchResult* result = (VectorSearchResult*)TOP_K_CURRENT_DISTANCE_BUFFER;
  result->top_k = topK;
  for (unsigned int i = 0; i < topK; i++) {
    result->key[i] = 0;
    result->distance[i] = FLT_MAX;
  }
  // Xil_DCacheFlushRange(TOP_K_CURRENT_DISTANCE_BUFFER, sizeof(VectorSearchResult));

  int bruteforceSearchActivated = 0;
  for (int level = 0; level < MAX_LEVEL; level++) {
    int i = super_ss_list[level].head;
    for (int checked = 0; checked < super_level_info->level_count[level]; checked++) {
      super_ss_list_level[level][i].search_completed = 0;
      if (level >= 1 && !bruteforceSearchActivated && HAS_NO_INDEX_STORED_TO(super_ss_list_level[level][i].vector_index_start_lpn)) {
        bruteforceSearchActivated = 1;
      }
      i = (i + 1) % MAX_SEALED_LEVEL[level];
    }
  }
  waitForAllRequests();

  int earlyExitFlag = 0;
  int numSearchingAcrossSs = 0;
  int numBfSearchingAcrossSs = 0;
  int bf = 0;

  while (1) {
    assignNextSsToSearch();
    if (HAS_NO_SS_SEARCH_TARGET()) { // No Sealed Seg search found
      break;
    }
    else {
      if (targetSsForSearching->vector_index_start_lpn > REVERSE_VALUE_LOG_BASE_LBA || HAS_NO_INDEX_STORED_TO(targetSsForSearching->vector_index_start_lpn)) {
        earlyExitFlag = bruteForceSearch(targetSsForSearching, topK, 0);
      }
      else {
        earlyExitFlag = searchOndemandHnsw(targetSsForSearching, topK);
      }
      if (earlyExitFlag == -1) { // retry after flushing databuf
        xil_printf("RETRY: Early Exit (%d)\r\n", earlyExitFlag);
        manuallyFlushDirtyDataBuffer();
        if (targetSsForSearching->vector_index_start_lpn > REVERSE_VALUE_LOG_BASE_LBA || HAS_NO_INDEX_STORED_TO(targetSsForSearching->vector_index_start_lpn)) {
          earlyExitFlag = bruteForceSearch(targetSsForSearching, topK, 0);
        }
        else {
          earlyExitFlag = searchOndemandHnsw(targetSsForSearching, topK);
        }
      }
      numSearchingAcrossSs++;
    }
    if (earlyExitFlag == 1) {
      CLEAR_TARGET(SS_CONDITION_READY_FOR_SEARCH);
      break;
    }
  }


  if (!earlyExitFlag && bruteforceSearchActivated) {
    int caching = 0;
    while (1)
    {
      findNextSsFromLevel(SS_CONDITION_READY_FOR_SEARCH, 1);
      if (HAS_NO_SS_SEARCH_TARGET()) break;
      earlyExitFlag = bruteForceSearch(targetSsForSearching, topK, caching);
      numBfSearchingAcrossSs++;
      caching = 1;
    }
  }
}

void printVectorSearchResults(int topK) {
  VectorSearchResult* searchResults = (VectorSearchResult*)TOP_K_CURRENT_DISTANCE_BUFFER;
  unsigned int validGlobalCount = 0;
  for (unsigned int i = 0; i < topK; i++) {
    if (searchResults->distance[i] < FLT_MAX) {
      validGlobalCount++;
    }
    else {
      break;
    }
  }
  xil_printf("-----Top-%d Nearest Results-----\r\n", validGlobalCount);
  for (int i = 0; i < validGlobalCount; i++) {
    xil_printf("Rank %d: VID=%d, Dist=", i + 1, searchResults->key[i]);
    printFloatArray(&searchResults->distance[i], 1);
    xil_printf(", ");
  }
  xil_printf("\r\n");
}

void handle_nvme_io_read(unsigned int cmdSlotTag, NVME_IO_COMMAND* nvmeIOCmd) {
  IO_READ_COMMAND_DW12 readInfo12;
  // IO_READ_COMMAND_DW13 readInfo13;
  // IO_READ_COMMAND_DW15 readInfo15;
  unsigned int startLba[2];
  unsigned int nlb;

  readInfo12.dword = nvmeIOCmd->dword[12];
  // readInfo13.dword = nvmeIOCmd->dword[13];
  // readInfo15.dword = nvmeIOCmd->dword[15];

  startLba[0] = nvmeIOCmd->dword[10];
  startLba[1] = nvmeIOCmd->dword[11];
  nlb = readInfo12.NLB;

  ASSERT(startLba[0] < storageCapacity_L && (startLba[1] < STORAGE_CAPACITY_H || startLba[1] == 0));
  // ASSERT(nlb < MAX_NUM_OF_NLB);
  ASSERT((nvmeIOCmd->PRP1[0] & 0x3) == 0 && (nvmeIOCmd->PRP2[0] & 0x3) == 0);  // error
  ASSERT(nvmeIOCmd->PRP1[1] < 0x10000 && nvmeIOCmd->PRP2[1] < 0x10000);

  ReqTransNvmeToSlice(cmdSlotTag, startLba[0], nlb, IO_NVM_READ);
}

void handle_nvme_io_write(unsigned int cmdSlotTag, NVME_IO_COMMAND* nvmeIOCmd) {
  IO_READ_COMMAND_DW12 writeInfo12;
  // IO_READ_COMMAND_DW13 writeInfo13;
  // IO_READ_COMMAND_DW15 writeInfo15;
  unsigned int startLba[2];
  unsigned int nlb;

  writeInfo12.dword = nvmeIOCmd->dword[12];
  // writeInfo13.dword = nvmeIOCmd->dword[13];
  // writeInfo15.dword = nvmeIOCmd->dword[15];

  // if(writeInfo12.FUA == 1)
  // xil_printf("write FUA\r\n");

  startLba[0] = nvmeIOCmd->dword[10];
  startLba[1] = nvmeIOCmd->dword[11];
  nlb = writeInfo12.NLB;

  ASSERT(startLba[0] < storageCapacity_L && (startLba[1] < STORAGE_CAPACITY_H || startLba[1] == 0));
  // ASSERT(nlb < MAX_NUM_OF_NLB);
  ASSERT((nvmeIOCmd->PRP1[0] & 0xF) == 0 && (nvmeIOCmd->PRP2[0] & 0xF) == 0);
  ASSERT(nvmeIOCmd->PRP1[1] < 0x10000 && nvmeIOCmd->PRP2[1] < 0x10000);

  ReqTransNvmeToSlice(cmdSlotTag, startLba[0], nlb, IO_NVM_WRITE);
}


void handle_nvme_io_kv_put(unsigned int cmdSlotTag, NVME_IO_COMMAND* nvmeIOCmd) {
  IO_READ_COMMAND_DW12 writeInfo12;
  // IO_READ_COMMAND_DW13 writeInfo13;
  // IO_READ_COMMAND_DW15 writeInfo15;
  unsigned int startLba[2];
  unsigned int nlb;
  unsigned int kv_key, kv_nlb, kv_lba;
  writeInfo12.dword = nvmeIOCmd->dword[12];
  // writeInfo13.dword = nvmeIOCmd->dword[13];
  // writeInfo15.dword = nvmeIOCmd->dword[15];

  if (writeInfo12.FUA == 1) xil_printf("write FUA\r\n");

  startLba[0] = nvmeIOCmd->dword[10];
  startLba[1] = nvmeIOCmd->dword[11];
  nlb = writeInfo12.NLB;
  // if (nvmeIOCmd->dword[13] != raw_vector_size) {
  //   xil_printf("current vector size %u inserted %u\n", raw_vector_size, nvmeIOCmd->dword[13]);
  // }
  kv_key = startLba[0];
  raw_vector_size = nvmeIOCmd->dword[13];  // raw vector size (bytes)
  kv_nlb = nlb + 1;
  kv_lba = write_pointer_lba;
  ASSERT(kv_nlb == (raw_vector_size / BYTES_PER_SECTOR) + ((raw_vector_size % BYTES_PER_SECTOR) > 0 ? 1 : 0));

  if (skiphead->count == MAX_SKIPLIST_NODE) CompactGrowingSegment();

  SKIPLIST_NODE* ret = skiplist_insert(skiphead, kv_key, kv_lba, raw_vector_size);

  if (ret != SKIPLIST_NIL) {
    skiplist_remove(skiphead, kv_key);
    ret = skiplist_insert(skiphead, kv_key, kv_lba, raw_vector_size);
    ASSERT(ret == SKIPLIST_NIL);
  }
  // optimization issue : if the vector size is less than 4KB, space
  // amplification overhead occurs. solution : VectorCompaction(!= lsm
  // compaction) ---> reduce amplifcation and NAND layout(such like starling) in
  // one operations
  write_pointer_lba += kv_nlb;
  cur_vector_seq++;

  // Write to Data Segment Log
  ReqTransNvmeToSlice(cmdSlotTag, kv_lba, kv_nlb - 1, IO_NVM_WRITE);
}

void handle_nvme_io_vector_build(unsigned int sqid, unsigned int cid, unsigned int cmdSlotTag,
  NVME_IO_COMMAND* nvmeIOCmd) {
  IO_READ_COMMAND_DW12 readInfo12;
  // IO_READ_COMMAND_DW13 readInfo13;
  // IO_READ_COMMAND_DW15 readInfo15;
  unsigned int startLba[2];
  unsigned int nlb;

  NVME_COMPLETION nvmeCPL;
  unsigned int kv_key, kv_length, kv_lba, kv_nlb;
  // unsigned int version_id = 0x7fff;

  readInfo12.dword = nvmeIOCmd->dword[12];
  // readInfo13.dword = nvmeIOCmd->dword[13];
  // readInfo15.dword = nvmeIOCmd->dword[15];

  startLba[0] = nvmeIOCmd->dword[10];
  startLba[1] = nvmeIOCmd->dword[11];
  nlb = readInfo12.NLB;

  ASSERT((nvmeIOCmd->PRP1[0] & 0x3) == 0 && (nvmeIOCmd->PRP2[0] & 0x3) == 0);  // error
  ASSERT(nvmeIOCmd->PRP1[1] < 0x10000 && nvmeIOCmd->PRP2[1] < 0x10000);

  if (IS_INDEX_NOT_BUILDING()) {
    xil_printf("Starting new build - sending success completion\r\n");
    nvmeCPL.dword[0] = 0;
    nvmeCPL.dword[1] = 0;
    nvmeCPL.dword[2] = 0;
    nvmeCPL.dword[3] = 0;

    nvmeCPL.statusField.SCT = 0x0;
    nvmeCPL.statusField.SC = 0x0;
    nvmeCPL.statusField.MORE = 0;
    nvmeCPL.statusField.DNR = 0;
    nvmeCPL.specific = 1;
    // xil_printf("Before set_auto_nvme_cpl: specific=%d, statusFieldWord=0x%x\r\n", 
    //                nvmeCPL.specific, nvmeCPL.statusFieldWord);
    set_nvme_cpl(sqid, cid, 1, 0x0); // result, err
    // set_auto_nvme_cpl(cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
    // set_nvme_cpl(sqid, cid, nvmeCPL.specific, nvmeCPL.statusFieldWord);

    // xil_printf("Completion sent, setting index building flag\r\n");
    SET_INDEX_BUILDING();
    CLEAR_SS_INDEX_TARGET();
    for (int i = 1; i < MAX_LEVEL; i++) {
      snapshot_tail_per_level[i] = super_ss_list[i].tail;
    }
  }
  else if (IS_INDEX_BUILDING()) {
    nvmeCPL.dword[0] = 0;
    nvmeCPL.statusField.SCT = 0x7;
    nvmeCPL.statusField.SC = 0xC1;
    nvmeCPL.specific = 0;
    set_auto_nvme_cpl(cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
  }
  else {
    xil_printf("isIndexBuilding = %d\n", isIndexBuilding);
    assert(!"[ERROR] Unexpected isIndexBuilding value occurred.");
  }
  xil_printf("=== VECTOR_BUILD HANDLER FINISHED ===\r\n");
}

void handle_nvme_io_vector_search(unsigned int sqid, unsigned int cid, unsigned int cmdSlotTag,
  NVME_IO_COMMAND* nvmeIOCmd) {
  XTime st, ed;

  XTime_GetTime(&st);
  IO_READ_COMMAND_DW12 writeInfo12;
  // IO_READ_COMMAND_DW15 writeInfo15;
  writeInfo12.dword = nvmeIOCmd->dword[12];

  // IO_READ_COMMAND_DW15 writeInfo15;
  // IO_READ_COMMAND_DW15 writeInfo15;
  // unsigned int startLba[2];
  unsigned int nlb, cmd4KBOffset, i, j, k;

  // writeInfo12.dword = nvmeIOCmd->dword[12];
  NVME_COMPLETION nvmeCPL;

  unsigned int top_k = nvmeIOCmd->dword15;
  nlb = writeInfo12.NLB;

  if (top_k > MAX_TOP_K) {
    xil_printf("we do not support top k %u\n", top_k);
    nvmeCPL.dword[0] = 0;
    nvmeCPL.statusField.SCT = 0x7;
    nvmeCPL.statusField.SC = 0xC1;
    nvmeCPL.specific = 0;
    set_auto_nvme_cpl(cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
    return;
  }

  unsigned int predefined_vector_nlb = VECTOR_SIZE_NLB;
  float* query_vector_addr = QUERY_VECTOR_BUFFER_START_ADDR;
  unsigned int devAddr = QUERY_VECTOR_BUFFER_START_ADDR;

  Xil_DCacheInvalidateRange(QUERY_VECTOR_BUFFER_START_ADDR, QUERY_VECTOR_BUFFER_SIZE);

  for (cmd4KBOffset = 0; cmd4KBOffset < predefined_vector_nlb; cmd4KBOffset++) {
    set_auto_rx_dma(cmdSlotTag, cmd4KBOffset, devAddr, NVME_COMMAND_AUTO_COMPLETION_OFF);
    devAddr += BYTES_PER_NVME_BLOCK;
  }
  unsigned int reqTail = g_hostDmaStatus.fifoTail.autoDmaRx;
  unsigned int overFlowed = g_hostDmaAssistStatus.autoDmaRxOverFlowCnt;

  while (!check_auto_rx_dma_partial_done(reqTail, overFlowed));
  Xil_DCacheInvalidateRange(TOP_K_CURRENT_DISTANCE_BUFFER, TOP_K_CURRENT_DISTANCE_BUFFER_SIZE);

  if (top_k) {
    tryVectorSearch(top_k);
  }

  VectorSearchResult* searchResult = (VectorSearchResult*)TOP_K_CURRENT_DISTANCE_BUFFER;

  Xil_DCacheInvalidateRange(TOP_K_CURRENT_DISTANCE_BUFFER, TOP_K_CURRENT_DISTANCE_BUFFER_SIZE);

  // xil_printf("=== Vector Search Results ===\r\n");
  // xil_printf("Top K: %u\r\n", searchResult->top_k);

  // if (searchResult->top_k > MAX_TOP_K) {
  //     xil_printf("ERROR: Invalid top_k value: %u (max: %u)\r\n", searchResult->top_k, MAX_TOP_K);
  //     return;
  // }

  // for (unsigned int k = 0; k < searchResult->top_k; k++) {
  //     xil_printf("Result[%u]: key=%u, distance=", k, searchResult->key[k]);
  //     float dist = searchResult->distance[k];
  //     printFloatArray(&dist, 1);
  // }
  // xil_printf("=============================\r\n");

  for (i = 0;i < NVME_BLOCKS_PER_SLICE;i++) {
    set_auto_tx_dma(cmdSlotTag, (i), TOP_K_CURRENT_DISTANCE_BUFFER + (i * BYTES_PER_NVME_BLOCK), NVME_COMMAND_AUTO_COMPLETION_ON);
  }
  if (IS_INDEX_BUILDING())
    preempted = 1;
  // xil_printf("[PASS] HNSW search successfully.\r\n");

  XTime_GetTime(&ed);
  numVectorSearch++;
  if (numVectorSearch % 100 == 0) {
    double search_time = 1.0 * (double)(ed - st) / (double)(COUNTS_PER_SECOND / 1000000);
    printf("search latency: %f us\n", search_time);
  }
}

void handle_nvme_io_vector_build_status(unsigned int sqid, unsigned int cid, unsigned int cmdSlotTag,
  NVME_IO_COMMAND* nvmeIOCmd) {
  NVME_COMPLETION nvmeCPL;
  nvmeCPL.dword[0] = 0;
  if (IS_INDEX_BUILDING()) {
    nvmeCPL.statusField.SCT = 0x7;
    nvmeCPL.statusField.SC = 0xC1;
    nvmeCPL.specific = 0;
    // Build is on-going
    set_auto_nvme_cpl(cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
  }
  else {
    nvmeCPL.specific = 100;
    nvmeCPL.statusField.SCT = 0x0;
    nvmeCPL.statusField.SC = 0x0;
    // xil_printf("Build completed - returning success\r\n");
    // Build Complete
    set_auto_nvme_cpl(cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
  }
  // xil_printf("STATUS: After completion, IS_INDEX_BUILDING() = %d\r\n", IS_INDEX_BUILDING());
}

void handle_nvme_io_pause_build(unsigned int sqid, unsigned int cid, unsigned int cmdSlotTag,
  NVME_IO_COMMAND* nvmeIOCmd) {
  NVME_COMPLETION nvmeCPL;
  nvmeCPL.dword[0] = 0;
  if (IS_INDEX_BUILDING()) {
    nvmeCPL.statusField.SCT = 0x7;
    nvmeCPL.statusField.SC = 0xC1;
    nvmeCPL.specific = 0;
    build_paused = 1;
    // Return 0x7C1
    xil_printf("Pause Build\r\n");
    set_auto_nvme_cpl(cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
  }
  else {
    nvmeCPL.specific = 100;
    nvmeCPL.statusField.SCT = 0x0;
    nvmeCPL.statusField.SC = 0x0;
    // Not in the middle of the build..
    set_auto_nvme_cpl(cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
  }
}

void handle_nvme_io_resume_build(unsigned int sqid, unsigned int cid, unsigned int cmdSlotTag,
  NVME_IO_COMMAND* nvmeIOCmd) {
  NVME_COMPLETION nvmeCPL;
  nvmeCPL.dword[0] = 0;
  if (IS_INDEX_BUILDING()) {
    nvmeCPL.statusField.SCT = 0x7;
    nvmeCPL.statusField.SC = 0xC1;
    nvmeCPL.specific = 0;
    build_paused = 0;
    xil_printf("Resume Build\r\n");
    set_auto_nvme_cpl(cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
  }
  else {
    nvmeCPL.specific = 100;
    nvmeCPL.statusField.SCT = 0x0;
    nvmeCPL.statusField.SC = 0x0;
    set_auto_nvme_cpl(cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
  }
}

void handle_nvme_io_cmd(NVME_COMMAND* nvmeCmd) {
  NVME_IO_COMMAND* nvmeIOCmd;
  NVME_COMPLETION nvmeCPL;
  unsigned int opc;
  nvmeIOCmd = (NVME_IO_COMMAND*)nvmeCmd->cmdDword;
  // xil_printf("OPC = 0x%X\r\n", nvmeIOCmd->OPC);
  // xil_printf("PRP1[63:32] = 0x%X, PRP1[31:0] = 0x%X\r\n", nvmeIOCmd->PRP1[1],
  // nvmeIOCmd->PRP1[0]); xil_printf("PRP2[63:32] = 0x%X, PRP2[31:0] =
  // 0x%X\r\n", nvmeIOCmd->PRP2[1], nvmeIOCmd->PRP2[0]); xil_printf("dword10 =
  // 0x%X\r\n", nvmeIOCmd->dword10); xil_printf("dword11 = 0x%X\r\n",
  // nvmeIOCmd->dword11); xil_printf("dword12 = 0x%X\r\n", nvmeIOCmd->dword12);

  opc = (unsigned int)nvmeIOCmd->OPC;

  switch (opc) {
  case IO_NVM_FLUSH: {
    nvmeCPL.dword[0] = 0;
    nvmeCPL.specific = 0x0;
    set_auto_nvme_cpl(nvmeCmd->cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
    break;
  }
  case IO_NVM_WRITE: {
    handle_nvme_io_write(nvmeCmd->cmdSlotTag, nvmeIOCmd);
    break;
  }
  case IO_NVM_READ: {
    handle_nvme_io_read(nvmeCmd->cmdSlotTag, nvmeIOCmd);
    break;
  }
  case IO_NVM_KV_PUT: {
    commandType[nvmeCmd->cmdSlotTag] = KV_PUT;
    stat[KV_PUT].c++;
    searchActivated = 0;
    XTime st, ed;
    XTime_GetTime(&st);
    handle_nvme_io_kv_put(nvmeCmd->cmdSlotTag, nvmeIOCmd);
    XTime_GetTime(&ed);
    stat[KV_GET].TOTAL_TIME += ed - st;
    unsigned int retry;

    do {
      retry = MaybeDoCompaction();
    } while (retry == 1);

    break;
  }
  case IO_NVM_KV_DELETE: {
    // issue : vetordb need vector delete
    // handle_nvme_io_kv_delete(nvmeCmd->cmdSlotTag, nvmeIOCmd);
    break;
  }
  case IO_NVM_VECTOR_BUILD: {
    commandType[nvmeCmd->cmdSlotTag] = VECTOR_BUILD;
    stat[VECTOR_BUILD].c++;
    searchActivated = 0;

    XTime st, ed;
    XTime_GetTime(&st);
    handle_nvme_io_vector_build(nvmeCmd->qID, nvmeIOCmd->CID, nvmeCmd->cmdSlotTag, nvmeIOCmd);
    XTime_GetTime(&ed);
    stat[VECTOR_BUILD].TOTAL_TIME += ed - st;

    break;
  }
  case IO_NVM_VECTOR_SEARCH: {
    commandType[nvmeCmd->cmdSlotTag] = VECTOR_SEARCH;
    stat[VECTOR_SEARCH].c++;

    XTime st, ed;
    XTime_GetTime(&st);
    handle_nvme_io_vector_search(nvmeCmd->qID, nvmeIOCmd->CID, nvmeCmd->cmdSlotTag, nvmeIOCmd);
    XTime_GetTime(&ed);
    searchActivated++;
    stat[VECTOR_SEARCH].TOTAL_TIME += ed - st;

    break;
  }
  case IO_NVM_VECTOR_BUILD_STATUS: {
    commandType[nvmeCmd->cmdSlotTag] = VECTOR_BUILD_STATUS;
    // stat[VECTOR_BUILD_STATUS].c++;
    handle_nvme_io_vector_build_status(nvmeCmd->qID, nvmeIOCmd->CID, nvmeCmd->cmdSlotTag, nvmeIOCmd);
    break;
  }
  case IO_NVM_KV_PAUSE_BUILD: {
    commandType[nvmeCmd->cmdSlotTag] = PAUSE_BUILD;
    handle_nvme_io_pause_build(nvmeCmd->qID, nvmeIOCmd->CID, nvmeCmd->cmdSlotTag, nvmeIOCmd);
    break;
  }
  case IO_NVM_KV_RESUME_BUILD: {
    commandType[nvmeCmd->cmdSlotTag] = RESUME_BUILD;
    handle_nvme_io_resume_build(nvmeCmd->qID, nvmeIOCmd->CID, nvmeCmd->cmdSlotTag, nvmeIOCmd);
    break;
  }
  case PRINT_TIME: {
    xil_printf("\n\n================================================\n");
    for (int i = KV_PUT; i < LAST_CMD; i++) {
      if (stat[i].c != 0) {
        double total, dev_compute, dev_memcpy, nand_ss, nand_log;
        total = 1.0 * (stat[i].TOTAL_TIME / stat[i].c) / (COUNTS_PER_SECOND / 1000000);
        dev_compute = 1.0 * ((stat[i].TOTAL_TIME - stat[i].NAND_READ_SS - stat[i].DEVICE_MEMCPY) / stat[i].c) /
          (COUNTS_PER_SECOND / 1000000);
        dev_memcpy = 1.0 * (stat[i].DEVICE_MEMCPY / stat[i].c) / (COUNTS_PER_SECOND / 1000000);
        nand_ss = 1.0 * (stat[i].NAND_READ_SS / stat[i].c) / (COUNTS_PER_SECOND / 1000000);
        nand_log = 1.0 * (stat[i].NAND_READ_LOG / stat[i].found) / (COUNTS_PER_SECOND / 1000000);
      }

      stat[i].c = stat[i].found = stat[i].hit = 0;
      stat[i].TOTAL_TIME = stat[i].NAND_READ_SS = stat[i].NAND_READ_LOG = stat[i].DEVICE_MEMCPY = 0;
    }
    unsigned int lpn = nvmeIOCmd->dword[10];
    SyncAllLowLevelReqDone();
    TriggerInternalPagesRead(lpn, COMPACTION_BUFFER_ADDR, 1);
    SyncAllLowLevelReqDone();

    SEALED_SEGMENT_INDEX_NODE* tmp_ptr = (SEALED_SEGMENT_INDEX_NODE*)COMPACTION_BUFFER_ADDR;
    for (unsigned int j = 0; j < 4096; j++) {
      xil_printf("k[%d], %d\t", j, tmp_ptr[j].key);
    }
    xil_printf("\n");
    memset(COMPACTION_BUFFER_ADDR, 0, 16384);

    nvmeCPL.dword[0] = 0;
    nvmeCPL.specific = 0x0;
    set_auto_nvme_cpl(nvmeCmd->cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
    break;
  }
  default: {
    xil_printf("Not Support IO Command OPC: %X\r\n", opc);
    ASSERT(0);
    break;
  }
  }
}

void CompactGrowingSegment() {
  SyncAllLowLevelReqDone();

  SEALED_SEGMENT_INDEX_NODE* ss_index = (SEALED_SEGMENT_INDEX_NODE*)SEALED_SEGMENT_INDEX_BUFFER;
  SEALED_SEGMENT_DATA_NODE* ss_data = (SEALED_SEGMENT_DATA_NODE*)SEALED_SEGMENT_DATA_BUFFER;
  unsigned int cur_lpn = reverse_write_pointer_lpn;
  unsigned int ss_index_lpn = 0;
  unsigned int ss_data_lpn = 0;
  unsigned int cnt = 0;


  // Build Table
  SKIPLIST_NODE** cur = skiphead->forward;

  while (cur[0] != SKIPLIST_NIL) {
    // Build Table
    ss_index[cnt].key = cur[0]->key;
    ss_data[cnt].lba = cur[0]->lba;
    ss_data[cnt++].length = cur[0]->length;
    cur = cur[0]->forward;
    if (cnt > MAX_SKIPLIST_NODE) {
      xil_printf("something is going bad...");
      assert(0);
    }
  }
  unsigned int tmp;
  unsigned int tmp_size;
  unsigned int tmp_total_entry;
  // Superblock Update
  {
    unsigned int offset;
    unsigned int index_size = (sizeof(struct _SEALED_SEGMENT_INDEX_NODE) * cnt) / BYTES_PER_DATA_REGION_OF_SLICE;
    unsigned int data_size = (sizeof(struct _SEALED_SEGMENT_DATA_NODE) * cnt) / BYTES_PER_DATA_REGION_OF_SLICE;
    /**
     * @note: Below codes acts as ceiling when the size is less than BYTES_PER_DATA_REGION_OF_SLICE size.
     * This is possible when the number of SKIPLIST_NODE element is increasingly small.
     */
    if (index_size == 0 && cnt > 0) index_size++;
    if (data_size == 0 && cnt > 0) data_size++;


    cur_lpn = reverse_write_pointer_lpn - (index_size + data_size);
    TriggerInternalPagesWrite(cur_lpn, SEALED_SEGMENT_INDEX_BUFFER, index_size);
    ss_index_lpn = cur_lpn;
    super_ss_list_level[0][super_ss_list[0].tail].index_size = index_size;

    cur_lpn = reverse_write_pointer_lpn - (data_size);
    TriggerInternalPagesWrite(cur_lpn, SEALED_SEGMENT_DATA_BUFFER, data_size);
    ss_data_lpn = cur_lpn;
    super_ss_list_level[0][super_ss_list[0].tail].data_size = data_size;
    SyncAllLowLevelReqDone();


    CLEAR_INDEX_STORAGE(super_ss_list_level[0][super_ss_list[0].tail].vector_index_start_lpn);
    super_ss_list_level[0][super_ss_list[0].tail].vector_index_size = 0;
    super_ss_list_level[0][super_ss_list[0].tail].search_completed = 0;

    super_ss_list_level[0][super_ss_list[0].tail].level = 0;
    super_ss_list_level[0][super_ss_list[0].tail].head_lpn = ss_index_lpn;
    super_ss_list_level[0][super_ss_list[0].tail].tail_lpn = reverse_write_pointer_lpn - 1;
    super_ss_list_level[0][super_ss_list[0].tail].total_entry = cnt;
    super_ss_list_level[0][super_ss_list[0].tail].min_key = ss_index[0].key;
    super_ss_list_level[0][super_ss_list[0].tail].max_key = ss_index[cnt - 1].key;
    super_level_info->level_count[0]++;
    super_ss_list[0].tail = (super_ss_list[0].tail + 1) % MAX_SEALED_LEVEL0;
  }

  reverse_write_pointer_lpn = ss_index_lpn;

  // Skiplist Initialization
  initSkipList(skiphead);
  skiphead->st_lba = write_pointer_lba;

  FlushAllDataBuf();
  manuallyFlushDirtyDataBuffer();
}


void TriggerInternalDataWrite(const unsigned int lsa, const unsigned int bufAddr, const unsigned int bufSize) {
  unsigned int virtualSliceAddr = AddrTransWrite(lsa);
  unsigned int reqSlotTag = GetFromFreeReqQ();
  if (bufSize == BYTES_PER_DATA_REGION_OF_SLICE) {
    reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NAND;
    reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_WRITE;
    reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr = lsa;
    reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat = REQ_OPT_DATA_BUF_ADDR_NO_SPARE;
    reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr = REQ_OPT_NAND_ADDR_VSA;
    reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc = REQ_OPT_NAND_ECC_ON;
    reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning = REQ_OPT_NAND_ECC_WARNING_OFF; //
    reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK; //
    reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace = REQ_OPT_BLOCK_SPACE_MAIN;
    reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.addr = bufAddr;
    reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr = virtualSliceAddr;

    SelectLowLevelReqQ(reqSlotTag);
  }
  else {
    assert(!"multiple internal page write not supported yet!");
  }
}

void TriggerInternalDataRead(const unsigned int lsa, const unsigned int bufAddr, const unsigned int bufSize) {
  unsigned int virtualSliceAddr = AddrTransRead(lsa);

  unsigned int reqSlotTag = GetFromFreeReqQ();
  if (bufSize == BYTES_PER_DATA_REGION_OF_SLICE) {
    reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NAND;
    reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_READ;
    reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr = lsa;
    reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat = REQ_OPT_DATA_BUF_ADDR_NO_SPARE;
    reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr = REQ_OPT_NAND_ADDR_VSA;
    reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc = REQ_OPT_NAND_ECC_ON;
    reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning = REQ_OPT_NAND_ECC_WARNING_OFF; // 
    reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK; // 
    reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace = REQ_OPT_BLOCK_SPACE_MAIN;
    reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.addr = bufAddr;
    reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr = virtualSliceAddr;

    SelectLowLevelReqQ(reqSlotTag);
  }
  else {
    assert(!"multiple internal page read not supported yet!!");
  }
}


void TriggerInternalPagesRead(const unsigned int startLsa, const unsigned int bufAddr, const unsigned int numPages) {
  unsigned int i = 0;
  unsigned int offset = 0;
  unsigned int size = BYTES_PER_DATA_REGION_OF_SLICE * numPages;
  for (i = 0, offset = 0; offset < size; i++, offset += BYTES_PER_DATA_REGION_OF_SLICE)
    TriggerInternalDataRead(startLsa + i, bufAddr + offset, BYTES_PER_DATA_REGION_OF_SLICE);
}

void TriggerInternalPagesWrite(const unsigned int startLsa, const unsigned int bufAddr, const unsigned int numPages) {
  unsigned int i = 0;
  unsigned int offset = 0;
  unsigned int size = BYTES_PER_DATA_REGION_OF_SLICE * numPages;
  for (i = 0, offset = 0; offset < size; i++, offset += BYTES_PER_DATA_REGION_OF_SLICE) {
    TriggerInternalDataWrite(startLsa + i, bufAddr + offset, BYTES_PER_DATA_REGION_OF_SLICE);
  }
}

static unsigned int MaybeDoCompaction() {

  unsigned int victim_level = 0x7fff;
  for (int level = 2; level >= 0; level--) {
    if (super_level_info->level_count[level] >= 2 &&
      super_level_info->level_count[level] >= COMPACTION_THRESHOLD_LEVEL[level]) {
      victim_level = level;
      break;
    }
  }

  if (victim_level == 0x7fff) {
    return 0;
  }
  else {
    DoCompaction(victim_level);
    return 1;
  }
}

void checkDataBufForLpn(unsigned int lpn) {
  xil_printf("LPN 0x%X DataBuf:\r\n", lpn);

  int found = 0;
  for (int i = 0; i < AVAILABLE_DATA_BUFFER_ENTRY_COUNT; i++) {
    if (dataBufMapPtr->dataBuf[i].logicalSliceAddr == lpn) {
      xil_printf("Found in DataBuf[%d]: dirty=%d, full=%d\r\n", i, dataBufMapPtr->dataBuf[i].dirty, dataBufMapPtr->dataBuf[i].full);
      found = 1;
    }
  }

  if (!found) {
    xil_printf("Not found in DataBuf\r\n");
  }
}

static void FlushOutputSsBuffer(unsigned int victim_level, const unsigned int total_entry) {
  unsigned int output_level = victim_level + 1;
  unsigned int index_size;
  unsigned int data_size;
  unsigned int ss_index_lpn = 0;
  unsigned int ss_data_lpn = 0;

  index_size = ((sizeof(struct _SEALED_SEGMENT_INDEX_NODE) * total_entry) / BYTES_PER_DATA_REGION_OF_SLICE) +
    ((sizeof(struct _SEALED_SEGMENT_INDEX_NODE) * total_entry) % BYTES_PER_DATA_REGION_OF_SLICE ? 1 : 0);
  data_size = ((sizeof(struct _SEALED_SEGMENT_DATA_NODE) * total_entry) / BYTES_PER_DATA_REGION_OF_SLICE) +
    ((sizeof(struct _SEALED_SEGMENT_DATA_NODE) * total_entry) % BYTES_PER_DATA_REGION_OF_SLICE ? 1 : 0);
  ss_index_lpn = reverse_write_pointer_lpn - (index_size + data_size);
  TriggerInternalPagesWrite(ss_index_lpn, SEALED_SEGMENT_INDEX_BUFFER, index_size);
  SyncAllLowLevelReqDone();
  ss_data_lpn = reverse_write_pointer_lpn - data_size;
  TriggerInternalPagesWrite(ss_data_lpn, SEALED_SEGMENT_DATA_BUFFER, data_size);
  SyncAllLowLevelReqDone();

  super_ss_list_level[output_level][super_ss_list[output_level].tail].level = output_level;
  super_ss_list_level[output_level][super_ss_list[output_level].tail].head_lpn = ss_index_lpn;
  super_ss_list_level[output_level][super_ss_list[output_level].tail].tail_lpn = reverse_write_pointer_lpn - 1;
  super_ss_list_level[output_level][super_ss_list[output_level].tail].index_size = index_size;
  super_ss_list_level[output_level][super_ss_list[output_level].tail].data_size = data_size;
  super_ss_list_level[output_level][super_ss_list[output_level].tail].total_entry = total_entry;
  super_ss_list_level[output_level][super_ss_list[output_level].tail].vector_index_start_lpn = INVALID_LPN;
  super_ss_list_level[output_level][super_ss_list[output_level].tail].vector_index_size = 0;
  super_ss_list_level[output_level][super_ss_list[output_level].tail].search_completed = 0;
  super_ss_list_level[output_level][super_ss_list[output_level].tail].min_key =
    ((SEALED_SEGMENT_INDEX_NODE*)(SEALED_SEGMENT_INDEX_BUFFER))[0].key;
  super_ss_list_level[output_level][super_ss_list[output_level].tail].max_key =
    ((SEALED_SEGMENT_INDEX_NODE*)(SEALED_SEGMENT_INDEX_BUFFER))[total_entry - 1].key;

  super_level_info->level_count[output_level]++;
  super_ss_list[output_level].tail = (super_ss_list[output_level].tail + 1) % MAX_SEALED_LEVEL[output_level];
  reverse_write_pointer_lpn = ss_index_lpn;

  xil_printf("=== AFTER FLUSH - Vector Index State ===\r\n");
}


unsigned int DoCompactionImpl(SUPER_SEALED_SEGMENT_INFO* victim_ss[], unsigned int victim_n, unsigned int victim_level) {
  SEALED_SEGMENT_INDEX_NODE* ss_index = (SEALED_SEGMENT_INDEX_NODE*)SEALED_SEGMENT_INDEX_BUFFER;
  SEALED_SEGMENT_DATA_NODE* ss_data = (SEALED_SEGMENT_DATA_NODE*)SEALED_SEGMENT_DATA_BUFFER;

  Xil_DCacheInvalidateRange((INTPTR)ss_index, SEALED_SEGMENT_INDEX_BUFFER_SIZE);
  Xil_DCacheInvalidateRange((INTPTR)ss_data, SEALED_SEGMENT_DATA_BUFFER_SIZE);

  unsigned int v1 = 0, v2 = 0;
  unsigned int i = 0;

  assert(victim_n <= MAX_SEALED_LEVEL0);
  for (unsigned int i = 0; i < victim_n; i++) {
    assert(victim_ss[i] != NULL);
    assert(victim_ss[i]->head_lpn != 0xffffffff && victim_ss[i]->tail_lpn != 0xffffffff);
    assert(victim_ss[i]->total_entry != 0 && victim_ss[i]->head_lpn != 0xffffffff);
  }

  unsigned int max_size;
  if (victim_level == 0)
    max_size = MAX_SKIPLIST_NODE;
  else if (victim_level == 1)
    max_size = (MAX_SKIPLIST_NODE) * 3;
  else if (victim_level == 2)
    max_size = (MAX_SKIPLIST_NODE) * 3 * 2;
  unsigned int max_loop_cnt = victim_n * max_size + 1;
  unsigned int total_entry = 0;
  unsigned int remaining_total_entry = 0;
  unsigned int cur_idx[MAX_SEALED_LEVEL0];
  int alreadyReadSsIndex[MAX_SEALED_LEVEL0];
  int alreadyReadSsData[MAX_SEALED_LEVEL0];
  for (int i = 0; i < MAX_SEALED_LEVEL0; i++) {
    cur_idx[i] = 0;
  }
  for (int i = 0; i < MAX_SEALED_LEVEL0; i++) {
    alreadyReadSsIndex[i] = -1;
  }
  for (int i = 0; i < MAX_SEALED_LEVEL0; i++) {
    alreadyReadSsData[i] = -1;
  }
  memset(COMPACTION_BUFFER_ADDR, 0, COMPACTION_BUFFER_ADDR_SIZE_ONDEMAND);
  while (1) {
    if (--max_loop_cnt < 0) {
      xil_printf("Potential infinite loop detected!!!\r\n");
      break;
    }
    else {
      ;
    }

    unsigned int on_going = 0;
    unsigned int to_read_ondemand = 0;
    for (i = 0; i < victim_n; i++) {
      if (cur_idx[i] >= victim_ss[i]->total_entry) {
        continue;
      }
      on_going = 1;
      if (alreadyReadSsIndex[i] != (int)(cur_idx[i] / NUM_OF_SEALED_SEGMENT_INDEX_NODE_PER_NAND_PAGE)) {
        TriggerInternalPagesRead(victim_ss[i]->head_lpn + (unsigned int)(cur_idx[i] / NUM_OF_SEALED_SEGMENT_INDEX_NODE_PER_NAND_PAGE),
          COMPACTION_BUFFER_ADDR_INDEX_ONDEMAND(i), 1);
        to_read_ondemand = 1;
        alreadyReadSsIndex[i] = (int)(cur_idx[i] / NUM_OF_SEALED_SEGMENT_INDEX_NODE_PER_NAND_PAGE);
      }
      if (alreadyReadSsData[i] != (int)(cur_idx[i] / NUM_OF_SEALED_SEGMENT_DATA_NODE_PER_NAND_PAGE)) {
        TriggerInternalPagesRead(victim_ss[i]->head_lpn + victim_ss[i]->index_size + (unsigned int)(cur_idx[i] / NUM_OF_SEALED_SEGMENT_DATA_NODE_PER_NAND_PAGE),
          COMPACTION_BUFFER_ADDR_DATA_ONDEMAND(i), 1);
        to_read_ondemand = 1;
        alreadyReadSsData[i] = (int)(cur_idx[i] / NUM_OF_SEALED_SEGMENT_DATA_NODE_PER_NAND_PAGE);
      }
    }

    if (!on_going) {
      break;
    }
    if (to_read_ondemand) {
      SyncAllLowLevelReqDone();
      to_read_ondemand = 0;
    }

    // compare and sort
    int from = -1;
    unsigned int min_key = UINT32_MAX;
    for (i = 0; i < victim_n; i++) {
      if (cur_idx[i] >= victim_ss[i]->total_entry) {
        continue;
      }
      unsigned int cur_key = ((SEALED_SEGMENT_INDEX_NODE*)COMPACTION_BUFFER_ADDR_INDEX_ONDEMAND(
        i))[cur_idx[i] % NUM_OF_SEALED_SEGMENT_INDEX_NODE_PER_NAND_PAGE]
        .key;

      if (min_key > cur_key) {
        min_key = cur_key;
        from = i;
      }
      else if (min_key == cur_key) {
        cur_idx[from]++;
        from = i;
      }
    }

    if (from == -1) {
      assert(FALSE);
    }

    ss_index[total_entry].key = ((SEALED_SEGMENT_INDEX_NODE*)COMPACTION_BUFFER_ADDR_INDEX_ONDEMAND(
      from))[cur_idx[from] % NUM_OF_SEALED_SEGMENT_INDEX_NODE_PER_NAND_PAGE]
      .key;
    ss_data[total_entry].lba = ((SEALED_SEGMENT_DATA_NODE*)COMPACTION_BUFFER_ADDR_DATA_ONDEMAND(
      from))[cur_idx[from] % NUM_OF_SEALED_SEGMENT_DATA_NODE_PER_NAND_PAGE]
      .lba;
    ss_data[total_entry].length = ((SEALED_SEGMENT_DATA_NODE*)COMPACTION_BUFFER_ADDR_DATA_ONDEMAND(
      from))[cur_idx[from] % NUM_OF_SEALED_SEGMENT_DATA_NODE_PER_NAND_PAGE]
      .length;
    cur_idx[from]++;
    total_entry++;
  }
  return total_entry;
}

static void DoCompaction(unsigned int victim_level) {
  SUPER_SEALED_SEGMENT_INFO* victim_selection[COMPACTION_THRESHOLD_LEVEL[victim_level]];
  unsigned int victim_done[COMPACTION_THRESHOLD_LEVEL[victim_level]];

  unsigned int victim_n = (victim_level == 0) ? COMPACTION_THRESHOLD_LEVEL[victim_level] : NUM_VICTIMS_OF_UPPER_LEVEL;
  assert(victim_n >= 0 && victim_n <= COMPACTION_THRESHOLD_LEVEL[victim_level]);

  for (int i = 0; i < victim_n; i++) {
    int index = (super_ss_list[victim_level].head + i) % MAX_SEALED_LEVEL[victim_level];
    victim_selection[i] = &super_ss_list_level[victim_level][index];
    victim_done[i] = 0;
  }

  if (shouldDelayCompaction == 0) {
    unsigned int remain_entries = DoCompactionImpl(victim_selection, victim_n, victim_level);
    if (remain_entries) {
      FlushOutputSsBuffer(victim_level, remain_entries);
    }
  }
  else {
    int output_level = victim_level + 1;
    for (int i = 0; i < victim_n; i++) {
      super_ss_list_level[output_level][super_ss_list[output_level].tail].level = output_level;
      super_ss_list_level[output_level][super_ss_list[output_level].tail].head_lpn = victim_selection[i]->head_lpn;
      super_ss_list_level[output_level][super_ss_list[output_level].tail].tail_lpn = victim_selection[i]->tail_lpn;
      super_ss_list_level[output_level][super_ss_list[output_level].tail].index_size = victim_selection[i]->index_size;
      super_ss_list_level[output_level][super_ss_list[output_level].tail].data_size = victim_selection[i]->data_size;
      super_ss_list_level[output_level][super_ss_list[output_level].tail].total_entry = victim_selection[i]->total_entry;
      super_ss_list_level[output_level][super_ss_list[output_level].tail].vector_index_start_lpn = INVALID_LPN;
      super_ss_list_level[output_level][super_ss_list[output_level].tail].vector_index_size = 0;
      super_ss_list_level[output_level][super_ss_list[output_level].tail].search_completed = 0;
      super_ss_list_level[output_level][super_ss_list[output_level].tail].min_key = victim_selection[i]->min_key;
      super_ss_list_level[output_level][super_ss_list[output_level].tail].max_key = victim_selection[i]->max_key;
      super_level_info->level_count[output_level]++;
      super_ss_list[output_level].tail = (super_ss_list[output_level].tail + 1) % MAX_SEALED_LEVEL[output_level];
    }
  }

  if (shouldDelayCompaction == 0) {
    for (unsigned int v = 0; v < victim_n; v++) {
      for (unsigned int i = victim_selection[v]->head_lpn; i < victim_selection[v]->tail_lpn; i++) {
        InvalidateOldVsa(i);
      }
    }
  }

  super_level_info->level_count[victim_level] -= victim_n;
  for (int i = 0; i < victim_n; i++) {
    victim_selection[i]->head_lpn = INVALID_LPN;
    victim_selection[i]->tail_lpn = INVALID_LPN;
    victim_selection[i]->total_entry = 0;
    victim_selection[i]->vector_index_start_lpn = INVALID_LPN;
    victim_selection[i]->vector_index_size = 0;
    victim_selection[i]->search_completed = 0;
  }
  super_ss_list[victim_level].head =
    (super_ss_list[victim_level].head + victim_n) % MAX_SEALED_LEVEL[victim_level];

  for (int level = 0; level < MAX_LEVEL; level++) {
    int i = super_ss_list[level].head;
    xil_printf("[Level. %d]\r\n", level);
    for (int checked = 0; checked < super_level_info->level_count[level]; checked++) {
      xil_printf("Sealed[%d]: Min&Max=[%d ~ %d] (Total #: %d)\r\n", i, super_ss_list_level[level][i].min_key, super_ss_list_level[level][i].max_key, super_ss_list_level[level][i].total_entry);
      i = (i + 1) % MAX_SEALED_LEVEL[level];
    }
  }

  FlushAllDataBuf();
  manuallyFlushDirtyDataBuffer();

}

/**
 * Vector DB Management Functions
 *
 * The following functions handle various vectorDB's operations.
 */


void resetSearchFlags(void) {
  for (int level = 1; level < MAX_LEVEL; level++) {
    int levelCount = super_level_info->level_count[level];
    if (levelCount <= 0) continue;

    unsigned int i = super_ss_list[level].head;
    for (int checked = 0; checked < levelCount; checked++) {
      super_ss_list_level[level][i].search_completed = 0;
      i = (i + 1) % MAX_SEALED_LEVEL[level];
    }
  }
  xil_printf("All search flags reset\r\n");
}

int findNextSsFromLevel(enum SS_CONDITION_TYPE conditionType, int specificLevel) {
  unsigned int vector_index_start_lpn;
  int i;
  if (conditionType == SS_CONDITION_READY_FOR_SEARCH) { // Using bruteforce, search Lv0
    for (int level = specificLevel; level < MAX_LEVEL; level++) {
      int levelCount = super_level_info->level_count[level];
      if (levelCount <= 0) continue;

      i = super_ss_list[level].head;
      for (int checked = 0; checked < levelCount; checked++) {
        if (!super_ss_list_level[level][i].search_completed && super_ss_list_level[level][i].vector_index_start_lpn == INVALID_LPN) {
          SET_TARGET(conditionType, &super_ss_list_level[level][i]);
          super_ss_list_level[level][i].search_completed = 1;
          return 1;
        }
        i = (i + 1) % MAX_SEALED_LEVEL[level];
      }
    }
  }
  CLEAR_TARGET(conditionType);
  return 0;
}
/**
 * @brief Find next Sealed Segment matching the condition
 *
 * @return 1 if a matching Sealed Segment was found, 0 otherwise
 */
int findNextSs(enum SS_CONDITION_TYPE conditionType) {
  unsigned int i;
  int levelCount;
  unsigned int vector_index_start_lpn;
  int entered = 0, verified = 0;

  for (int level = 1; level < MAX_LEVEL; level++) {
    entered = 1;
    levelCount = super_level_info->level_count[level];
    if (levelCount <= 0) continue;

    i = super_ss_list[level].head;
    for (int checked = 0; checked < levelCount; checked++) {
      vector_index_start_lpn = super_ss_list_level[level][i].vector_index_start_lpn;
      verified = 1;

      if (CHECK_CONDITION(conditionType, vector_index_start_lpn)) {
        char shouldProcess = 0;
        if (conditionType == SS_CONDITION_NEEDS_INDEXING) {
          shouldProcess = 1;
        }
        else if (conditionType == SS_CONDITION_READY_FOR_SEARCH) {
          shouldProcess = !super_ss_list_level[level][i].search_completed;
        }
        else {
          assert(!"None");
        }

        if (shouldProcess) {
          SET_TARGET(conditionType, &super_ss_list_level[level][i]);
          if (conditionType == SS_CONDITION_READY_FOR_SEARCH) {
            super_ss_list_level[level][i].search_completed = 1;
          }
          return 1;
        }
      }
      if (conditionType == SS_CONDITION_NEEDS_INDEXING && super_ss_list[level].tail != snapshot_tail_per_level[level]) {
        if (i == snapshot_tail_per_level[level]) break;
      }
      i = (i + 1) % MAX_SEALED_LEVEL[level];
    }
  }

  /* All levels checked but no target found */
  CLEAR_TARGET(conditionType);
  if (entered && verified) {
  }
  else {
    xil_printf("[WARNING] Entered=%d, Verified=%d, probably there is no segment in the upper level than 0.\r\n", entered, verified);
    return 0;
  }

  return 0;
}

/**
 * @brief Assigns the next Sealed Segment that needs indexing with brute-force loop traversal
 */
void assignNextSsToIndex(void) {
  int result = findNextSs(SS_CONDITION_NEEDS_INDEXING);
}
/**
 * @brief Assigns the next Sealed Segment that needs searching with brute-force loop traversal
 */
void assignNextSsToSearch(void) {
  findNextSs(SS_CONDITION_READY_FOR_SEARCH);
}