#pragma once

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace vectorssd {

class DB;
struct VectorSearchReturn;

constexpr size_t VECTOR_DIMENSION = 96;
constexpr size_t VECTOR_ELEM_SIZE = sizeof(float);
constexpr size_t VECTOR_SIZE = VECTOR_DIMENSION * VECTOR_ELEM_SIZE;
constexpr size_t PAGE_SIZE = 4096;
constexpr size_t VECTOR_SIZE_NLB = (VECTOR_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
constexpr size_t TOP_K = 3;

constexpr size_t BytesToNLB(size_t bytes) { return (bytes + PAGE_SIZE - 1) / PAGE_SIZE; }

enum NvmeOpcode : uint8_t {
  NVME_CMD_KV_PUT = 0xA0,
  NVME_CMD_KV_GET = 0xA1,
  NVME_CMD_KV_DELETE = 0xA2,
  NVME_CMD_PAUSE_BUILD = 0xA3,
  NVME_CMD_RESUME_BUILD = 0xA4,
  NVME_CMD_VECTOR_SEARCH = 0xA8,
  NVME_CMD_VECTOR_BUILD = 0xA9,
  NVME_CMD_VECTOR_BUILD_STATUS = 0xA6,
  NVME_CMD_SST_PRINT = 0xA7,
};

struct VectorSearchReturn {
  explicit VectorSearchReturn(size_t top_k_) : top_k(top_k_){}
  ~VectorSearchReturn() = default;
  void PrintVID() const{
    printf("[ Print VID ]\n");
    for (unsigned int i = 0; i < top_k; i++) {
      std::cout << top_k_vector_id[i] << "\n";
    }
  }
  void PrintDistance() const {
        printf("[ Print Distance ]\n");

    for (unsigned int i = 0; i < top_k; i++) {
      std::cout << top_k_distance[i] << "\n";
    }
  }

  void AppendToResultCSVFile(const char* trace_path, int row, const char* result_path) const {
    std::ofstream outfile(result_path, std::ios::app);
    if (!outfile.is_open()) {
      std::cerr << "Error opening result file: " << result_path << std::endl;
      return;
    }
    for (size_t i = 0; i < top_k; ++i) {
      outfile << trace_path << "," << row << "," << top_k_vector_id[i] << "," << top_k_distance[i];
      if (i + 1 < top_k) outfile << "\t";
    }
    outfile << "\n";

    outfile.close();
  }
  // bool IsValid() const { return !top_k_vector_id.empty() && !top_k_distance.empty(); }
  size_t Size() const { return top_k; }

  unsigned int top_k;
  unsigned int top_k_vector_id[100];
  float top_k_distance[100];
  char padding[16384];
  // std::vector<unsigned int> top_k_vector_id;
  // std::vector<float> top_k_distance;
};

class DB {
 public:
  DB() : fd_(-1) {}
  ~DB();

  DB(const DB&) = delete;
  DB& operator=(const DB&) = delete;
  bool IsOpen() const { return fd_ != -1; }

  // Vector SSD APIs
  int Open(const std::string& dev, const std::string& collectionName);
  int Close();
  int Put(const std::string& key, const float vector[VECTOR_DIMENSION], const bool delayed_compaction = false);
  int Print(const std::string& key);
  int VectorSearch(const float query_vector[VECTOR_DIMENSION], int top_k, VectorSearchReturn* rets);
  int VectorBuild();
  int VectorBuildStatus();
  int PauseBuild();
  int ResumeBuild();

  // Mocking Vector SSD APIs
  int OpenMock(const std::string& dev, const std::string& collectionName);
  int CloseMock();
  int PutMock(const std::string& key, const float vector[VECTOR_DIMENSION], const bool delayed_compaction = false);
  int VectorSearchMock(const float query_vector[VECTOR_DIMENSION], int top_k, VectorSearchReturn* rets);
  int VectorBuildMock();

  // Interaction with file
  int OpenFile(const std::string& file);
  int PutToFile(const std::string& key, const float vector[VECTOR_DIMENSION]);

 private:
  int fd_;
  int nvme_passthru(uint8_t opcode, uint8_t flags, uint16_t rsvd, uint32_t nsid, uint32_t cdw2, uint32_t cdw3,
                    uint32_t cdw10, uint32_t cdw11, uint32_t cdw12, uint32_t cdw13, uint32_t cdw14, uint32_t cdw15,
                    uint32_t data_len, void* data, uint32_t* result);
};

}  // namespace vectorssd
