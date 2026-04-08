#ifndef VECTOR1_COSMOS_APP_SRC_VECTOR_INDEX_H_
#define VECTOR1_COSMOS_APP_SRC_VECTOR_INDEX_H_

// #include "../nvme/nvme_io_cmd.h"
#include "../data_buffer.h"
#include "../gs/growingsegment.h"
#include "../request_transform.h"
#include "../ss/super.h"
#include "../nvme/debug.h"



#include "xtime_l.h"

#define VECTOR_BUILD_PREFETCH_DEGREE 0 // VECTOR_BUILD_PREFETCH_DEGREE should be less then AVAILABLE_DATA_BUFFER_ENTRY_COUNT



#if AVAILABLE_DATA_BUFFER_ENTRY_COUNT<VECTOR_BUILD_PREFETCH_DEGREE
not should be compile :: this is inefficient code
#endif

// #include "hnsw.h"


/**
 * @brief Flag indicating whether the SSD is currently building vector index
 *
 * Tracks the operational status of the SSD, showing if it's actively processing
 * index-build operation at the present moment.
 *
 * @note Using char type to save memory as only boolean values are needed.
 */
  char isIndexBuilding;

/**
 * @brief Macros for managing the index building state
 *
 * These macros provide a clean interface for checking and modifying the
 * isIndexBuilding flag. They abstract the implementation details and ensure
 * consistent usage throughout the codebase.
 */
 // extern char isIndexBuilding;
#define IS_INDEX_BUILDING() (isIndexBuilding != 0)
#define IS_INDEX_NOT_BUILDING() (isIndexBuilding == 0)
#define SET_INDEX_BUILDING() (isIndexBuilding = 1)
#define CLEAR_INDEX_BUILDING() (isIndexBuilding = 0)

/**
 * @brief Pointer to the Sealed Segment (= Segment at the level upper than one)
 * currently being indexed.
 */
SUPER_SEALED_SEGMENT_INFO* targetSsForIndexing;

/**
 * @brief Constant representing no selected Sealed Segment for index building and macros for identifying the value of the
 * constant.
 *
 * Special value (0xffffffff) used to indicate that no Sealed Segment is being selected.
 */
 // extern SUPER_SEALED_SEGMENT_INFO *targetSsForIndexing;
#define SS_INDEX_TARGET_NONE ((SUPER_SEALED_SEGMENT_INFO *)0xffffffff)
#define HAS_NO_SS_INDEX_TARGET() (targetSsForIndexing == SS_INDEX_TARGET_NONE)
#define CLEAR_SS_INDEX_TARGET() (targetSsForIndexing = SS_INDEX_TARGET_NONE)

/**
  * @brief Pointer to the Sealed Segment to be searched.
  */
SUPER_SEALED_SEGMENT_INFO* targetSsForSearching;

/**
 * @brief Constant representing no selected Sealed Segment for searching and macros for identifying the value of the constant.
 */
 // extern SUPER_SEALED_SEGMENT_INFO *targetSsForSearching;
#define SS_SEARCH_TARGET_NONE ((SUPER_SEALED_SEGMENT_INFO *)0xffffffff)
#define HAS_NO_SS_SEARCH_TARGET() (targetSsForSearching == SS_SEARCH_TARGET_NONE)
#define CLEAR_SS_SEARCH_TARGET() (targetSsForSearching = SS_SEARCH_TARGET_NONE)

/**
 * @brief Constant representing an invalid of unassigned LPN and macro for
 * checking if an LPN has an address of page of the built index.
 */
#define INVALID_LPN ((unsigned int)0xffffffff)
#define HAS_INDEX_STORED_TO(lpn) ((lpn) != INVALID_LPN)
#define HAS_NO_INDEX_STORED_TO(lpn) ((lpn) == INVALID_LPN)
#define CLEAR_INDEX_STORAGE(lpn) ((lpn) = INVALID_LPN)

int hnswOndemandFieldOccupiedBy;
#define CLEAR_ONDEMAND_FIELD_INDICATOR() (hnswOndemandFieldOccupiedBy = -1)
#define SET_ONDEMAND_FIELD_WITH(lpn) (hnswOndemandFieldOccupiedBy = (lpn))
#define IS_ONDEMAND_FIELD(lpn) ((int)(hnswOndemandFieldOccupiedBy) == (int)(lpn))
#define DEBUG_VECTOR 14000
#define PREEMPTION_UNIT 1000
int curProcessingKey;
/**
* @brief Unique segment id that allocated to each segment.
*/
unsigned int nextSegmentId;

/**
 * @brief Macro for globally managing `nextSegmentId`;
 */
extern unsigned int nextSegmentId;
#define SET_SEGMENT_ID(segmentId)      \
  do {                                 \
    (segmentId) = nextSegmentId;       \
    nextSegmentId = nextSegmentId + 1; \
  } while (0)
#define CLEAR_NEXT_SEGMENT_ID() (nextSegmentId = 1)

/**
 * Pre-defined vector data information.
 */
#define PREDEFINED_DIMENSION 96
#define PREDEFINED_UNROLLING_DEGREE 16
#define PREDEFINED_VECTOR_SIZE (PREDEFINED_DIMENSION * sizeof(float))
#define VECTOR_SIZE_NLB \
  ((PREDEFINED_VECTOR_SIZE % 4096) ? ((PREDEFINED_VECTOR_SIZE / 4096) + 1) : (PREDEFINED_VECTOR_SIZE / 4096))
#define SQRT_ITERATION 16
#define SEARCH_SPACE_DEGREE 2

 /**
  * Pre-defined vector index control-related information.
  */
#define ONDEMAND_INDEX 1

#define HNSW 1

#define MAX_TOP_K 20

#define DELTA 1

#define INDEX_TYPE HNSW

#define SWAP_INT(x, y) \
  do {                 \
    int temp = x;      \
    x = y;             \
    y = temp;          \
  } while (0)

#define SWAP_FLOAT(x, y) \
  do {                   \
    float temp = x;      \
    x = y;               \
    y = temp;            \
  } while (0)


#define EPSILON 0.1
float cachedNorm;
float cachedInverseSqrtNorm;

/**
 * @brief Forward declaration for VectorIndex
 */
struct VectorIndex;

/**
 * @brief Data structure for search results
 */
typedef struct {
  unsigned int top_k;
  unsigned int key[MAX_TOP_K];
  float distance[MAX_TOP_K];
} VectorSearchResult;

/**
 * @brief General methods used in vector index
 *
 * @todo In the names of arguements of `calcVectorDistance`, the digit is simply
 * put at the end of each. This may be fixed.
 * @todo Not only cosine distance, but also another way for computing distance
 * between vectors could be introduced.
 */
float calcCosineSimilarityUnroll(const unsigned int vecDramAddr1[], const unsigned int vecDramAddr2[]);
float calcCosineSimilarityUnrollWithCachedNorm(const unsigned int vecDramAddr1[], const unsigned int vecDramAddr2[]);
float calcVectorDistance(const unsigned int vecDramAddr1[], const unsigned int vecDramAddr2[]);
float calcVectorDistanceWithCachedNorm(const unsigned int vecDramAddr1[], const unsigned int vecDramAddr2[]);
void calcVectorSum(float* result, const unsigned int vecDramAddr[]);
unsigned int get_distance(unsigned int dimension, unsigned int vid1, unsigned int vid2);

unsigned int vectorssd_rand();
unsigned int getSliceToBuf(unsigned int start_lsa);
void PinorUnPin1PageBuf(unsigned int dramAddr, unsigned int pin);
void PinOrUnPinBuf(unsigned int bufDramAddr[], unsigned int size, unsigned int pin);
static int loadRawVectorData(unsigned int retDramAddr[], unsigned int startLba, unsigned int endLba, unsigned int* need_sync_ret);

#define VECTOR_BUILD_PREFETCH_DEGREE 0
void WaitPrefetchedNANDOperation(unsigned int dramAddr[], unsigned int dramAddrN);
float my_sqrt_multiply(float number1, float number2);
float fastInverseSqrt(float number);
#endif  // VECTOR1_COSMOS_APP_SRC_VECTOR_INDEX_H_
