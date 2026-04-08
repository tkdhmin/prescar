// config.h
#ifndef VECTOR1_COSMOS_APP_SRC_VECTOR_HNSW_CONFIG_H_
#define VECTOR1_COSMOS_APP_SRC_VECTOR_HNSW_CONFIG_H_

#define MAX_LEVELS 3           // Max. level of HNSW
// #define M_PARAM 8              // M (Fixed for VectorSSD)
// #define EF_CONSTRUCTION 100    // Max. number of candidates (Fixed for VectorSSD)
#define M_PARAM 8              // M (Fixed for VectorSSD)
#define EF_CONSTRUCTION 32    // Max. number of candidates (Fixed for VectorSSD)
// #define M_PARAM 40              // M (Fixed for VectorSSD)
// #define EF_CONSTRUCTION 160    // Max. number of candidates (Fixed for VectorSSD)

#define LEVEL_PROBABILITY 0.1  // Probability for level assignment

#endif  // VECTOR1_COSMOS_APP_SRC_VECTOR_HNSW_CONFIG_H_
