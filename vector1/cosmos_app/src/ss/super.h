#ifndef VECTOR1_COSMOS_APP_SRC_SS_SUPER_H_
#define VECTOR1_COSMOS_APP_SRC_SS_SUPER_H_

// #include "../vector/index.h"

// #define VALUE_LOG_BASE_LBA 0x00300000
#define VALUE_LOG_BASE_LBA 0

#define MAX_LEVEL 4
#define MAX_SEALED_LEVEL0 4
#define MAX_SEALED_LEVEL1 (400)
#define MAX_SEALED_LEVEL2 (20)
#define MAX_SEALED_LEVEL3 (1)
#define MAX_SEALED_LEVEL4 (1)


//NOTE: SLICES_PER_SSD - BYTES_PER_DATA_REGION_OF_SLICE
#define REVERSE_VALUE_LOG_BASE_LBA 0x1ffc000
extern unsigned int write_pointer_lba;
extern unsigned int reverse_write_pointer_lpn;

typedef struct _SUPER_LEVEL_INFO {
  unsigned int level_count[MAX_LEVEL];
} SUPER_LEVEL_INFO;

typedef struct _SUPER_SEALED_SEGMENT_LIST {
  unsigned int head;
  unsigned int tail;
} SUPER_SEALED_SEGMENT_LIST;

typedef struct _SUPER_SEALED_SEGMENT_INFO {
  unsigned int level;
  unsigned int index_size;
  unsigned int data_size;
  // NOTE: The `total_entry` cannot effectively deliver the number of entry meaning, suggesting change of the
  // name to the `entry_count`.
  unsigned int total_entry;
  unsigned int min_key;
  unsigned int max_key;
  unsigned int head_lpn;
  unsigned int tail_lpn;
  unsigned int vector_index_start_lpn;
  unsigned int vector_index_size;
  unsigned int search_completed;
} SUPER_SEALED_SEGMENT_INFO;

#endif  // VECTOR1_COSMOS_APP_SRC_SS_SUPER_H_
