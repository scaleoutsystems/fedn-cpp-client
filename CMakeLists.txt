cmake_minimum_required(VERSION 3.10)
project(hw-cpp VERSION 1.0 DESCRIPTION "hw client in C++")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find required dependencies for HTTP client
find_package(CURL REQUIRED)
if(NOT CURL_FOUND)
    message(FATAL_ERROR "curl not found! Please set the path.")
endif()

set(YAML_CPP_DIR "<path-to-yaml-cpp>")
find_package(yaml-cpp REQUIRED)

if(NOT yaml-cpp_FOUND)
    message(FATAL_ERROR "yaml-cpp not found! Please set the path.")
endif()

include_directories(${YAML_CPP_DIR}/include)
link_directories(${YAML_CPP_DIR}/lib)

find_package(nlohmann_json REQUIRED)

if(NOT nlohmann_json_FOUND)
    message(FATAL_ERROR "nlohmann_json not found! Please set the path.")
endif()

include_directories(${YAML_CPP_INCLUDE_DIRS})

# Protobuf and gRPC
# This assumes that gRPC and all its dependencies are already installed
# on this system, so they can be located by find_package().

# Find Protobuf installation
# Looks for protobuf-config.cmake file installed by Protobuf's cmake installation.
option(protobuf_MODULE_COMPATIBLE TRUE)
# Set CMAKE_PREFIX_PATH to the directory containing protobuf-config.cmake
set(CMAKE_PREFIX_PATH "~/.local/lib/cmake/")
find_package(Protobuf CONFIG REQUIRED)
message(STATUS "Using protobuf ${Protobuf_VERSION}")

set(_PROTOBUF_LIBPROTOBUF protobuf::libprotobuf)
set(_REFLECTION gRPC::grpc++_reflection)
if(CMAKE_CROSSCOMPILING)
find_program(_PROTOBUF_PROTOC protoc)
else()
set(_PROTOBUF_PROTOC $<TARGET_FILE:protobuf::protoc>)
endif()

# Find gRPC installation
# Looks for gRPCConfig.cmake file installed by gRPC's cmake installation.
find_package(gRPC CONFIG REQUIRED)
message(STATUS "Using gRPC ${gRPC_VERSION}")

set(_GRPC_GRPCPP gRPC::grpc++)
if(CMAKE_CROSSCOMPILING)
find_program(_GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)
else()
set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)
endif()

# Proto file for project
get_filename_component(fedn_proto "src/protos/fedn.proto" ABSOLUTE)
get_filename_component(fedn_proto_path "${fedn_proto}" PATH)

# Generated sources
set(fedn_proto_srcs "${CMAKE_CURRENT_BINARY_DIR}/fedn.pb.cc")
set(fedn_proto_hdrs "${CMAKE_CURRENT_BINARY_DIR}/fedn.pb.h")
set(fedn_grpc_srcs "${CMAKE_CURRENT_BINARY_DIR}/fedn.grpc.pb.cc")
set(fedn_grpc_hdrs "${CMAKE_CURRENT_BINARY_DIR}/fedn.grpc.pb.h")
add_custom_command(
      OUTPUT "${fedn_proto_srcs}" "${fedn_proto_hdrs}" "${fedn_grpc_srcs}" "${fedn_grpc_hdrs}"
      COMMAND ${_PROTOBUF_PROTOC}
      ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
        --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
        -I "${fedn_proto_path}"
        --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
        "${fedn_proto}"
      DEPENDS "${fedn_proto}")

# Include generated *.pb.h files
# if ${ARMADILLO_INCLUDE_DIR} is set, include it
if(DEFINED ${ARMADILLO_INCLUDE_DIR})
  include_directories("${CMAKE_CURRENT_BINARY_DIR}" "${ARMADILLO_INCLUDE_DIRS}" "${ARMADILLO_INCLUDE_DIR}")
else()
  include_directories("${CMAKE_CURRENT_BINARY_DIR}")
endif()

# fedn_grpc_proto
add_library(fedn_grpc_proto
  ${fedn_grpc_srcs}
  ${fedn_grpc_hdrs}
  ${fedn_proto_srcs}
  ${fedn_proto_hdrs})
target_link_libraries(fedn_grpc_proto
  ${_REFLECTION}
  ${_GRPC_GRPCPP}
  ${_PROTOBUF_LIBPROTOBUF})

# Include directories fednlib
include_directories(include)

# Source files fednlib
set(SOURCES
    src/http.cpp
    src/grpc.cpp
    src/fedn.cpp
    src/utils.cpp
)

# Add fednlib as a library
add_library(fednlib STATIC ${SOURCES})

# Create an alias target
add_library(fednlib::fednlib ALIAS fednlib)

# Link the fednlib with libraries
target_link_libraries(fednlib PUBLIC 
  fedn_grpc_proto 
  absl::flags 
  absl::flags_parse 
  ${_REFLECTION} 
  ${_GRPC_GRPCPP} 
  ${_PROTOBUF_LIBPROTOBUF} 
  CURL::libcurl 
  yaml-cpp 
  ${YAML_CPP_LIBRARIES} 
  nlohmann_json::nlohmann_json
)

if(DEFINED ${ARMADILLO_LIBRARY})
target_link_libraries(fednlib PRIVATE ${ARMADILLO_LIBRARY})
endif()

# Link header files to fednlib
target_include_directories(fednlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Include directories
include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})


