cmake_minimum_required(VERSION 3.10)
project(vectorssd_project)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(PYBIND11_FINDPYTHON ON)
find_package(pybind11 REQUIRED)

set(VECTORSSD_SOURCES
    src/vectorssd.cc
)

add_library(vectorssd_lib ${VECTORSSD_SOURCES})
target_include_directories(vectorssd_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(vectorssd_lib PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# main
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/vectorssd_test.cc")
    add_executable(vectorssd_test_exec src/vectorssd_test.cc)
    target_link_libraries(vectorssd_test_exec PRIVATE vectorssd_lib)
endif()


# Python 바인딩 모듈 Ref: https://github.com/pybind/cmake_example/blob/master/CMakeLists.txt
pybind11_add_module(vectorssd src/python_binding.cpp)
target_link_libraries(vectorssd PRIVATE vectorssd_lib)

target_compile_definitions(vectorssd PRIVATE VERSION_INFO=${EXAMPLE_VERSION_INFO})

set_target_properties(vectorssd PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(vectorssd_lib PRIVATE DEBUG)
endif()
