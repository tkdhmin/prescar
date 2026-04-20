#include "hnsw.h"

#include "../memory_map.h"
#include "binary_heap.h"
#include "float.h"
#include "hnsw_config.h"
#include "xil_printf.h"
extern g_index_build_resume_point;
extern NVME_CONTEXT g_nvmeTask;


int successFlag = 1;
/**
 * @brief Prints a float array with 6 decimal places using xil_printf.
 *
 * Each float is printed in the format: [-]integer.decimal, separated by commas.
 * Negative values are handled manually. Output ends with a newline.
 *
 * @param arr   Pointer to the float array.
 * @param count Number of elements in the array.
 */

void printFloatArray(float* arr, int count) {
  for (int i = 0; i < count; i++) {
    if (i > 0) xil_printf(", ");

    float f = arr[i];
    if (f < 0) {
      xil_printf("-");
      f = -f;
    }
    int integer_part = (int)f;
    int decimal_part = (int)((f - integer_part) * 1000000);
    xil_printf("%d.%06d ", integer_part, decimal_part);
  }
}

/**
 * @brief Generates a random level based on a probability distribution.
 *
 * This function uses a random number generator (`vectorssd_rand()`) and
 * compares it with a probability threshold (`LEVEL_PROBABILITY`) to determine
 * the level. The function continues to increase the level while the random
 * number is below the threshold and the level is less than `MAX_LEVELS - 1`.
 *
 * @return The generated level, ranging from 0 to `MAX_LEVELS - 1`.
 */
static int getRandomLevel() {
  int level = 0;
  while ((vectorssd_rand() & 0xFFFF) < (int)(0xFFFF * LEVEL_PROBABILITY) && level < MAX_LEVELS - 1) {
    level++;
  }
  char result = (char)level;
  return level;
}

/**
 * @brief Initialize on-demand entries of HNSW index for vector indexing
 *
 * This function initializes HNSW on-demand entries for the specified Sealed Segment.
 * It performs the following steps:
 * 1. Loads the Sealed Segment index node from NAND flash memory.
 * 2. Initializes the HNSW index structure with basic attributes.
 * 3. Creates HNSW node for each vector of the Sealed Segment, assigning the random
 * levels to each.
 * 4. Tracks the entry point, which is likely to be selected by one who has
 * top-most max_level.
 * 5. Writes batches of nodes to NAND flash memory as an on-demand entry.
 */
static void initializeIndexWith(SUPER_SEALED_SEGMENT_INFO* targetSsForIndexing, unsigned int* reverse_write_pointer_lpn) {
  HNSWOndemandEntry* newEntry = 0;
  SEALED_SEGMENT_INDEX_NODE* ssIndexNode = (SEALED_SEGMENT_INDEX_NODE*)SEALED_SEGMENT_INDEX_BUFFER;
  VectorIndex* vectorIndex = (VectorIndex*)VECTOR_INDEX_START_ADDR;
  HNSWIndex* hnswIndex = (HNSWIndex*)(&(vectorIndex->hnsw));


  // HNSW's attributes processing
  SET_SEGMENT_ID(hnswIndex->segmentId);
  hnswIndex->entryPointKeyIndex = 0;
  hnswIndex->maxLevel = 0;
  CLEAR_VISITED(hnswIndex);

  unsigned int dataBufEntry;
  unsigned int accVectorCount = 0;
  unsigned int entryIndex = 0;
  unsigned int try
  = 0;
  unsigned int vector_size = 0;
  if (targetSsForIndexing->total_entry % NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK == 0) {
    vector_size = targetSsForIndexing->total_entry / NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK;
  }
  else {
    vector_size = targetSsForIndexing->total_entry / NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK + 1;
  }
  unsigned int checkpointed_reverse_write_pointer_lpn = (*reverse_write_pointer_lpn);
  unsigned int lpnOfNewEntry = (*reverse_write_pointer_lpn) - vector_size;
  (*reverse_write_pointer_lpn) = lpnOfNewEntry;

  unsigned int accNodeIdx = 0;

  for (unsigned int i = 0; i < targetSsForIndexing->total_entry; i++) {
    if (accVectorCount % NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK == 0) {
      if (newEntry) {
        Xil_DCacheFlushRange((INTPTR)newEntry, sizeof(HNSWOndemandEntry));
      }

      for (try = 0; try < 2; try++) {
        dataBufEntry = AllocateDataBuf(1);
        if (dataBufEntry != DATA_BUF_FAIL) {
          break;
        }
        FlushAllDataBuf();
        SyncAllLowLevelReqDone();
      }
      if (dataBufEntry == DATA_BUF_FAIL) {
        assert(!"unexpected buffer allocation fail error at initializeIndexWith\n");
      }
      dataBufMapPtr->dataBuf[dataBufEntry].full = 4;
      dataBufMapPtr->dataBuf[dataBufEntry].dirty = DATA_BUF_DIRTY;
      dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr = lpnOfNewEntry;
      hnswIndex->ondemandEntryLpn[entryIndex++] = lpnOfNewEntry;
      PutToDataBufHashList(dataBufEntry);
      newEntry = (HNSWOndemandEntry*)dataBufEntry2DramAddr(dataBufEntry);
      Xil_DCacheInvalidateRange((INTPTR)newEntry, BYTES_PER_DATA_REGION_OF_SLICE);

      accVectorCount = 0;
      lpnOfNewEntry++;
      if (lpnOfNewEntry > checkpointed_reverse_write_pointer_lpn) {
        xil_printf("%d > %d (%d)\r\n", lpnOfNewEntry, checkpointed_reverse_write_pointer_lpn, *reverse_write_pointer_lpn);
        assert(FALSE);
      }
      SyncAllLowLevelReqDone();
    }

    // NOTE: The value of the key is temporarily assigned.
    newEntry->hnswnodes[accVectorCount].key = ssIndexNode[i].key;
    for (int j = 0; j < MAX_LEVELS; j++) {
      newEntry->hnswnodes[accVectorCount].links[j].count = 0;
      for (int k = 0; k < M_PARAM; k++) {
        newEntry->hnswnodes[accVectorCount].links[j].neighbors[k] = 0;
      }
    }
    // NOTE: Temporarily assinged custom level.
    if (i < MAX_LEVELS)
      newEntry->hnswnodes[accVectorCount].maxLevel = (int)i;
    else
      newEntry->hnswnodes[accVectorCount].maxLevel = 0;


    if (hnswIndex->maxLevel < newEntry->hnswnodes[accVectorCount].maxLevel) {
      hnswIndex->maxLevel = newEntry->hnswnodes[accVectorCount].maxLevel;
      hnswIndex->entryPointKeyIndex = i;
    }

    assert(newEntry->hnswnodes[accVectorCount].maxLevel <= MAX_LEVELS);
    Xil_DCacheFlushRange((INTPTR)newEntry, sizeof(HNSWOndemandEntry));

    accVectorCount++;

  }
  FlushAllDataBuf();
  SyncAllLowLevelReqDone();

}

/**
 * @brief Get the index of the key data in a Sealed Segment.
 *
 * @note Ensure that the target Sealed Segment is stored to `SEALED_SEGMENT_INDEX_BUFFER`
 * before triggering this function.
 */
unsigned int getKeyIndexFromSsWith(const unsigned int keyData) {
  SEALED_SEGMENT_INDEX_NODE* ssIndexNode = (SEALED_SEGMENT_INDEX_NODE*)SEALED_SEGMENT_INDEX_BUFFER;
  for (unsigned int i = 0; i < targetSsForIndexing->total_entry; i++) {
    if (ssIndexNode[i].key == keyData) return i;
  }
  return NOT_FOUND;
}

/**
 * @brief Loads an HNSW node from DRAM or storage into memory on demand.
 *
 * This function checks whether the desired HNSW node (identified by
 * `ondemandEntryLpnIndex`) is present in the data buffer (acting as a cache).
 * If not, it triggers a data load from storage. Once the data is available, it
 * synchronizes low-level I/O and returns a pointer to the HNSW node data in
 * DRAM.
 *
 * @return Pointer to the loaded HNSW node (HNSWOndemandEntry). Returns NULL if
 * loading fails.
 */
HNSWOndemandEntry* loadHnswNode(const unsigned int keyIndex, unsigned* need_sync_ret) {

  VectorIndex* vectorIndex = (VectorIndex*)VECTOR_INDEX_START_ADDR;
  HNSWIndex* hnswIndex = (HNSWIndex*)(&(vectorIndex->hnsw));
  XTime st, ed;

  unsigned int entryIndex = keyIndex / NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK;
  unsigned int need_sync = 0;
  unsigned int dataBufEntry = CheckDataBufHitWithLSA(hnswIndex->ondemandEntryLpn[entryIndex]);

  if (dataBufEntry == DATA_BUF_FAIL) {
    dataBufEntry = getSliceToBuf(hnswIndex->ondemandEntryLpn[entryIndex]);
    need_sync = 1;
    assert(dataBufEntry != DATA_BUF_FAIL);
    SyncAllLowLevelReqDone();
    Xil_DCacheInvalidateRange((INTPTR)dataBufEntry2DramAddr(dataBufEntry), sizeof(HNSWOndemandEntry));
  }

  unsigned int hnswNodedramAddr = dataBufEntry2DramAddr(dataBufEntry);
  HNSWOndemandEntry* result = (HNSWOndemandEntry*)hnswNodedramAddr;

  if (need_sync_ret && (need_sync == 1))
    *need_sync_ret = need_sync;

  return result;
}

static int loadRawVectorData(unsigned int retDramAddr[], unsigned int startLba, unsigned int endLba,
  unsigned int* need_sync_ret) {
  unsigned int need_sync = 0;
  unsigned int i = 0;
  unsigned int dramAddrIndex = 0;
  unsigned int startLsa = startLba / NVME_BLOCKS_PER_SLICE;
  unsigned int endLsa = endLba / NVME_BLOCKS_PER_SLICE;
  unsigned int dramAddrReadN = endLsa - startLsa + 1;

  if (endLba < startLba) {
    xil_printf("ERROR: endLba(%u) < startLba(%u)\r\n", endLba, startLba);
    successFlag = 0;

    return 0;
  }
  // NOTE(Dhmin): 10 is a magic number
  if (dramAddrReadN > 10) {

    SEALED_SEGMENT_DATA_NODE* ssDataNode = (SEALED_SEGMENT_DATA_NODE*)SEALED_SEGMENT_DATA_BUFFER;
    SEALED_SEGMENT_INDEX_NODE* ssIndexNode = (SEALED_SEGMENT_INDEX_NODE*)SEALED_SEGMENT_INDEX_BUFFER;
    unsigned int j;
    for (j = 0; j < targetSsForIndexing->total_entry / 1000; j += 2) {
      xil_printf("k[%d], %d (with V: %d %d)\t", j, ssIndexNode[j].key, ssDataNode[j].lba, ssDataNode[j].length);
    }
    j = targetSsForIndexing->total_entry - 1;
    xil_printf("k[%d], %d (with V: %d %d)\t", j, ssIndexNode[j].key, ssDataNode[j].lba, ssDataNode[j].length);
    xil_printf("\r\n,Total Entry: %d \r\n", targetSsForIndexing->total_entry);
    xil_printf("ERROR: dramAddrReadN too large: %u (%ld, %ld)\r\n", dramAddrReadN, startLba, endLba);
    assert(FALSE);
    successFlag = 0;
    return 0;
  }

  do {
    unsigned int dataBufEntry = CheckDataBufHitWithLSA(startLsa + i);
    if (dataBufEntry == DATA_BUF_FAIL) {
      dataBufEntry = getSliceToBuf(startLsa + i);
      need_sync = 1;
    }
    retDramAddr[dramAddrIndex] = dataBufEntry2DramAddr(dataBufEntry);
    PinorUnPin1PageBuf(retDramAddr[dramAddrIndex], 1); // next read buf can evict by next iteration, so pin.
    dramAddrIndex++;
    i++;
    assert(dramAddrIndex < 100);
  } while (startLsa + i < endLsa);

  PinOrUnPinBuf(retDramAddr, endLsa - startLsa + 1, 0);
  if (need_sync_ret && need_sync) {
    *need_sync_ret = need_sync;
    for (i = 0;i < dramAddrReadN;i++) {
      Xil_DCacheInvalidateRange((INTPTR)retDramAddr[i], BYTES_PER_DATA_REGION_OF_SLICE);
    }
  }
  retDramAddr[0] = retDramAddr[0] + (startLba % NVME_BLOCKS_PER_SLICE) * BYTES_PER_NVME_BLOCK;

  return (dramAddrReadN);
}


/**
 * @brief Loads an neighbor's vector data
 */
int loadNeighborVectorData(unsigned int neighborIdx, unsigned int neighborVectorDramAddr[],
  unsigned int* need_sync_ret) {
  SEALED_SEGMENT_DATA_NODE* ssDataNode = (SEALED_SEGMENT_DATA_NODE*)SEALED_SEGMENT_DATA_BUFFER;
  unsigned int keyIndex = neighborIdx;
  unsigned int need_sync = 0;

  VectorIndex* vectorIndex = (VectorIndex*)VECTOR_INDEX_START_ADDR;
  HNSWIndex* hnswIndex = (HNSWIndex*)(&(vectorIndex->hnsw));

  if (keyIndex == NOT_FOUND) {
    assert(!"Not found in Sealed Segment");
    return 0;
  }

  unsigned int startLba = ssDataNode[keyIndex].lba;
  unsigned int nlb = (ssDataNode[keyIndex].length / BYTES_PER_NVME_BLOCK);
  unsigned int endLba = (startLba + nlb);

  unsigned int dramAddrReadN = loadRawVectorData(neighborVectorDramAddr, startLba, endLba, need_sync_ret);

  return dramAddrReadN;
}

/**
 * @brief Performs a greedy search on a specific HNSW level to find the nearest
 * node.
 *
 * This function starts from the given entry point and iteratively explores
 * neighboring nodes at the specified level in the HNSW graph. It loads vector
 * data either from DRAM or NAND as needed, calculates distances, and updates
 * the search position if a closer neighbor is found.
 *
 * @param ondemandEntryIndex   Index of the on-demand HNSW entry to search from.
 * @param queryVectorDramAddr  DRAM address of the query vector (assumed to be
 * already loaded).
 * @param entryPointIdx        Index of the entry point node in the Sealed Segment to
 * start the search.
 * @param level                The level in the HNSW graph where the search is
 * performed.
 *
 * @return Index of the closest node found at the specified level using greedy
 * search.
 */
static unsigned int greedySearchLevel(const unsigned int queryVectorDramAddr[], const unsigned int entryPointKeyIndex,
  const int level, const int queryVectorIdx) {
  SEALED_SEGMENT_DATA_NODE* ssDataNode = (SEALED_SEGMENT_DATA_NODE*)SEALED_SEGMENT_DATA_BUFFER;
  VectorIndex* vectorIndex = (VectorIndex*)VECTOR_INDEX_START_ADDR;
  HNSWIndex* hnswIndex = (HNSWIndex*)(&(vectorIndex->hnsw));
  XTime st, ed;
  unsigned int need_sync = 0;
  // Array for storing pointers to result vectors (allocated only once)
  unsigned int dramAddr[(PREDEFINED_VECTOR_SIZE / BYTES_PER_DATA_REGION_OF_SLICE) + 2] = { 0 };
  unsigned int neighborVectorDramAddr[(PREDEFINED_VECTOR_SIZE / BYTES_PER_DATA_REGION_OF_SLICE) + 2] = { 0 };

  // Initialize the current search position and the modification flag
  char changed;
  unsigned int curPointKeyIdx = entryPointKeyIndex;
  do {
    changed = 0;
    need_sync = 0;
    unsigned int startLba = ssDataNode[curPointKeyIdx].lba;
    unsigned int nlb = ssDataNode[curPointKeyIdx].length / BYTES_PER_NVME_BLOCK;
    unsigned int endLba = (startLba + nlb);
    HNSWOndemandEntry* nodePtr;
    if (!IS_ONDEMAND_FIELD((int)(curPointKeyIdx / NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK))) {
      nodePtr = loadHnswNode(curPointKeyIdx, &need_sync);  // prefetch and pin
      assert(nodePtr);
      if (need_sync) {
        SyncAllLowLevelReqDone();
        Xil_DCacheInvalidateRange((INTPTR)nodePtr, sizeof(HNSWOndemandEntry));
        need_sync = 0;
      }
      PinorUnPin1PageBuf(nodePtr, 1); // pin
    }
    else {
      xil_printf("hnswOndemandFieldOccupiedBy: %d == %d (=%d / %d)\r\n", hnswOndemandFieldOccupiedBy, curPointKeyIdx / NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK, curPointKeyIdx, NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK);
      assert(FALSE);
    }

    unsigned int dramAddrIndex = 0;
    unsigned int maxSlices = loadRawVectorData(dramAddr, startLba, endLba, &need_sync);
    if (queryVectorIdx == -1 && successFlag == 0) {
      SEALED_SEGMENT_INDEX_NODE* ssIndexNode = (SEALED_SEGMENT_INDEX_NODE*)SEALED_SEGMENT_INDEX_BUFFER;
      xil_printf("[GREEDY_SEARCH_LEVEL] startLba=%d, endLba=%d, key=%d, keyIdx=%d\r\n", ssDataNode[curPointKeyIdx].lba, endLba, ssIndexNode[curPointKeyIdx].key, curPointKeyIdx);
    }
    if (successFlag == 0) break;

#if VECTOR_BUILD_PREFETCH_DEGREE> 0
    unsigned int prefetched = 0;
    WaitPrefetchedNANDOperation(dramAddr, dramAddrN);
    for (int j = 0; j < (PREDEFINED_VECTOR_SIZE / BYTES_PER_DATA_REGION_OF_SLICE) + 2; j++) {
      if (dramAddr[j] != 0) {
        if (j == 0) {
          unsigned int originalAddr = dramAddr[j] - (startLba % NVME_BLOCKS_PER_SLICE) * BYTES_PER_NVME_BLOCK;
          Xil_DCacheInvalidateRange(originalAddr, BYTES_PER_DATA_REGION_OF_SLICE);
        }
        else {
          Xil_DCacheInvalidateRange(dramAddr[j], BYTES_PER_DATA_REGION_OF_SLICE);
        }
      }
    }
    float curDist = calcVectorDistance(queryVectorDramAddr, dramAddr);
    SyncAllLowLevelReqDone();
#else
    if (need_sync) {
      SyncAllLowLevelReqDone();
      for (int j = 0; j < maxSlices; j++) {
        Xil_DCacheInvalidateRange((INTPTR)dramAddr[j], BYTES_PER_DATA_REGION_OF_SLICE);
      }
      need_sync = 0;
    }

    if (!nodePtr) {
      assert(!"Failed loading HNSW node.");
      return 0;
    }
    float curDist = calcVectorDistance(queryVectorDramAddr, dramAddr);

#endif
    unsigned int nodeIndex = curPointKeyIdx % NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK;
    if (nodePtr->hnswnodes[nodeIndex].links[level].count > EF_CONSTRUCTION) {
      xil_printf("At Lv.%d, nodeIndex.%d nodePtr->hnswnodes[nodeIndex].links[level].count: %d\r\n", level, nodeIndex, nodePtr->hnswnodes[nodeIndex].links[level].count);
      for (unsigned int kk = 0;kk < EF_CONSTRUCTION; kk++) {
        xil_printf("nodePtr->hnswnodes[nodeIndex].links[level].neighbors[%d]: ", kk, nodePtr->hnswnodes[nodeIndex].links[level].neighbors[kk]);
      }
      assert(FALSE);
    }
    for (unsigned int i = 0; i < nodePtr->hnswnodes[nodeIndex].links[level].count; i++) {
      unsigned int neighborIdx = nodePtr->hnswnodes[nodeIndex].links[level].neighbors[i];
#if VECTOR_BUILD_PREFETCH_DEGREE> 0
      prefetchedVectorDataAddr prefetchedVectorDataAddr[VECTOR_BUILD_PREFETCH_DEGREE];
      for (p = 0;prefetched < i + VECTOR_BUILD_PREFETCH_DEGREE;prefetched++, p++) {
        unsigned int prefetchNeighborIdx = nodePtr->hnswnodes[nodeIndex].links[level].neighbors[prefetched];
        prefetchedVectorDataAddr[p].n =
          loadNeighborVectorData(prefetchNeighborIdx, prefetchedVectorDataAddr[p].dramAddr, &need_sync);

        prefetchedVectorDataAddr[p].aux = prefetchNeighborIdx;
        PinOrUnPinBuf(prefetchedVectorDataAddr[p].dramAddr, prefetchedVectorDataAddr[p].n, 1);
      }

      for (unsigned int tmp = 0;tmp < p;tmp++) {
        PinOrUnPinBuf(prefetchedVectorDataAddr[tmp].dramAddr, prefetchedVectorDataAddr[tmp].n, 0);
        if (prefetchedVectorDataAddr[tmp].aux == neighborIdx) {
          WaitPrefetchedNANDOperation(prefetchedVectorDataAddr[tmp].dramAddr, prefetchedVectorDataAddr[tmp].dramAddrN);
        }
      }
#endif
      int maxSlices = loadNeighborVectorData(neighborIdx, neighborVectorDramAddr, &need_sync);
      if (queryVectorIdx == -1 && successFlag == 0) {
        SEALED_SEGMENT_INDEX_NODE* ssIndexNode = (SEALED_SEGMENT_INDEX_NODE*)SEALED_SEGMENT_INDEX_BUFFER;
        unsigned int n_startlba = ssDataNode[neighborIdx].lba;
        unsigned int n_nlb = ssDataNode[neighborIdx].length / BYTES_PER_NVME_BLOCK;
        unsigned int n_endlba = (n_startlba + n_nlb);
      }
      if (!maxSlices) continue; // NOTE: When loading fails, go to the other neighbors.

#if VECTOR_BUILD_PREFETCH_DEGREE>0
      assert(!need_sync);
#endif 
      if (need_sync) {
        SyncAllLowLevelReqDone();
        for (int j = 0; j < maxSlices; j++) {
          Xil_DCacheInvalidateRange((INTPTR)neighborVectorDramAddr[j], BYTES_PER_DATA_REGION_OF_SLICE);
        }
        need_sync = 0;
      }
      float neighborDist = calcVectorDistance(queryVectorDramAddr, neighborVectorDramAddr);

      if (curDist > neighborDist) {
        curPointKeyIdx = neighborIdx;
        curDist = neighborDist;
        changed = 1;
        break;
      }
    }
    if (!IS_ONDEMAND_FIELD((int)(curPointKeyIdx / NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK))) {
      PinorUnPin1PageBuf(nodePtr, 0);
    }
  } while (changed);

  return curPointKeyIdx;
}

/**
 * Searches the HNSW graph for neighbors around the query vector
 *
 * @param queryVectorDramAddr Array containing the query vector's DRAM address
 * @param entryPointIdx Starting point index for the search
 * @param level Current level in the HNSW graph to search
 * @param ef Expansion factor controlling the search breadth
 * @param candidates Output array to store the candidate indices
 * @param distances Output array to store the distances to candidates
 * @param candCount Pointer to store the number of candidates found
 */
static void searchNeighborsEf(const unsigned int queryVectorDramAddr[], const unsigned int entryPointIdx,
  const int level, const unsigned int ef, unsigned int candidates[], float distances[],
  int* candCount, const int queryVectorIdx) {
  SEALED_SEGMENT_INDEX_NODE* ssIndexNode = (SEALED_SEGMENT_INDEX_NODE*)SEALED_SEGMENT_INDEX_BUFFER;
  SEALED_SEGMENT_DATA_NODE* ssDataNode = (SEALED_SEGMENT_DATA_NODE*)SEALED_SEGMENT_DATA_BUFFER;
  VectorIndex* vectorIndex = (VectorIndex*)VECTOR_INDEX_START_ADDR;
  HNSWIndex* hnswIndex = (HNSWIndex*)(&(vectorIndex->hnsw));
  XTime st, ed;

  if (ef > EF_CONSTRUCTION){
    assert(!"ef must be less than the supported EF_CONSTRUCTION");
  }

  CLEAR_VISITED(hnswIndex);

  MinHeap to_visit;
  MaxHeap result_set;
  unsigned int need_sync = 0;
  min_heap_init(&to_visit);
  max_heap_init(&result_set, ef + 1);

  unsigned int dramAddr[(PREDEFINED_VECTOR_SIZE / BYTES_PER_DATA_REGION_OF_SLICE) + 2] = {
      0,
  };
  unsigned int neighborVectorDramAddr[(PREDEFINED_VECTOR_SIZE / BYTES_PER_DATA_REGION_OF_SLICE) + 2] = {
      0,
  };
  unsigned int startLba = ssDataNode[entryPointIdx].lba;
  unsigned int nlb = (ssDataNode[entryPointIdx].length / BYTES_PER_NVME_BLOCK);
  unsigned int endLba = (startLba + nlb);

  int maxSlices = loadRawVectorData(dramAddr, startLba, endLba, &need_sync);
  if (IS_INDEX_NOT_BUILDING() && successFlag == 0) {
    xil_printf("[SEARCH_N] n_startLba=%d, n_endLba=%d, n_key=%d, n_keyIdx=%d\r\n", startLba, endLba, ssIndexNode[entryPointIdx].key, entryPointIdx);
  }
  if (successFlag == 0) return;
  if (need_sync) {
    SyncAllLowLevelReqDone();
    for (int j = 0; j < maxSlices; j++) {
      Xil_DCacheInvalidateRange((INTPTR)dramAddr[j], BYTES_PER_DATA_REGION_OF_SLICE);
    }
    need_sync = 0;
  }

  float curDist = calcVectorDistance(queryVectorDramAddr, dramAddr);


  max_heap_insert(&result_set, entryPointIdx, curDist);
  min_heap_insert(&to_visit, entryPointIdx, curDist);
  SET_VISITED(hnswIndex, entryPointIdx);

  int curPointKeyIdx;
  char breaking_signal = 0;
  HNSWOndemandEntry* nodePtr = 0xffffffff;
  while (to_visit.size > 0 && !breaking_signal) {
    min_heap_pop(&to_visit, &curPointKeyIdx, &curDist);
    if (result_set.size > 0 && result_set.distances[0] < curDist) break;
    nodePtr = loadHnswNode(curPointKeyIdx, &need_sync);
    if (!nodePtr) {
      assert(!"Failed loading HNSW node.");
    }
    SyncAllLowLevelReqDone();
    Xil_DCacheInvalidateRange((INTPTR)nodePtr, BYTES_PER_DATA_REGION_OF_SLICE);
    PinorUnPin1PageBuf(nodePtr, 1);

    unsigned int nodeIndex = curPointKeyIdx % NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK;
    if (nodePtr->hnswnodes[nodeIndex].maxLevel >= MAX_LEVELS) {
      xil_printf("=== CORRUPTION DETECTED ===\r\n");
      xil_printf("curPointKeyIdx: %d, nodeIndex: %d\r\n", curPointKeyIdx, nodeIndex);
      xil_printf("nodePtr: 0x%08X\r\n", (unsigned int)nodePtr);
      xil_printf("maxLevel: %d (should be < %d)\r\n", nodePtr->hnswnodes[nodeIndex].maxLevel, MAX_LEVELS);

      xil_printf("Cache line dump:\r\n");
      unsigned char* ptr = (unsigned char*)nodePtr;
      for (int k = 0; k < 128; k += 16) {
        xil_printf("  [%03d]: ", k);
        for (int j = 0; j < 16; j++) {
          xil_printf("%02X ", ptr[k + j]);
        }
        xil_printf("\r\n");
      }

      xil_printf("ERROR: Invalid level %d in searchNeighborsEf()\r\n", nodePtr->hnswnodes[nodeIndex].maxLevel);
      assert(FALSE);
    }
    if (nodePtr->hnswnodes[nodeIndex].links[level].count > M_PARAM) {
      xil_printf("[WARNING] %d > M_PARAM(%d)\r\n", nodePtr->hnswnodes[nodeIndex].links[level].count, M_PARAM);
      nodePtr = loadHnswNode(curPointKeyIdx, &need_sync);
      if (!nodePtr) {
        assert(!"Failed loading HNSW node.");
      }
      SyncAllLowLevelReqDone();
      Xil_DCacheInvalidateRange((INTPTR)nodePtr, BYTES_PER_DATA_REGION_OF_SLICE);
      PinorUnPin1PageBuf(nodePtr, 1);
    }

    for (int i = 0; i < nodePtr->hnswnodes[nodeIndex].links[level].count; i++) {
      unsigned int neighborIdx = nodePtr->hnswnodes[nodeIndex].links[level].neighbors[i];
      if (IS_VISITED(hnswIndex, neighborIdx)) {
        continue;
      }
      SET_VISITED(hnswIndex, neighborIdx);
      maxSlices = loadNeighborVectorData(neighborIdx, neighborVectorDramAddr, &need_sync);
      if (IS_INDEX_NOT_BUILDING() && successFlag == 0) {
        unsigned int n_startlba = ssDataNode[neighborIdx].lba;
        unsigned int n_nlb = ssDataNode[neighborIdx].length / BYTES_PER_NVME_BLOCK;
        unsigned int n_endlba = (n_startlba + n_nlb);
        xil_printf("[SEARCH_N 2(NEIG)] n_startLba=%d, n_endLba=%d, n_key=%d, n_keyIdx=%d\r\n", n_startlba, n_endlba, ssIndexNode[neighborIdx].key, neighborIdx);
      }
      if (!maxSlices) continue; // When loading fails, go to the other neighbors.

      if (need_sync) {
        SyncAllLowLevelReqDone();
        for (int j = 0; j < maxSlices; j++) {
          Xil_DCacheInvalidateRange((INTPTR)neighborVectorDramAddr[j], BYTES_PER_DATA_REGION_OF_SLICE);
        }
        need_sync = 0;
      }

      float neighborDist = calcVectorDistance(queryVectorDramAddr, neighborVectorDramAddr);

      if (result_set.size < ef || neighborDist < result_set.distances[0]) {
        max_heap_insert(&result_set, neighborIdx, neighborDist);
        min_heap_insert(&to_visit, neighborIdx, neighborDist);
      }
      if (result_set.size >= ef) {
        breaking_signal = 1;
        PinorUnPin1PageBuf(nodePtr, 0);
        break;
      }
    }
    PinorUnPin1PageBuf(nodePtr, 0);
  }

  for (int i = result_set.size - 1, j = 0; i >= 0; i--, j++) {
    candidates[j] = result_set.indices[i];
    distances[j] = result_set.distances[i];
  }
  *candCount = result_set.size;

}


static void connectNeighbors(unsigned int vectorKeyIndex, unsigned int candidates[], float distances[], const int candCount, const int level) {
  SEALED_SEGMENT_INDEX_NODE* ssIndexNode = (SEALED_SEGMENT_INDEX_NODE*)SEALED_SEGMENT_INDEX_BUFFER;
  VectorIndex* vectorIndex = (VectorIndex*)VECTOR_INDEX_START_ADDR;
  HNSWIndex* hnswIndex = (HNSWIndex*)(&(vectorIndex->hnsw));

  HNSWOndemandEntry* curHnswEntry = 0;
  unsigned int need_sync = 0;
  if (!IS_ONDEMAND_FIELD((int)(vectorKeyIndex / NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK))) {
    curHnswEntry = loadHnswNode(vectorKeyIndex, &need_sync);

    if (!curHnswEntry) {
      assert(!"Failed loading HNSW node.");
    }
    if (need_sync) {
      SyncAllLowLevelReqDone();
      Xil_DCacheInvalidateRange((INTPTR)curHnswEntry, sizeof(HNSWOndemandEntry));
      need_sync = 0;
    }
  }
  else {
    curHnswEntry = (HNSWOndemandEntry*)HNSW_ONDEMAND_ENTRY_START_ADDR;
    xil_printf("hnswOndemandFieldOccupiedBy: %d == %d (=%d / %d)\r\n", hnswOndemandFieldOccupiedBy, vectorKeyIndex / NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK, vectorKeyIndex, NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK);
    assert(FALSE);
  }

  unsigned int entryIndex = vectorKeyIndex / NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK;
  unsigned int nodeIndex = vectorKeyIndex % NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK;
  need_sync = 0;
  char uniqueCount = 0;
  unsigned int uniqueCandidates[M_PARAM];
  int minCount = (candCount < M_PARAM) ? candCount : M_PARAM;
  float uniqueDistances[M_PARAM];

  // De-duplication
  for (unsigned int i = 0; i < minCount; i++) {
    // Skip to avoid self-referencing
    if (candidates[i] == vectorKeyIndex) continue;
    char isDuplicated = 0;
    for (unsigned j = 0; j < uniqueCount; j++) {
      if (uniqueCandidates[j] == candidates[i]) {
        isDuplicated = 1;
        break;
      }
    }
    if (!isDuplicated) {
      uniqueCandidates[uniqueCount] = candidates[i];
      uniqueDistances[uniqueCount] = distances[i];
      uniqueCount++;
    }
  }

  // Neighbor update and NAND write
  curHnswEntry->hnswnodes[nodeIndex].links[level].count = uniqueCount;
  if (uniqueCount > M_PARAM)
    assert(FALSE);
  for (int i = 0; i < uniqueCount;i++) {
    curHnswEntry->hnswnodes[nodeIndex].links[level].neighbors[i] = uniqueCandidates[i];
  }

  // Build bi-directional neighbors
  typedef struct {
    unsigned int idx;
    float dist;
    int orgPos;
  } NeighborDist;

  NeighborDist tempNeighbors[M_PARAM + 1];
  XTime st, ed;

  // Load HNSWNode from either DRAM or NAND
  unsigned int neighborVectorDramAddr[(PREDEFINED_VECTOR_SIZE / BYTES_PER_DATA_REGION_OF_SLICE) + 2] = { 0, 0 };
  unsigned int otherNeighborVectorDramAddr[(PREDEFINED_VECTOR_SIZE / BYTES_PER_DATA_REGION_OF_SLICE) + 2] = {
      0,
  };

  for (int i = 0; i < uniqueCount; i++) {
    unsigned int neighborIdx = uniqueCandidates[i];
    unsigned int neighborNodeIndex = neighborIdx % NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK;
    unsigned int neighborEntryIndex = neighborIdx / NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK;

    HNSWOndemandEntry* nodePtr = 0;
    if (1) {
      nodePtr = loadHnswNode(neighborIdx, &need_sync);
      if (need_sync) {
        SyncAllLowLevelReqDone();
        Xil_DCacheInvalidateRange((INTPTR)nodePtr, sizeof(HNSWOndemandEntry));
      }
      if (!nodePtr) {
        assert(!"Failed loading HNSW node.");
        return 0;
      }
      PinorUnPin1PageBuf(nodePtr, 1); // pin
    }
    else {
      nodePtr = (HNSWOndemandEntry*)HNSW_ONDEMAND_ENTRY_START_ADDR;
      xil_printf("Do not use HNSW_ONDEMAND_ENTRY_START_ADDR\r\n");
      assert(FALSE);
    }

    unsigned int curIncluded = 0;
    for (int j = 0; j < nodePtr->hnswnodes[neighborNodeIndex].links[level].count; j++) {
      if (nodePtr->hnswnodes[neighborNodeIndex].links[level].neighbors[j] == vectorKeyIndex) {
        curIncluded = 1;
        break;
      }
    }

    if (!curIncluded) {
      if (nodePtr->hnswnodes[neighborNodeIndex].links[level].count < M_PARAM) {
        nodePtr->hnswnodes[neighborNodeIndex].links[level].neighbors[nodePtr->hnswnodes[neighborNodeIndex].links[level].count] = vectorKeyIndex;
        nodePtr->hnswnodes[neighborNodeIndex].links[level].count++;
      }
      else {
        unsigned maxSlice1;
        maxSlice1 = loadNeighborVectorData(neighborIdx, neighborVectorDramAddr, &need_sync);
        if (!maxSlice1) continue;
        if (need_sync) {
          for (unsigned k = 0; k < maxSlice1;k++) {
            Xil_DCacheInvalidateRange((INTPTR)neighborVectorDramAddr[k], sizeof(HNSWOndemandEntry));
          }
          SyncAllLowLevelReqDone();
          need_sync = 0;
        }

        for (unsigned int j = 0; j < M_PARAM; j++) {
          tempNeighbors[j].idx = nodePtr->hnswnodes[neighborNodeIndex].links[level].neighbors[j];
          unsigned int maxSlice2 = loadNeighborVectorData(tempNeighbors[j].idx, otherNeighborVectorDramAddr, &need_sync);
          if (!maxSlice2) continue;

          if (need_sync) {
            for (unsigned k = 0; k < maxSlice2;k++) {
              Xil_DCacheInvalidateRange((INTPTR)otherNeighborVectorDramAddr[k], sizeof(HNSWOndemandEntry));
            }
            SyncAllLowLevelReqDone();
            need_sync = 0;
          }
          tempNeighbors[j].dist = calcVectorDistance(neighborVectorDramAddr, otherNeighborVectorDramAddr);
        }

        unsigned int farthestIdx = 0;
        float maxDist = tempNeighbors[0].dist;
        for (unsigned int j = 1; j <= M_PARAM; j++) {
          if (tempNeighbors[j].dist > maxDist) {
            maxDist = tempNeighbors[j].dist;
            farthestIdx = j;
          }
        }

        if (farthestIdx < M_PARAM) {
          for (unsigned int j = 0; j < M_PARAM; j++) {
            if (j != farthestIdx) {
              nodePtr->hnswnodes[neighborNodeIndex].links[level].neighbors[j] = tempNeighbors[j].idx;
            }
            else {
              nodePtr->hnswnodes[neighborNodeIndex].links[level].neighbors[j] = vectorKeyIndex;
            }
          }
        }
        Xil_DCacheFlushRange((INTPTR)nodePtr, sizeof(HNSWOndemandEntry));
      }
      if (1) {
        unsigned int dataBufEntry = dramAddr2DataBufEntry((unsigned int)nodePtr);
        dataBufMapPtr->dataBuf[dataBufEntry].full = 4;
        dataBufMapPtr->dataBuf[dataBufEntry].dirty = DATA_BUF_DIRTY;
      }
    }

    if (1) {
      PinorUnPin1PageBuf(nodePtr, 0);
    }
  }
}


void hnswOndemandPreemptiveIndexBuild(SUPER_SEALED_SEGMENT_INFO* targetSsForIndexing) { assert(!"Not implemented yet."); }

void debugMemoryMap() {
  xil_printf("=== Memory Map Debug ===\r\n");
  xil_printf("RESERVED1_START_ADDR: %X\r\n", RESERVED1_START_ADDR);
  xil_printf("BYTES_PER_DATA_REGION_OF_SLICE: %d\r\n", BYTES_PER_DATA_REGION_OF_SLICE);
  xil_printf("VECTOR_INDEX_START_ADDR: %X\r\n", VECTOR_INDEX_START_ADDR);
  xil_printf("sizeof(VectorIndex): %d\r\n", sizeof(VectorIndex));
  xil_printf("VECTOR_INDEX_SIZE: %d\r\n", sizeof(VectorIndex));

  unsigned int manual_hnsw_addr = VECTOR_INDEX_START_ADDR + sizeof(VectorIndex);
  xil_printf("Manual HNSW addr: %X\r\n", manual_hnsw_addr);
  xil_printf("Macro HNSW addr: %X\r\n", HNSW_ONDEMAND_ENTRY_START_ADDR);

  if (manual_hnsw_addr == HNSW_ONDEMAND_ENTRY_START_ADDR) {
    xil_printf("O Macro works correctly\r\n");
  }
  else {
    xil_printf("X Macro definition problem!\r\n");
  }
}

int buildOndemandHnsw(SUPER_SEALED_SEGMENT_INFO* targetSsForIndexing, unsigned int* reverse_write_pointer_lpn, const int currentIndex, const int endIndex, const int preempted) {
  successFlag = 1;
  SEALED_SEGMENT_INDEX_NODE* ssIndexNode = (SEALED_SEGMENT_INDEX_NODE*)SEALED_SEGMENT_INDEX_BUFFER;
  SEALED_SEGMENT_DATA_NODE* ssDataNode = (SEALED_SEGMENT_DATA_NODE*)SEALED_SEGMENT_DATA_BUFFER;
  VectorIndex* vectorIndex = (VectorIndex*)VECTOR_INDEX_START_ADDR;
  HNSWIndex* hnswIndex = (HNSWIndex*)(&(vectorIndex->hnsw));

  unsigned int need_sync = 0;

  if (currentIndex == 0 || preempted == 1) {
    manuallyFlushDirtyDataBuffer();
    clearAllDataBufferContents();

    memset(SEALED_SEGMENT_INDEX_BUFFER, 0, SEALED_SEGMENT_INDEX_BUFFER_SIZE);
    memset(SEALED_SEGMENT_DATA_BUFFER, 0, SEALED_SEGMENT_DATA_BUFFER_SIZE);
    TriggerInternalPagesRead(targetSsForIndexing->head_lpn, (unsigned int)ssIndexNode,
      targetSsForIndexing->index_size);
    TriggerInternalPagesRead(targetSsForIndexing->head_lpn + targetSsForIndexing->index_size,
      (unsigned int)ssDataNode, targetSsForIndexing->data_size);

    SyncAllLowLevelReqDone();

    Xil_DCacheInvalidateRange((INTPTR)ssIndexNode, targetSsForIndexing->index_size * BYTES_PER_DATA_REGION_OF_SLICE);
    Xil_DCacheInvalidateRange((INTPTR)ssDataNode, targetSsForIndexing->data_size * BYTES_PER_DATA_REGION_OF_SLICE);
    if (currentIndex == 0) {
      memset(VECTOR_INDEX_START_ADDR, 0, VECTOR_INDEX_SIZE);
      Xil_DCacheInvalidateRange((INTPTR)hnswIndex, VECTOR_INDEX_SIZE);
    }

    if (currentIndex == 0)
      initializeIndexWith(targetSsForIndexing, reverse_write_pointer_lpn);
  }

  if (currentIndex == 0)
    CLEAR_ONDEMAND_FIELD_INDICATOR();


  unsigned int candidateIdxs[EF_CONSTRUCTION] = { 0 };
  float candidateDistances[EF_CONSTRUCTION] = { FLT_MAX };
  unsigned int dataBufEntry;

  int ondemandEntryLpnIndex = -1;
  HNSWOndemandEntry* curHnswEntry = 0;
  for (unsigned int i = currentIndex; i <= endIndex; i++) {
    ondemandEntryLpnIndex = i / NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK;
    unsigned int nodeIndex = i % NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK;

    if ((i % NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK) == 0) {
      if (curHnswEntry)
        PinorUnPin1PageBuf(curHnswEntry, 0);
      curHnswEntry = loadHnswNode(i, &need_sync);
      if (need_sync) {
        Xil_DCacheInvalidateRange((INTPTR)curHnswEntry, sizeof(HNSWOndemandEntry));
        SyncAllLowLevelReqDone();
        need_sync = 0;
      }
      SET_ONDEMAND_FIELD_WITH(INVALID_LPN);
      PinorUnPin1PageBuf(curHnswEntry, 1);
    }
    unsigned int candCount = 0;
    unsigned int dramAddr[(PREDEFINED_VECTOR_SIZE / BYTES_PER_DATA_REGION_OF_SLICE) + 2] = { 0 };
    // Logical block address and offset info
    unsigned int startLba = ssDataNode[i].lba;
    unsigned int nlb = (ssDataNode[i].length / BYTES_PER_NVME_BLOCK);
    unsigned int endLba = (startLba + nlb);

    // Logical slice address and offset info
    unsigned int maxSlices = loadRawVectorData(dramAddr, startLba, endLba, &need_sync);

    if (successFlag == 0) {
      if (curHnswEntry)
        PinorUnPin1PageBuf(curHnswEntry, 0);
      return -1;
    }

    if (need_sync) {
      SyncAllLowLevelReqDone();
      for (int j = 0; j < maxSlices; j++) {
        Xil_DCacheInvalidateRange((INTPTR)dramAddr[j], BYTES_PER_DATA_REGION_OF_SLICE);
      }
      need_sync = 0;
    }

    unsigned int selectedEntryPoint = greedySearchLevel(dramAddr, hnswIndex->entryPointKeyIndex, hnswIndex->maxLevel, i);
    if (successFlag == 0) return -1;

    for (int level = curHnswEntry->hnswnodes[nodeIndex].maxLevel; level >= 0; level--) {
      searchNeighborsEf(dramAddr, selectedEntryPoint, level, EF_CONSTRUCTION, candidateIdxs, candidateDistances,
        &candCount, i);
      if (successFlag == 0) return -1;
      connectNeighbors(i, candidateIdxs, candidateDistances, candCount, level);
    }

    unsigned int start_lsa = ssDataNode[i].lba / NVME_BLOCKS_PER_SLICE;
    unsigned int end_lsa = (ssDataNode[i].lba + nlb) / NVME_BLOCKS_PER_SLICE;
    PinOrUnPinBuf(dramAddr, end_lsa - start_lsa + 1, 0);
  }
  if (curHnswEntry)
    PinorUnPin1PageBuf(curHnswEntry, 0);

  if (endIndex < targetSsForIndexing->total_entry - 1) {
    FlushAllDataBuf();
    SyncAllLowLevelReqDone();
    return 1;
  }

  Xil_DCacheFlushRange((INTPTR)curHnswEntry, BYTES_PER_DATA_REGION_OF_SLICE);
  FlushAllDataBuf();
  SyncAllLowLevelReqDone();

  unsigned int vector_index_size = (int)((sizeof(struct _VectorIndex) + BYTES_PER_DATA_REGION_OF_SLICE - 1) / BYTES_PER_DATA_REGION_OF_SLICE);
  unsigned int lpnOfIndex = (*reverse_write_pointer_lpn) - vector_index_size;
  (*reverse_write_pointer_lpn) = lpnOfIndex;
  targetSsForIndexing->vector_index_start_lpn = lpnOfIndex;
  targetSsForIndexing->vector_index_size = vector_index_size;
  Xil_DCacheFlushRange((INTPTR)VECTOR_INDEX_START_ADDR, sizeof(struct _VectorIndex));
  TriggerInternalPagesWrite(targetSsForIndexing->vector_index_start_lpn, VECTOR_INDEX_START_ADDR, targetSsForIndexing->vector_index_size);
  SyncAllLowLevelReqDone();

  CLEAR_ONDEMAND_FIELD_INDICATOR();

  return 0;
}

void checkDataBufContentBeforeFlush() {
  xil_printf("=== checkDataBufContentBeforeFlush ===\r\n");

  for (int i = 0; i < AVAILABLE_DATA_BUFFER_ENTRY_COUNT; i++) {
    if (dataBufMapPtr->dataBuf[i].logicalSliceAddr == 24131938) {
      xil_printf("DataBuf[%d] LPN 0x%X: dirty=%d, full=%d\r\n",
        i, dataBufMapPtr->dataBuf[i].logicalSliceAddr,
        dataBufMapPtr->dataBuf[i].dirty, dataBufMapPtr->dataBuf[i].full);

      unsigned int* bufAddr = dataBufEntry2DramAddr(i);
      HNSWOndemandEntry* entry = (HNSWOndemandEntry*)bufAddr;

      xil_printf("DataBuf[%d]: Node[0] key=%u, neighbors=%d\r\n",
        i, entry->hnswnodes[0].key, entry->hnswnodes[0].links[0].count);

      unsigned int* rawData = (unsigned int*)bufAddr;
      xil_printf("Raw data: 0x%X 0x%X 0x%X 0x%X\r\n",
        rawData[0], rawData[1], rawData[2], rawData[3]);
    }
  }
}

// void trackDataBufDuringBuild(int stepNumber, const char* stepName) {
//     xil_printf("=== Step %d: %s ===\r\n", stepNumber, stepName);

//     for(int i = 0; i < AVAILABLE_DATA_BUFFER_ENTRY_COUNT; i++) {
//         if(dataBufMapPtr->dataBuf[i].logicalSliceAddr == 24131938) {
//             unsigned int* bufAddr = dataBufEntry2DramAddr(i);
//             HNSWOndemandEntry* entry = (HNSWOndemandEntry*)bufAddr;

//             xil_printf("  DataBuf[%d]: key=%u, neighbors=%d\r\n",
//                 i, entry->hnswnodes[0].key, entry->hnswnodes[0].links[0].count);
//         }
//     }
// }


void printTopKResults(MaxHeap* result_set, int topK) {
  typedef struct {
    int idx;
    float dist;
  } SearchResult;

  SearchResult results[EF_CONSTRUCTION];

  for (int i = 0; i < result_set->size && i < topK; i++) {
    results[i].idx = result_set->indices[i];
    results[i].dist = result_set->distances[i];
  }

  xil_printf("-----Top-%d Nearest Results-----\r\n", topK);
  for (int i = 0; i < result_set->size && i < topK; i++) {
    xil_printf("Rank %d: ID = %d, Distance = ", i + 1, results[i].idx);
    printFloatArray(&results[i].dist, 1);
    xil_printf("\r\n");
  }
}

void manuallyFlushDirtyDataBuffer(void) {
  // xil_printf("[FUNC] manuallyFlushDirtyDataBuffer\r\n");

  unsigned int flushedCnt = 0;
  for (int i = 0; i < AVAILABLE_DATA_BUFFER_ENTRY_COUNT; i++) {
    unsigned int lpn = dataBufMapPtr->dataBuf[i].logicalSliceAddr;

    if (lpn != 0 && lpn != LOGICAL_SLICE_ADDR_NONE && dataBufMapPtr->dataBuf[i].dirty == DATA_BUF_DIRTY) {
      Xil_DCacheFlushRange(dataBufEntry2DramAddr(i), BYTES_PER_DATA_REGION_OF_SLICE);
      TriggerInternalPagesWrite(lpn, dataBufEntry2DramAddr(i), 1);
      SyncAllLowLevelReqDone();
      flushedCnt++;
      dataBufMapPtr->dataBuf[i].dirty = DATA_BUF_CLEAN;
    }
  }
  // if (flushedCnt > 0) {
  waitForAllRequests();
  // }
}
/**
 * @note(Dhmin): Flush NAND Flush
 */
void waitForAllRequests() {
  // xil_printf("[FUNC] waitForAllRequests\r\n");
  while (sliceReqQ.reqCnt > 0) {
    ReqTransSliceToLowLevel();
  }
  SchedulingNandReq();
  SyncAllLowLevelReqDone();
  // xil_printf("Final Status: slice=%u, dma=%u, nand=%u, blocked=%u\r\n", sliceReqQ.reqCnt, nvmeDmaReqQ.reqCnt,
  //            notCompletedNandReqCnt, blockedReqCnt);
  // xil_printf("Completed\r\n");
}

void clearAllDataBufferContents() {
  for (int i = 0; i < AVAILABLE_DATA_BUFFER_ENTRY_COUNT; i++) {
    unsigned int* dataAddr = (unsigned int*)dataBufEntry2DramAddr(i);
    memset(dataAddr, 0, BYTES_PER_DATA_REGION_OF_SLICE);
    Xil_DCacheInvalidateRange((INTPTR)dataAddr, BYTES_PER_DATA_REGION_OF_SLICE);
  }
}

int bruteForceSearch(SUPER_SEALED_SEGMENT_INFO* targetSsForSearching, const unsigned int topK, const int caching) {
  successFlag = 1;
  SEALED_SEGMENT_INDEX_NODE* ssIndexNode = (SEALED_SEGMENT_INDEX_NODE*)SEALED_SEGMENT_INDEX_BUFFER;
  SEALED_SEGMENT_DATA_NODE* ssDataNode = (SEALED_SEGMENT_DATA_NODE*)SEALED_SEGMENT_DATA_BUFFER;
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

  TriggerInternalPagesRead(targetSsForSearching->head_lpn, (unsigned int)ssIndexNode,
    targetSsForSearching->index_size);
  TriggerInternalPagesRead(targetSsForSearching->head_lpn + targetSsForSearching->index_size,
    (unsigned int)ssDataNode, targetSsForSearching->data_size);

  SyncAllLowLevelReqDone();
  Xil_DCacheInvalidateRange((INTPTR)ssIndexNode, SEALED_SEGMENT_INDEX_BUFFER_SIZE);
  Xil_DCacheInvalidateRange((INTPTR)ssDataNode, SEALED_SEGMENT_DATA_BUFFER_SIZE);


  unsigned int queryVectorAddr[(PREDEFINED_VECTOR_SIZE / BYTES_PER_DATA_REGION_OF_SLICE) + 2] = { 0, };
  queryVectorAddr[0] = QUERY_VECTOR_BUFFER_START_ADDR;
  for (int i = 1; i < (PREDEFINED_VECTOR_SIZE / BYTES_PER_DATA_REGION_OF_SLICE) + 2; i++) {
    if (PREDEFINED_VECTOR_SIZE > BYTES_PER_DATA_REGION_OF_SLICE * i) {
      queryVectorAddr[i] = QUERY_VECTOR_BUFFER_START_ADDR + (BYTES_PER_DATA_REGION_OF_SLICE * i);
    }
    else {
      queryVectorAddr[i] = 0;
    }
  }

  int need_sync = 1;
  unsigned int candCount = 0;
  unsigned int dramAddr1[(PREDEFINED_VECTOR_SIZE / BYTES_PER_DATA_REGION_OF_SLICE) + 2] = { 0, };
  unsigned int dramAddr2[(PREDEFINED_VECTOR_SIZE / BYTES_PER_DATA_REGION_OF_SLICE) + 2] = { 0, };
  unsigned int dramAddr3[(PREDEFINED_VECTOR_SIZE / BYTES_PER_DATA_REGION_OF_SLICE) + 2] = { 0, };
  unsigned int dramAddr4[(PREDEFINED_VECTOR_SIZE / BYTES_PER_DATA_REGION_OF_SLICE) + 2] = { 0, };

  MaxHeap result_set;
  max_heap_init(&result_set, topK + 1);


  double load_vector_time = 0;
  int load_cnt = 0;
  double comp_time = 0;
  int comp_cnt = 0;
  double temp_time = 0;
  double heap_organizing_time = 0;

#define BRUTEFORCE_SEARCH_BATCH_SIZE 4

  unsigned int candidateKeys[10] = { 0 };
  float candidateDistances[10];
  for (unsigned int i = 0; i < topK; i++) candidateDistances[i] = FLT_MAX;
  unsigned int candidateCount = 0;

  int medianPoint = (int)topK / 2;
  float curMaxDistance = (validGlobalCount > 0) ? searchResults->distance[validGlobalCount - 1] : FLT_MAX;

  unsigned int i = 0;
  int accChecked = 0;
  for (i = 0; i < (targetSsForSearching->total_entry) / BRUTEFORCE_SEARCH_BATCH_SIZE; i += BRUTEFORCE_SEARCH_BATCH_SIZE) {
    unsigned int startLbas[BRUTEFORCE_SEARCH_BATCH_SIZE] = { 0 };
    unsigned int nlbs[BRUTEFORCE_SEARCH_BATCH_SIZE] = { 0 };
    unsigned int endLbas[BRUTEFORCE_SEARCH_BATCH_SIZE] = { 0 };

    startLbas[0] = ssDataNode[i].lba;
    nlbs[0] = (ssDataNode[i].length / BYTES_PER_NVME_BLOCK);
    endLbas[0] = startLbas[0] + nlbs[0];

    startLbas[1] = ssDataNode[i + 1].lba;
    nlbs[1] = (ssDataNode[i + 1].length / BYTES_PER_NVME_BLOCK);
    endLbas[1] = startLbas[1] + nlbs[1];

    startLbas[2] = ssDataNode[i + 2].lba;
    nlbs[2] = (ssDataNode[i + 2].length / BYTES_PER_NVME_BLOCK);
    endLbas[2] = startLbas[2] + nlbs[2];

    startLbas[3] = ssDataNode[i + 3].lba;
    nlbs[3] = (ssDataNode[i + 3].length / BYTES_PER_NVME_BLOCK);
    endLbas[3] = startLbas[3] + nlbs[3];

    unsigned int maxSlicesSet[BRUTEFORCE_SEARCH_BATCH_SIZE] = { 0 };
    maxSlicesSet[0] = loadRawVectorData(dramAddr1, startLbas[0], endLbas[0], &need_sync);
    maxSlicesSet[1] = loadRawVectorData(dramAddr2, startLbas[1], endLbas[1], &need_sync);
    maxSlicesSet[2] = loadRawVectorData(dramAddr3, startLbas[2], endLbas[2], &need_sync);
    maxSlicesSet[3] = loadRawVectorData(dramAddr4, startLbas[3], endLbas[3], &need_sync);

    if (successFlag == 0) return -1;

    SyncAllLowLevelReqDone();
    for (int j = 0; j < maxSlicesSet[0]; j++)
      Xil_DCacheInvalidateRange((INTPTR)dramAddr1[j], BYTES_PER_DATA_REGION_OF_SLICE);
    for (int j = 0; j < maxSlicesSet[1]; j++)
      Xil_DCacheInvalidateRange((INTPTR)dramAddr2[j], BYTES_PER_DATA_REGION_OF_SLICE);
    for (int j = 0; j < maxSlicesSet[2]; j++)
      Xil_DCacheInvalidateRange((INTPTR)dramAddr3[j], BYTES_PER_DATA_REGION_OF_SLICE);
    for (int j = 0; j < maxSlicesSet[3]; j++)
      Xil_DCacheInvalidateRange((INTPTR)dramAddr4[j], BYTES_PER_DATA_REGION_OF_SLICE);

    load_cnt += 4;

    float curDist[4];
    if (caching) {
      curDist[0] = calcVectorDistanceWithCachedNorm(queryVectorAddr, dramAddr1);
      curDist[1] = calcVectorDistanceWithCachedNorm(queryVectorAddr, dramAddr2);
      curDist[2] = calcVectorDistanceWithCachedNorm(queryVectorAddr, dramAddr3);
      curDist[3] = calcVectorDistanceWithCachedNorm(queryVectorAddr, dramAddr4);
    }
    else {
      curDist[0] = calcVectorDistance(queryVectorAddr, dramAddr1);
      curDist[1] = calcVectorDistanceWithCachedNorm(queryVectorAddr, dramAddr2);
      curDist[3] = calcVectorDistanceWithCachedNorm(queryVectorAddr, dramAddr3);
      curDist[2] = calcVectorDistanceWithCachedNorm(queryVectorAddr, dramAddr4);
    }


    comp_cnt += 4;
    for (int b = 0; b < BRUTEFORCE_SEARCH_BATCH_SIZE; b++) {
      if (candidateCount == topK && curDist[b] >= candidateDistances[topK - 1]) continue;

      unsigned int pos = candidateCount;
      for (unsigned int j = 0; j < candidateCount; j++) {
        if (curDist[b] < candidateDistances[j]) {
          pos = j;
          break;
        }
      }

      if (pos < topK) {
        if (candidateCount < topK) candidateCount++;
        for (unsigned int j = candidateCount - 1; j > pos; j--) {
          candidateDistances[j] = candidateDistances[j - 1];
          candidateKeys[j] = candidateKeys[j - 1];
        }
        candidateDistances[pos] = curDist[b];
        candidateKeys[pos] = ssIndexNode[i + b].key;
      }
    }

    if (candidateCount == topK && candidateDistances[medianPoint] < curMaxDistance) {
      break;
    }
  }

  return mergeSearchResults(result_set.indices, result_set.distances, result_set.size, topK, 0);
}

int mergeSearchResults(int* candidateIdx, float* candidateDistances, unsigned int candCount, const unsigned int topK, const unsigned int earyExit) {
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

  for (unsigned int i = 0; i < candCount; i++) {
    if (validGlobalCount == topK && candidateDistances[i] >= searchResults->distance[topK - 1]) {
      continue;
    }

    // Find insertion position
    unsigned int insertPos = validGlobalCount;
    for (unsigned int j = 0; j < validGlobalCount; j++) {
      if (candidateDistances[i] < searchResults->distance[j]) {  // insert
        insertPos = j;
        break;
      }
    }

    // Insert
    if (insertPos < topK) {
      for (unsigned int j = (validGlobalCount < topK ? validGlobalCount : topK - 1); j > insertPos; j--) {
        searchResults->distance[j] = searchResults->distance[j - 1];
        searchResults->key[j] = searchResults->key[j - 1];
      }
      searchResults->distance[insertPos] = candidateDistances[i];
      searchResults->key[insertPos] = candidateIdx[i];
    }

    if (validGlobalCount < topK) {
      validGlobalCount++;
    }
  }

  // xil_printf("-----Top-%d Nearest Results-----\r\n", validGlobalCount);
  // for (int i = 0; i < validGlobalCount; i++) {
  //   xil_printf("Rank %d: VID=%d, Dist=", i + 1, searchResults->key[i]);
  //   printFloatArray(&searchResults->distance[i], 1);
  //   xil_printf(", ");
  // }
  // xil_printf("\r\n");

  int maxPosition = (validGlobalCount < topK) ? validGlobalCount - 1 : topK - 1;
  if (searchResults->distance[maxPosition] <= EPSILON)
    return 1;

  return 0;
}

void reclaimAllDataBufWithStore() {

  unsigned int savedCount = 0;
  unsigned int skippedCount = 0;

  for (int i = 0; i < AVAILABLE_DATA_BUFFER_ENTRY_COUNT; i++) {
    unsigned int lpn = dataBufMapPtr->dataBuf[i].logicalSliceAddr;

    if (lpn != 0 && lpn != LSA_NONE && dataBufMapPtr->dataBuf[i].dirty) {

      unsigned int virtualSliceAddr = AddrTransWrite(lpn);
      if (virtualSliceAddr == 0xffffffff) {
        skippedCount++;
      }
      else {
        Xil_DCacheFlushRange(dataBufEntry2DramAddr(i), BYTES_PER_DATA_REGION_OF_SLICE);

        unsigned int reqSlotTag = GetFromFreeReqQ();
        if (reqSlotTag != REQ_SLOT_TAG_NONE) {
          reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NAND;
          reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_WRITE;
          reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr = lpn;
          reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat = REQ_OPT_DATA_BUF_ENTRY;
          reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr = REQ_OPT_NAND_ADDR_VSA;
          reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc = REQ_OPT_NAND_ECC_ON;
          reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning = REQ_OPT_NAND_ECC_WARNING_ON;
          reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK;
          reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace = REQ_OPT_BLOCK_SPACE_MAIN;
          reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = i;
          reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr = virtualSliceAddr;

          UpdateDataBufEntryInfoBlockingReq(i, reqSlotTag);
          SelectLowLevelReqQ(reqSlotTag);

          savedCount++;
        }
        else {
          skippedCount++;
        }
      }
    }

    SelectiveGetFromDataBufHashList(i);

    dataBufMapPtr->dataBuf[i].logicalSliceAddr = 0;
    dataBufMapPtr->dataBuf[i].dirty = DATA_BUF_CLEAN;
    dataBufMapPtr->dataBuf[i].full = 0;
    dataBufMapPtr->dataBuf[i].pinned = 0;
    dataBufMapPtr->dataBuf[i].prevEntry = DATA_BUF_NONE;
    dataBufMapPtr->dataBuf[i].nextEntry = DATA_BUF_NONE;
    dataBufMapPtr->dataBuf[i].hashPrevEntry = DATA_BUF_NONE;
    dataBufMapPtr->dataBuf[i].hashNextEntry = DATA_BUF_NONE;
    dataBufMapPtr->dataBuf[i].blockingReqTail = DATA_BUF_NONE;
  }
  SyncAllLowLevelReqDone();
}

void reclaimAllDataBuf() {
  for (int i = 0; i < AVAILABLE_DATA_BUFFER_ENTRY_COUNT; i++) {
    unsigned int lpn = dataBufMapPtr->dataBuf[i].logicalSliceAddr;
    SelectiveGetFromDataBufHashList(i);
    dataBufMapPtr->dataBuf[i].logicalSliceAddr = 0;
    dataBufMapPtr->dataBuf[i].dirty = DATA_BUF_CLEAN;
    dataBufMapPtr->dataBuf[i].pinned = 0;
    // PutToDataBufHashList(i);
  }
}


int searchOndemandHnsw(SUPER_SEALED_SEGMENT_INFO* targetSsForSearching, const unsigned int topK) {
  successFlag = 1;
  VectorIndex* vectorIndex = (VectorIndex*)VECTOR_INDEX_START_ADDR;
  HNSWIndex* hnswIndex = (HNSWIndex*)(&(vectorIndex->hnsw));
  HNSWOndemandEntry* memoryEntry = (HNSWOndemandEntry*)HNSW_ONDEMAND_ENTRY_START_ADDR;
  SEALED_SEGMENT_INDEX_NODE* ssIndexNode = (SEALED_SEGMENT_INDEX_NODE*)SEALED_SEGMENT_INDEX_BUFFER;
  SEALED_SEGMENT_DATA_NODE* ssDataNode = (SEALED_SEGMENT_DATA_NODE*)SEALED_SEGMENT_DATA_BUFFER;

  unsigned int vectorIndexLpn = targetSsForSearching->vector_index_start_lpn;

  TriggerInternalPagesRead(targetSsForSearching->head_lpn, (unsigned int)ssIndexNode,
    targetSsForSearching->index_size);

  TriggerInternalPagesRead(targetSsForSearching->head_lpn + targetSsForSearching->index_size,
    (unsigned int)ssDataNode, targetSsForSearching->data_size);

  TriggerInternalPagesRead(targetSsForSearching->vector_index_start_lpn, VECTOR_INDEX_START_ADDR,
    targetSsForSearching->vector_index_size);
  SyncAllLowLevelReqDone();
  Xil_DCacheInvalidateRange((INTPTR)hnswIndex,
    targetSsForSearching->vector_index_size * BYTES_PER_DATA_REGION_OF_SLICE);
  Xil_DCacheInvalidateRange((INTPTR)vectorIndex,
    targetSsForSearching->vector_index_size * BYTES_PER_DATA_REGION_OF_SLICE);
  Xil_DCacheInvalidateRange((INTPTR)ssIndexNode, SEALED_SEGMENT_INDEX_BUFFER_SIZE);
  Xil_DCacheInvalidateRange((INTPTR)ssDataNode, SEALED_SEGMENT_DATA_BUFFER_SIZE);


  vectorIndex = (VectorIndex*)VECTOR_INDEX_START_ADDR;
  hnswIndex = (HNSWIndex*)(&(vectorIndex->hnsw));

  unsigned int ep = hnswIndex->entryPointKeyIndex;

  unsigned int queryVectorAddr[(PREDEFINED_VECTOR_SIZE / BYTES_PER_DATA_REGION_OF_SLICE) + 2] = { 0 };
  queryVectorAddr[0] = QUERY_VECTOR_BUFFER_START_ADDR;
  for (int i = 1; i < (PREDEFINED_VECTOR_SIZE / BYTES_PER_DATA_REGION_OF_SLICE) + 2; i++) {
    if (PREDEFINED_VECTOR_SIZE > BYTES_PER_DATA_REGION_OF_SLICE * i) {
      queryVectorAddr[i] = QUERY_VECTOR_BUFFER_START_ADDR + (BYTES_PER_DATA_REGION_OF_SLICE * i);
    }
    else {
      queryVectorAddr[i] = 0;
    }
  }

  ep = greedySearchLevel(queryVectorAddr, ep, hnswIndex->maxLevel, -1);
  if (successFlag == 0) return -1;

  int candidateIdxs[EF_CONSTRUCTION] = { 0 };
  float candidateDistances[EF_CONSTRUCTION] = { FLT_MAX };
  unsigned int candCount = 0;

  searchNeighborsEf(queryVectorAddr, ep, 0, topK * SEARCH_SPACE_DEGREE, candidateIdxs,
    candidateDistances, &candCount, -1);
  if (successFlag == 0) return -1;
  unsigned int resultCount = candCount < topK ? candCount : topK;
  return mergeSearchResults(candidateIdxs, candidateDistances, resultCount, topK, 1);
}

void printNodeNeighbors(unsigned int nodeIdx, SUPER_SEALED_SEGMENT_INFO* targetSs) {
  VectorIndex* vectorIndex = (VectorIndex*)VECTOR_INDEX_START_ADDR;
  HNSWIndex* hnswIndex = (HNSWIndex*)(&(vectorIndex->hnsw));

  unsigned int entryIndex = nodeIdx / NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK;
  unsigned int nodeIndex = nodeIdx % NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK;
  unsigned int need_sync = 0;

  HNSWOndemandEntry* nodePtr = NULL;

  if (!IS_ONDEMAND_FIELD((int)entryIndex)) {
    nodePtr = loadHnswNode(nodeIdx, &need_sync);
    if (need_sync) {
      SyncAllLowLevelReqDone();
      Xil_DCacheInvalidateRange((INTPTR)nodePtr, sizeof(HNSWOndemandEntry));
    }
    if (!nodePtr) {
      xil_printf("ERROR: Failed to load HNSW node %u\r\n", nodeIdx);
      return;
    }
  }
  else {
    nodePtr = (HNSWOndemandEntry*)HNSW_ONDEMAND_ENTRY_START_ADDR;
  }

  int level0_count = nodePtr->hnswnodes[nodeIndex].links[0].count;

  xil_printf("Node[%u]: Level 0 has %d neighbors -> ", nodeIdx, level0_count);
  if (level0_count == 0) {
    xil_printf("(no neighbors)\r\n");
  }
  else {
    xil_printf("{");
    for (int i = 0; i < level0_count; i++) {
      unsigned int neighborIdx = nodePtr->hnswnodes[nodeIndex].links[0].neighbors[i];
      xil_printf("%u", neighborIdx);
      if (i < level0_count - 1) {
        xil_printf(", ");
      }
    }
    xil_printf("}\r\n");
  }
}

void printAllLevel0Neighbors(SUPER_SEALED_SEGMENT_INFO* targetSs) {
  VectorIndex* vectorIndex = (VectorIndex*)VECTOR_INDEX_START_ADDR;
  HNSWIndex* hnswIndex = (HNSWIndex*)(&(vectorIndex->hnsw));

  xil_printf("=== HNSW Level 0 Neighbor Connections ===\r\n");
  xil_printf("Total nodes: %u\r\n", targetSs->total_entry);
  xil_printf("Entry point: %u\r\n", hnswIndex->entryPointKeyIndex);
  xil_printf("Max level: %u\r\n", hnswIndex->maxLevel);
  xil_printf("==========================================\r\n");

  unsigned int current_entry_index = 0xFFFFFFFF;
  HNSWOndemandEntry* currentNodePtr = NULL;
  unsigned int need_sync = 0;

  for (unsigned int nodeIdx = 0; nodeIdx < targetSs->total_entry; nodeIdx++) {
    unsigned int entryIndex = nodeIdx / NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK;
    unsigned int nodeIndex = nodeIdx % NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK;

    if (entryIndex != current_entry_index) {
      current_entry_index = entryIndex;

      if (!IS_ONDEMAND_FIELD((int)entryIndex)) {
        Xil_DCacheInvalidateRange((INTPTR)HNSW_ONDEMAND_ENTRY_START_ADDR,
          BYTES_PER_DATA_REGION_OF_SLICE);
        TriggerInternalPagesRead(hnswIndex->ondemandEntryLpn[entryIndex],
          HNSW_ONDEMAND_ENTRY_START_ADDR, 1);
        SyncAllLowLevelReqDone();
        Xil_DCacheInvalidateRange((INTPTR)HNSW_ONDEMAND_ENTRY_START_ADDR,
          BYTES_PER_DATA_REGION_OF_SLICE);
      }
      currentNodePtr = (HNSWOndemandEntry*)HNSW_ONDEMAND_ENTRY_START_ADDR;
    }

    int level0_count = currentNodePtr->hnswnodes[nodeIndex].links[0].count;

    xil_printf("Node[%u]: %d neighbors -> ", nodeIdx, level0_count);
    if (level0_count == 0) {
      xil_printf("(isolated)\r\n");
    }
    else {
      xil_printf("{");
      for (int i = 0; i < level0_count; i++) {
        unsigned int neighborIdx = currentNodePtr->hnswnodes[nodeIndex].links[0].neighbors[i];
        xil_printf("%u", neighborIdx);
        if (i < level0_count - 1) {
          xil_printf(", ");
        }
      }
      xil_printf("}\r\n");
    }
  }

  xil_printf("==========================================\r\n");
}

void printLevel0ConnectionStats(SUPER_SEALED_SEGMENT_INFO* targetSs) {
  VectorIndex* vectorIndex = (VectorIndex*)VECTOR_INDEX_START_ADDR;
  HNSWIndex* hnswIndex = (HNSWIndex*)(&(vectorIndex->hnsw));

  unsigned int totalConnections = 0;
  unsigned int isolatedNodes = 0;
  unsigned int maxConnections = 0;
  unsigned int minConnections = M_PARAM;
  unsigned int connectionHistogram[M_PARAM + 1] = { 0 };

  unsigned int current_entry_index = 0xFFFFFFFF;
  HNSWOndemandEntry* currentNodePtr = NULL;

  xil_printf("=== Analyzing Level 0 Connection Statistics ===\r\n");
  for (unsigned int nodeIdx = 0; nodeIdx < targetSs->total_entry; nodeIdx++) {
    unsigned int entryIndex = nodeIdx / NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK;
    unsigned int nodeIndex = nodeIdx % NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK;

    if (entryIndex != current_entry_index) {
      current_entry_index = entryIndex;

      if (!IS_ONDEMAND_FIELD((int)entryIndex)) {
        Xil_DCacheInvalidateRange((INTPTR)HNSW_ONDEMAND_ENTRY_START_ADDR,
          BYTES_PER_DATA_REGION_OF_SLICE);
        TriggerInternalPagesRead(hnswIndex->ondemandEntryLpn[entryIndex],
          HNSW_ONDEMAND_ENTRY_START_ADDR, 1);
        SyncAllLowLevelReqDone();
        Xil_DCacheInvalidateRange((INTPTR)HNSW_ONDEMAND_ENTRY_START_ADDR,
          BYTES_PER_DATA_REGION_OF_SLICE);
      }
      currentNodePtr = (HNSWOndemandEntry*)HNSW_ONDEMAND_ENTRY_START_ADDR;
    }

    unsigned int neighborCount = currentNodePtr->hnswnodes[nodeIndex].links[0].count;

    totalConnections += neighborCount;
    if (neighborCount == 0) {
      isolatedNodes++;
    }
    if (neighborCount > maxConnections) {
      maxConnections = neighborCount;
    }
    if (neighborCount < minConnections) {
      minConnections = neighborCount;
    }

    if (neighborCount <= M_PARAM) {
      connectionHistogram[neighborCount]++;
    }
  }

  xil_printf("Total nodes: %u\r\n", targetSs->total_entry);
  xil_printf("Total connections: %u\r\n", totalConnections);
  xil_printf("Average connections per node: %u.%02u\r\n",
    totalConnections / targetSs->total_entry,
    (totalConnections * 100 / targetSs->total_entry) % 100);
  xil_printf("Isolated nodes (0 connections): %u\r\n", isolatedNodes);
  xil_printf("Min connections: %u\r\n", minConnections);
  xil_printf("Max connections: %u\r\n", maxConnections);

  xil_printf("\nConnection histogram:\r\n");
  for (unsigned int i = 0; i <= M_PARAM; i++) {
    if (connectionHistogram[i] > 0) {
      xil_printf("  %u connections: %u nodes\r\n", i, connectionHistogram[i]);
    }
  }
  xil_printf("================================================\r\n");
}

void printLevel0NeighborsRange(SUPER_SEALED_SEGMENT_INFO* targetSs,
  unsigned int startNode, unsigned int endNode) {
  VectorIndex* vectorIndex = (VectorIndex*)VECTOR_INDEX_START_ADDR;
  HNSWIndex* hnswIndex = (HNSWIndex*)(&(vectorIndex->hnsw));

  if (endNode > targetSs->total_entry) {
    endNode = targetSs->total_entry;
  }

  xil_printf("=== Level 0 Neighbors (Nodes %u to %u) ===\r\n", startNode, endNode - 1);
  unsigned int current_entry_index = 0xFFFFFFFF;
  HNSWOndemandEntry* currentNodePtr = NULL;

  for (unsigned int nodeIdx = startNode; nodeIdx < endNode; nodeIdx++) {
    unsigned int entryIndex = nodeIdx / NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK;
    unsigned int nodeIndex = nodeIdx % NUM_OF_HNSWNODE_PER_DATA_BUFFER_BLOCK;

    if (entryIndex != current_entry_index) {
      current_entry_index = entryIndex;

      if (!IS_ONDEMAND_FIELD((int)entryIndex)) {
        Xil_DCacheInvalidateRange((INTPTR)HNSW_ONDEMAND_ENTRY_START_ADDR,
          BYTES_PER_DATA_REGION_OF_SLICE);
        TriggerInternalPagesRead(hnswIndex->ondemandEntryLpn[entryIndex],
          HNSW_ONDEMAND_ENTRY_START_ADDR, 1);
        SyncAllLowLevelReqDone();
        Xil_DCacheInvalidateRange((INTPTR)HNSW_ONDEMAND_ENTRY_START_ADDR,
          BYTES_PER_DATA_REGION_OF_SLICE);
      }
      currentNodePtr = (HNSWOndemandEntry*)HNSW_ONDEMAND_ENTRY_START_ADDR;
    }

    int level0_count = currentNodePtr->hnswnodes[nodeIndex].links[0].count;

    xil_printf("Node[%u]: %d neighbors -> ", nodeIdx, level0_count);
    if (level0_count == 0) {
      xil_printf("(none)\r\n");
    }
    else {
      xil_printf("{");
      for (int i = 0; i < level0_count; i++) {
        unsigned int neighborIdx = currentNodePtr->hnswnodes[nodeIndex].links[0].neighbors[i];
        xil_printf("%u", neighborIdx);
        if (i < level0_count - 1) {
          xil_printf(", ");
        }
      }
      xil_printf("}\r\n");
    }
  }
  xil_printf("==========================================\r\n");
}