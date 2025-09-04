cmake_minimum_required(VERSION 3.10)
project(hw-cpp-example LANGUAGES CXX)

add_executable(my-client my-client.cpp)

# Only link deps you actually include from the client.
# yaml-cpp is already PUBLIC on fednlib, so do NOT link it here.
target_link_libraries(my-client PRIVATE
  fednlib
  cnpy
  ZLIB::ZLIB
  nlohmann_json::nlohmann_json
  CURL::libcurl
)

# Includes for client (fednlib public headers are already exposed by link)
target_include_directories(my-client PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
  "${CNPY_INCLUDE_DIR}"
)
