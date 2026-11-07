cmake_minimum_required(VERSION 3.20)
project(perlin_render CXX)

add_library(perlin_render_chunk STATIC
    chunk/FaceMask.cpp
    chunk/GreedyMesher.cpp
    chunk/Residency.cpp)
target_include_directories(perlin_render_chunk PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(perlin_render_chunk PUBLIC cxx_std_20)

if(PROJECT_IS_TOP_LEVEL)
    enable_testing()
    add_executable(chunk_system_selftest "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/chunk_system_selftest.cpp")
    target_link_libraries(chunk_system_selftest PRIVATE perlin_render_chunk)
    add_test(NAME chunk_system_selftest COMMAND chunk_system_selftest)
endif()
