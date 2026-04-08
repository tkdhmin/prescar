#include "index.h"
#include "../memory_map.h"
#include "binary_heap.h"
#include "xil_printf.h"

float calcCosineSimilarityUnrollWithCachedNorm(const unsigned int vecDramAddr1[], const unsigned int vecDramAddr2[]) {
  float dot = 0.0f, norm2 = 0.0f;

  unsigned int accSize = 0;
  unsigned int vecDramIndex1 = 0;
  unsigned int vecDramIndex2 = 0;

  if (cachedNorm == 0.0f) return 0.0f;

  float* vec1 = (float*)vecDramAddr1[0];
  float* vec2 = (float*)vecDramAddr2[0];

  for (unsigned int i = 0; i <= PREDEFINED_DIMENSION - PREDEFINED_UNROLLING_DEGREE; i += PREDEFINED_UNROLLING_DEGREE) {
    dot += vec1[0] * vec2[0] + vec1[1] * vec2[1] + vec1[2] * vec2[2] + vec1[3] * vec2[3] +
      vec1[4] * vec2[4] + vec1[5] * vec2[5] + vec1[6] * vec2[6] + vec1[7] * vec2[7] +
      vec1[8] * vec2[8] + vec1[9] * vec2[9] + vec1[10] * vec2[10] + vec1[11] * vec2[11] +
      vec1[12] * vec2[12] + vec1[13] * vec2[13] + vec1[14] * vec2[14] + vec1[15] * vec2[15];

    norm2 += vec2[0] * vec2[0] + vec2[1] * vec2[1] + vec2[2] * vec2[2] + vec2[3] * vec2[3] +
      vec2[4] * vec2[4] + vec2[5] * vec2[5] + vec2[6] * vec2[6] + vec2[7] * vec2[7] +
      vec2[8] * vec2[8] + vec2[9] * vec2[9] + vec2[10] * vec2[10] + vec2[11] * vec2[11] +
      vec2[12] * vec2[12] + vec2[13] * vec2[13] + vec2[14] * vec2[14] + vec2[15] * vec2[15];

    vec1 += PREDEFINED_UNROLLING_DEGREE;
    vec2 += PREDEFINED_UNROLLING_DEGREE;
    accSize += PREDEFINED_UNROLLING_DEGREE * sizeof(float);

    if (accSize >= BYTES_PER_DATA_REGION_OF_SLICE) {
      vec1 = (float*)vecDramAddr1[++vecDramIndex1];
      vec2 = (float*)vecDramAddr2[++vecDramIndex2];
    }
  }
  if (norm2 == 0.0f) return 0.0f;  // avoid division by zero

  return dot * (cachedInverseSqrtNorm * fastInverseSqrt(norm2));
}
/**
 * General methods used in vector index
 *
 * @note These functions assume that each vector data (a set of float32-typed * digits) has been stored to a single
 * page. It is not allowed that the size of the vector exceeds the size of the NAND page (16KB).
 */
float calcCosineSimilarityUnroll(const unsigned int vecDramAddr1[], const unsigned int vecDramAddr2[]) {
  float dot = 0.0f, norm1 = 0.0f, norm2 = 0.0f;


  unsigned int accSize = 0;
  unsigned int vecDramIndex1 = 0;
  unsigned int vecDramIndex2 = 0;

  float* vec1 = (float*)vecDramAddr1[0];
  float* vec2 = (float*)vecDramAddr2[0];

  for (unsigned int i = 0; i <= PREDEFINED_DIMENSION - PREDEFINED_UNROLLING_DEGREE; i += PREDEFINED_UNROLLING_DEGREE) {
    // dot += vec1[0] * vec2[0] + vec1[1] * vec2[1] + vec1[2] * vec2[2] + vec1[3] * vec2[3] + vec1[4] * vec2[4] +
    //        vec1[5] * vec2[5] + vec1[6] * vec2[6] + vec1[7] * vec2[7];

    // norm1 += vec1[0] * vec1[0] + vec1[1] * vec1[1] + vec1[2] * vec1[2] + vec1[3] * vec1[3] + vec1[4] * vec1[4] +
    //          vec1[5] * vec1[5] + vec1[6] * vec1[6] + vec1[7] * vec1[7];

    // norm2 += vec2[0] * vec2[0] + vec2[1] * vec2[1] + vec2[2] * vec2[2] + vec2[3] * vec2[3] + vec2[4] * vec2[4] +
    //          vec2[5] * vec2[5] + vec2[6] * vec2[6] + vec2[7] * vec2[7];

    // dot product
    dot += vec1[0] * vec2[0] + vec1[1] * vec2[1] + vec1[2] * vec2[2] + vec1[3] * vec2[3] +
      vec1[4] * vec2[4] + vec1[5] * vec2[5] + vec1[6] * vec2[6] + vec1[7] * vec2[7] +
      vec1[8] * vec2[8] + vec1[9] * vec2[9] + vec1[10] * vec2[10] + vec1[11] * vec2[11] +
      vec1[12] * vec2[12] + vec1[13] * vec2[13] + vec1[14] * vec2[14] + vec1[15] * vec2[15];

    // norm1
    norm1 += vec1[0] * vec1[0] + vec1[1] * vec1[1] + vec1[2] * vec1[2] + vec1[3] * vec1[3] +
      vec1[4] * vec1[4] + vec1[5] * vec1[5] + vec1[6] * vec1[6] + vec1[7] * vec1[7] +
      vec1[8] * vec1[8] + vec1[9] * vec1[9] + vec1[10] * vec1[10] + vec1[11] * vec1[11] +
      vec1[12] * vec1[12] + vec1[13] * vec1[13] + vec1[14] * vec1[14] + vec1[15] * vec1[15];

    // norm2
    norm2 += vec2[0] * vec2[0] + vec2[1] * vec2[1] + vec2[2] * vec2[2] + vec2[3] * vec2[3] +
      vec2[4] * vec2[4] + vec2[5] * vec2[5] + vec2[6] * vec2[6] + vec2[7] * vec2[7] +
      vec2[8] * vec2[8] + vec2[9] * vec2[9] + vec2[10] * vec2[10] + vec2[11] * vec2[11] +
      vec2[12] * vec2[12] + vec2[13] * vec2[13] + vec2[14] * vec2[14] + vec2[15] * vec2[15];


    // // dot product
    // dot += vec1[0] * vec2[0] + vec1[1] * vec2[1] + vec1[2] * vec2[i + 2] + vec1[i + 3] * vec2[i + 3] +
    //        vec1[4] * vec2[4] + vec1[5] * vec2[5] + vec1[6] * vec2[i + 6] + vec1[i + 7] * vec2[i + 7] +
    //        vec1[8] * vec2[8] + vec1[9] * vec2[9] + vec1[10] * vec2[i + 10] + vec1[i + 11] * vec2[i + 11] +
    //        vec1[12] * vec2[12] + vec1[i + 13] * vec2[i + 13] + vec1[i + 14] * vec2[i + 14] + vec1[i + 15] * vec2[i + 15];

    // // norm1
    // norm1 += vec1[i + 0] * vec1[i + 0] + vec1[i + 1] * vec1[i + 1] + vec1[i + 2] * vec1[i + 2] + vec1[i + 3] * vec1[i + 3] +
    //          vec1[i + 4] * vec1[i + 4] + vec1[i + 5] * vec1[i + 5] + vec1[i + 6] * vec1[i + 6] + vec1[i + 7] * vec1[i + 7] +
    //          vec1[i + 8] * vec1[i + 8] + vec1[i + 9] * vec1[i + 9] + vec1[i + 10] * vec1[i + 10] + vec1[i + 11] * vec1[i + 11] +
    //          vec1[i + 12] * vec1[i + 12] + vec1[i + 13] * vec1[i + 13] + vec1[i + 14] * vec1[i + 14] + vec1[i + 15] * vec1[i + 15];

    // // norm2
    // norm2 += vec2[i + 0] * vec2[i + 0] + vec2[i + 1] * vec2[i + 1] + vec2[i + 2] * vec2[i + 2] + vec2[i + 3] * vec2[i + 3] +
    //          vec2[i + 4] * vec2[i + 4] + vec2[i + 5] * vec2[i + 5] + vec2[i + 6] * vec2[i + 6] + vec2[i + 7] * vec2[i + 7] +
    //          vec2[i + 8] * vec2[i + 8] + vec2[i + 9] * vec2[i + 9] + vec2[i + 10] * vec2[i + 10] + vec2[i + 11] * vec2[i + 11] +
    //          vec2[i + 12] * vec2[i + 12] + vec2[i + 13] * vec2[i + 13] + vec2[i + 14] * vec2[i + 14] + vec2[i + 15] * vec2[i + 15];


    vec1 += PREDEFINED_UNROLLING_DEGREE;
    vec2 += PREDEFINED_UNROLLING_DEGREE;
    accSize += PREDEFINED_UNROLLING_DEGREE * sizeof(float);

    if (accSize >= BYTES_PER_DATA_REGION_OF_SLICE) {
      vec1 = (float*)vecDramAddr1[++vecDramIndex1];
      vec2 = (float*)vecDramAddr2[++vecDramIndex2];
    }
  }

  // NOTE: Loop unrolling technique requires the computation for remaining un-aligned part. We assume that the
  // unrolling degree is divisor of the total dimension size.

  cachedNorm = norm1;
  if (norm1 == 0.0f || norm2 == 0.0f) {
    return 0.0f;  // avoid division by zero
  }

  cachedInverseSqrtNorm = fastInverseSqrt(norm1);
  return dot * (cachedInverseSqrtNorm * fastInverseSqrt(norm2));
  // return dot / (my_sqrt_multiply(norm1, norm2));
}

float calcVectorDistance(const unsigned int vecDramAddr1[], const unsigned int vecDramAddr2[]) {
  double result = 1.0f - calcCosineSimilarityUnroll(vecDramAddr1, vecDramAddr2);
  return result;
}

float calcVectorDistanceWithCachedNorm(const unsigned int vecDramAddr1[], const unsigned int vecDramAddr2[]) {
  double result = 1.0f - calcCosineSimilarityUnrollWithCachedNorm(vecDramAddr1, vecDramAddr2);
  return result;
}

void calcVectorSum(float* result, const unsigned int vecDramAddr[]) {
  unsigned int accSize = 0;
  unsigned int vecDramIndex = 0, resultDramIndex = 0;

  float* vec = (float*)vecDramAddr[0];

  for (unsigned int i = 0; i < PREDEFINED_DIMENSION; i += PREDEFINED_UNROLLING_DEGREE) {
    result[i + 0] = result[i + 0] + vec[i + 0];
    result[i + 1] = result[i + 1] + vec[i + 1];
    result[i + 2] = result[i + 2] + vec[i + 2];
    result[i + 3] = result[i + 3] + vec[i + 3];
    result[i + 4] = result[i + 4] + vec[i + 4];
    result[i + 5] = result[i + 5] + vec[i + 5];
    result[i + 6] = result[i + 6] + vec[i + 6];
    result[i + 7] = result[i + 7] + vec[i + 7];
    result[i + 8] = result[i + 8] + vec[i + 8];
    result[i + 9] = result[i + 9] + vec[i + 9];
    result[i + 10] = result[i + 10] + vec[i + 10];
    result[i + 11] = result[i + 11] + vec[i + 11];
    result[i + 12] = result[i + 12] + vec[i + 12];
    result[i + 13] = result[i + 13] + vec[i + 13];
    result[i + 14] = result[i + 14] + vec[i + 14];
    result[i + 15] = result[i + 15] + vec[i + 15];


    vec += PREDEFINED_UNROLLING_DEGREE;
    accSize += PREDEFINED_UNROLLING_DEGREE * sizeof(float);

    if (accSize >= BYTES_PER_DATA_REGION_OF_SLICE) {
      vec = (float*)vecDramAddr[++vecDramIndex];
      accSize = 0;
    }
  }
}

unsigned int vectorssd_rand() {
  static unsigned int seed = 41;
  // LCG parameters: same as POSIX [glibc]
  seed = (1103515245 * seed + 12345) & 0x7fffffff;
  return seed;
}

float fastInverseSqrt(float number) {
  float x2 = number * 0.5F;
  float y = number;
  uint32_t i;

  memcpy(&i, &y, sizeof(i));
  i = 0x5f3759df - (i >> 1);
  memcpy(&y, &i, sizeof(y));

  // 2x Newton-Raphson
  y = y * (1.5F - (x2 * y * y));
  y = y * (1.5F - (x2 * y * y));

  return y;
}



float my_sqrt_multiply(float number1, float number2) {
  float x1 = number1;
  float x2 = number2;
  for (int i = 0; i < SQRT_ITERATION; i++) {
    x1 = 0.5f * (x1 + number1 / x1);
    x2 = 0.5f * (x2 + number2 / x2);
  }
  return x1 * x2;
}


// it is not sync operation. to sync, SyncAllLowLevelReqDone()
unsigned int getSliceToBuf(unsigned int target_lsa) {
  unsigned int dataBufEntry;
  dataBufEntry = AllocateDataBuf(0);
  if (dataBufEntry == DATA_BUF_NONE) {
    assert(FALSE);
    return DATA_BUF_NONE;
  }

  dataBufMapPtr->dataBuf[dataBufEntry].pinned = 0;
  unsigned int reqSlotTag = GetFromFreeReqQ();
  assert(reqSlotTag != REQ_SLOT_TAG_NONE);
  reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = dataBufEntry;

  if (dataBufMapPtr->dataBuf[reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry].dirty) {
    Xil_DCacheFlushRange(dataBufEntry2DramAddr(dataBufEntry), BYTES_PER_DATA_REGION_OF_SLICE);
  }
  EvictDataBufEntry(reqSlotTag);
  dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr = target_lsa;
  PutToDataBufHashList(dataBufEntry);

  reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_SLICE;
  reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_READ;
  reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr = target_lsa;

  DataReadFromNand(reqSlotTag);
  PutToFreeReqQ(reqSlotTag);
  return dataBufEntry;
}

void PinorUnPin1PageBuf(unsigned int dramAddr, unsigned int pin) {
  unsigned int addrArray[(PREDEFINED_VECTOR_SIZE / BYTES_PER_DATA_REGION_OF_SLICE) + 2] = { dramAddr, 0 };
  PinOrUnPinBuf(addrArray, 1, pin);
}

void PinOrUnPinBuf(unsigned int bufDramAddr[], unsigned int size, unsigned int pin) {
  unsigned int i = 0;
  for (i = 0;i < size;i++) {
    dataBufMapPtr->dataBuf[dramAddr2DataBufEntry(bufDramAddr[i])].pinned = pin;
  }
}


void WaitPrefetchedNANDOperation(unsigned int dramAddr[], unsigned int dramAddrN) {
  unsigned int i = 0;
  while (1) {
    unsigned int done = 1;

    ProcessNANDOperation();
    for (i = 0; i < dramAddrN; i++) {
      unsigned int blockingReqTail = dataBufMapPtr->dataBuf[dramAddr2DataBufEntry(dramAddr[i])].blockingReqTail;
      unsigned int logicalSliceAddr = dataBufMapPtr->dataBuf[dramAddr2DataBufEntry(dramAddr[i])].logicalSliceAddr;
      for (blockingReqTail; blockingReqTail != REQ_SLOT_TAG_NONE;
        blockingReqTail = reqPoolPtr->reqPool[blockingReqTail].prevBlockingReq) {
        if (reqPoolPtr->reqPool[blockingReqTail].logicalSliceAddr == logicalSliceAddr) {
          done = 0;
        }
      }

    }

    if (done) {
      break;
    }
  }
  return;
}