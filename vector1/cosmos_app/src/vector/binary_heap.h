#ifndef VECTOR1_COSMOS_APP_SRC_VECTOR_BINARY_HEAP_H_
#define VECTOR1_COSMOS_APP_SRC_VECTOR_BINARY_HEAP_H_

#include "hnsw_config.h"

typedef struct {
  int indices[EF_CONSTRUCTION];
  float distances[EF_CONSTRUCTION];
  int size;
} MinHeap;

typedef struct {
  int indices[EF_CONSTRUCTION];
  float distances[EF_CONSTRUCTION];
  int size;
  int capacity;
} MaxHeap;

/*
 * Heap Data Structure Implementation
 * =====================================================
 * This section contains functions for implementing and manipulating
 * both min-heap and max-heap data structures. The functions are
 * organized by helper operations, min-heap operations, and max-heap
 * operations.
 */
static inline int heap_parent(int i);
static inline int heap_left_child(int i);
static inline int heap_right_child(int i);
static inline void min_heap_swap(MinHeap *heap, int i, int j);
static inline void max_heap_swap(MaxHeap *heap, int i, int j);

void min_heap_init(MinHeap *heap);
void min_heap_sift_up(MinHeap *heap, int i);
void min_heap_sift_down(MinHeap *heap, int i);
void min_heap_insert(MinHeap *heap, int idx, float dist);
void min_heap_pop(MinHeap *heap, int *idx, float *dist);

void max_heap_init(MaxHeap *heap, int capacity);
void max_heap_sift_up(MaxHeap *heap, int i);
void max_heap_sift_down(MaxHeap *heap, int i);
void max_heap_insert(MaxHeap *heap, int idx, float dist);

#endif  // VECTOR1_COSMOS_APP_SRC_VECTOR_BINARY_HEAP_H_
