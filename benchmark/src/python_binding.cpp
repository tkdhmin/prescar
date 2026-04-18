#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "vectorssd.h"

namespace py = pybind11;

/**
 * @note Python module named vectorssd
 */
PYBIND11_MODULE(vectorssd, m) {
  m.doc() = "VectorSSD Python bindings for high-performance vector storage and search";

  // Constants
  m.attr("VECTOR_DIMENSION") = vectorssd::VECTOR_DIMENSION;
  m.attr("VECTOR_ELEM_SIZE") = vectorssd::VECTOR_ELEM_SIZE;
  m.attr("VECTOR_SIZE") = vectorssd::VECTOR_SIZE;
  m.attr("PAGE_SIZE") = vectorssd::PAGE_SIZE;
  m.attr("VECTOR_SIZE_NLB") = vectorssd::VECTOR_SIZE_NLB;
  m.attr("TOP_K") = vectorssd::TOP_K;

  // Utility function
  m.def("bytes_to_nlb", &vectorssd::BytesToNLB, py::arg("bytes"), "Convert bytes to Number of Logical Blocks");

  // NvmeOpcode enum
  py::enum_<vectorssd::NvmeOpcode>(m, "NvmeOpcode")
      .value("KV_PUT", vectorssd::NVME_CMD_KV_PUT)
      .value("KV_GET", vectorssd::NVME_CMD_KV_GET)
      .value("KV_DELETE", vectorssd::NVME_CMD_KV_DELETE)
      .value("VECTOR_SEARCH", vectorssd::NVME_CMD_VECTOR_SEARCH)
      .value("VECTOR_BUILD", vectorssd::NVME_CMD_VECTOR_BUILD)
      .export_values();

  // VectorSearchReturn class binding
  py::class_<vectorssd::VectorSearchReturn>(m, "VectorSearchReturn")
      .def(py::init<size_t>(), py::arg("top_k"))
      .def_readonly("top_k", &vectorssd::VectorSearchReturn::top_k)
      .def_readonly("top_k_vector_id", &vectorssd::VectorSearchReturn::top_k_vector_id)
      .def_readonly("top_k_distance", &vectorssd::VectorSearchReturn::top_k_distance)
      .def("print_distance", &vectorssd::VectorSearchReturn::PrintDistance, "Print all distances to stdout")
      .def("append_to_csv", &vectorssd::VectorSearchReturn::AppendToResultCSVFile, py::arg("trace_path"),
           py::arg("row"), py::arg("result_path"), "Append search results to CSV file")
      // .def("is_valid", &vectorssd::VectorSearchReturn::IsValid, "Check if search results are valid")
      .def("size", &vectorssd::VectorSearchReturn::Size, "Get the number of search results")
      .def("__len__", &vectorssd::VectorSearchReturn::Size)
      .def("__repr__", [](const vectorssd::VectorSearchReturn& vsr) {
        return "<VectorSearchReturn top_k=" + std::to_string(vsr.top_k) + ">";
      });

  // DB class binding
  py::class_<vectorssd::DB>(m, "DB")
      .def(py::init<>())
      .def("is_open", &vectorssd::DB::IsOpen, "Check if database connection is open")

      .def("open", &vectorssd::DB::Open, py::arg("device_path"), py::arg("collection_name"), "Open connection to Vector SSD device")

      .def("open_mock", &vectorssd::DB::OpenMock, py::arg("device_path"), py::arg("collection_name"), "OpenMock")

      .def("close", &vectorssd::DB::Close, "Close the database connection")

      .def("close_mock", &vectorssd::DB::CloseMock, "CloseMock")

      .def(
          "print",
          [](vectorssd::DB& self, const std::string& key) {
            // py::buffer_info buf = vector.request();

            // if (buf.ndim != 1) {
            //   throw std::runtime_error("Vector must be 1-dimensional");
            // }

            // if (buf.size != vectorssd::VECTOR_DIMENSION) {
            //   throw std::runtime_error("Vector must have exactly " + std::to_string(vectorssd::VECTOR_DIMENSION) +
            //                            " dimensions");
            // }

            // float* ptr = static_cast<float*>(buf.ptr);
            return self.Print(key);
          },
          py::arg("key"), "Print given lpn key")

      .def(
          "put",
          [](vectorssd::DB& self, const std::string& key, py::array_t<float> vector, const bool delayed_compaction) {
            py::buffer_info buf = vector.request();

            if (buf.ndim != 1) {
              throw std::runtime_error("Vector must be 1-dimensional");
            }

            if (buf.size != vectorssd::VECTOR_DIMENSION) {
              throw std::runtime_error("Vector must have exactly " + std::to_string(vectorssd::VECTOR_DIMENSION) +
                                       " dimensions");
            }

            float* ptr = static_cast<float*>(buf.ptr);
            return self.Put(key, ptr, delayed_compaction);
          },
          py::arg("key"), py::arg("vector"), py::arg("delayed_compaction") = false, "Store vector to device with given key")

      .def(
          "put_mock",
          [](vectorssd::DB& self, const std::string& key, py::array_t<float> vector, const bool delayed_compaction) {
            py::buffer_info buf = vector.request();

            if (buf.ndim != 1) {
              throw std::runtime_error("Vector must be 1-dimensional");
            }

            if (buf.size != vectorssd::VECTOR_DIMENSION) {
              throw std::runtime_error("Vector must have exactly " + std::to_string(vectorssd::VECTOR_DIMENSION) +
                                      " dimensions");
            }

            float* ptr = static_cast<float*>(buf.ptr);
            return self.PutMock(key, ptr, delayed_compaction);
          },
          py::arg("key"),
          py::arg("vector"),
          py::arg("delayed_compaction") = false, "PutMock")


      .def(
          "vector_search",
          [](vectorssd::DB& self, py::array_t<float> query_vector, int top_k) {
            py::buffer_info buf = query_vector.request();

            if (buf.ndim != 1) {
              throw std::runtime_error("Query vector must be 1-dimensional");
            }

            if (buf.size != vectorssd::VECTOR_DIMENSION) {
              throw std::runtime_error("Query vector must have exactly " + std::to_string(vectorssd::VECTOR_DIMENSION) +
                                       " dimensions");
            }

            if (top_k <= 0) {
              throw std::runtime_error("top_k must be positive");
            }

            auto result = std::make_unique<vectorssd::VectorSearchReturn>(top_k);
            float* ptr = static_cast<float*>(buf.ptr);
            int ret = self.VectorSearch(ptr, top_k, result.get());
            if (ret < 0) {
              if (ret == -2) {
                throw std::runtime_error("Vector index not built. Call vector_build() first.");
              } else {
                throw std::runtime_error("Vector search failed with error: " + std::to_string(ret));
              }
            }
            // result.PrintVID();
            // result.PrintDistance()
            return result.release();
          },
          py::arg("query_vector"), py::arg("top_k"), "Search for similar vectors. Returns VectorSearchReturn object.",
          py::return_value_policy::take_ownership)

      .def(
          "vector_search_mock",
          [](vectorssd::DB& self, py::array_t<float> query_vector, int top_k) {
            py::buffer_info buf = query_vector.request();

            if (buf.ndim != 1) {
              throw std::runtime_error("Query vector must be 1-dimensional");
            }

            if (buf.size != vectorssd::VECTOR_DIMENSION) {
              throw std::runtime_error("Query vector must have exactly " + std::to_string(vectorssd::VECTOR_DIMENSION) +
                                       " dimensions");
            }

            if (top_k <= 0) {
              throw std::runtime_error("top_k must be positive");
            }

            auto result = std::make_unique<vectorssd::VectorSearchReturn>(top_k);
            float* ptr = static_cast<float*>(buf.ptr);

            int ret = self.VectorSearchMock(ptr, top_k, result.get());
            if (ret < 0) {
              if (ret == -2) {
                throw std::runtime_error("Vector index not built. Call vector_build() first.");
              } else {
                throw std::runtime_error("Vector search failed with error: " + std::to_string(ret));
              }
            }

            return result.release();
          },
          py::arg("query_vector"), py::arg("top_k"), "VectorSearchMock", py::return_value_policy::take_ownership)

      .def("vector_build", &vectorssd::DB::VectorBuild, "Build vector index for fast similarity search")

      .def(
          "vector_build_status",
          [](vectorssd::DB& self) {
            return self.VectorBuildStatus();
          }, "Build Status")
      .def(
          "pause_build",
          [](vectorssd::DB& self) {
            return self.PauseBuild();
          }, "Pause")
      .def(
          "resume_build",
          [](vectorssd::DB& self) {
            return self.ResumeBuild();
          }, "Resume")
      .def("vector_build_mock", &vectorssd::DB::VectorBuildMock, "VectorBuildMock")

      // Context manager support
      .def("__enter__", [](vectorssd::DB& self) -> vectorssd::DB& { return self; })
      .def("__exit__", [](vectorssd::DB& self, py::object, py::object, py::object) { self.Close(); })

      .def("__repr__", [](const vectorssd::DB& db) {
        return "<VectorSSD.DB open=" + std::string(db.IsOpen() ? "True" : "False") + ">";
      });

  py::register_exception<std::runtime_error>(m, "VectorSSDError");
}
