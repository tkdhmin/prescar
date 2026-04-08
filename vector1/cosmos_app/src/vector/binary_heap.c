#include "binary_heap.h"

static inline int heap_parent(int i) { return (i - 1) / 2; }
static inline int heap_left_child(int i) { return 2 * i + 1; }
static inline int heap_right_child(int i) { return 2 * i + 2; }
static inline void min_heap_swap(MinHeap *heap, int i, int j) {
  int tmp_idx = heap->indices[i];
  float tmp_dist = heap->distances[i];

  heap->indices[i] = heap->indices[j];
  heap->distances[i] = heap->distances[j];

  heap->indices[j] = tmp_idx;
  heap->distances[j] = tmp_dist;
}
static inline void max_heap_swap(MaxHeap *heap, int i, int j) {
  int tmp_idx = heap->indices[i];
  float tmp_dist = heap->distances[i];

  heap->indices[i] = heap->indices[j];
  heap->distances[i] = heap->distances[j];

  heap->indices[j] = tmp_idx;
  heap->distances[j] = tmp_dist;
}
/**
 * MinHeap data structure for accessing smallest distance entries
 */

void min_heap_init(MinHeap *heap) { heap->size = 0; }
void min_heap_sift_up(MinHeap *heap, int i) {
  int parent_of_i = heap_parent(i);
  while (i > 0 && heap->distances[i] < heap->distances[parent_of_i]) {
    min_heap_swap(heap, i, parent_of_i);
    i = parent_of_i;
    parent_of_i = heap_parent(i);
  }
}
void min_heap_sift_down(MinHeap *heap, int i) {
  while (1) {
    int min_idx = i;
    int left = heap_left_child(i);
    int right = heap_right_child(i);

    if (left < heap->size && heap->distances[left] < heap->distances[min_idx]) {
      min_idx = left;
    }

    if (right < heap->size && heap->distances[right] < heap->distances[min_idx]) {
      min_idx = right;
    }

    if (min_idx == i) break;

    min_heap_swap(heap, i, min_idx);
    i = min_idx;
  }
}

void min_heap_insert(MinHeap *heap, int idx, float dist) {
  if (heap->size >= EF_CONSTRUCTION) {
    return;
  }
  heap->indices[heap->size] = idx;
  heap->distances[heap->size] = dist;
  min_heap_sift_up(heap, heap->size);

  (heap->size)++;
}

void min_heap_pop(MinHeap *heap, int *idx, float *dist) {
  if (heap->size <= 0) return;
  *idx = heap->indices[0];
  *dist = heap->distances[0];

  heap->indices[0] = heap->indices[heap->size - 1];
  heap->distances[0] = heap->distances[heap->size - 1];
  (heap->size)--;
  min_heap_sift_down(heap, 0);
}

/**
 * MaxHeap for storing and managing known nearest neighbors
 */

void max_heap_init(MaxHeap *heap, int capacity) {
  heap->size = 0;
  heap->capacity = capacity;
}

void max_heap_sift_up(MaxHeap *heap, int i) {
  int parent_of_i = heap_parent(i);
  while (i > 0 && heap->distances[i] > heap->distances[parent_of_i]) {
    max_heap_swap(heap, i, parent_of_i);
    i = parent_of_i;
    parent_of_i = heap_parent(i);
  }
}

void max_heap_sift_down(MaxHeap *heap, int i) {
  while (1) {
    int min_idx = i;
    int left = heap_left_child(i);
    int right = heap_right_child(i);

    if (left < heap->size && heap->distances[left] > heap->distances[min_idx]) {
      min_idx = left;
    }

    if (right < heap->size && heap->distances[right] > heap->distances[min_idx]) {
      min_idx = right;
    }

    if (min_idx == i) break;

    max_heap_swap(heap, i, min_idx);
    i = min_idx;
  }
}
void max_heap_insert(MaxHeap *heap, int idx, float dist) {
  if (heap->size < heap->capacity) {
    heap->indices[heap->size] = idx;
    heap->distances[heap->size] = dist;
    max_heap_sift_up(heap, heap->size);
    heap->size++;
  } else if (dist < heap->distances[0]) {
    heap->indices[0] = idx;
    heap->distances[0] = dist;
    max_heap_sift_down(heap, 0);
  }
  // if dist> heap->distances[0], do nothing
  // heap->distances[0] is largest elem
}
