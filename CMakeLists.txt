cmake_minimum_required(VERSION 3.10)
project(hw-cpp VERSION 1.0 DESCRIPTION "hw client in C++")

# Find required dependencies for HTTP client
find_package(CURL REQUIRED)
set(YAML_CPP_DIR "<path-to-yaml-cpp>")
find_package(yaml-cpp REQUIRED)
include_directories(${YAML_CPP_DIR}/include)
link_directories(${YAML_CPP_DIR}/lib)
find_package(nlohmann_json REQUIRED)


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

# If using Armadillo
# Ensure that Armadillo is installed on the system
#set(ARMADILLO_LIBRARY "~/.local/lib/cmake/lib/libarmadillo.so")
#set(ARMADILLO_INCLUDE_DIR "~/.local/lib/cmake/include")
#set(ARMADILLO_INCLUDE_DIRS "/usr/include")

# Proto file for project
get_filename_component(fedn_proto "protos/fedn.proto" ABSOLUTE)
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

# # Define the source files for your project
# set(SOURCES
#     http_client.cpp
# )

# # Create an executable target
# add_executable(http-client ${SOURCES})

# # Link dependencies to the target
# target_link_libraries(http-client PRIVATE CURL::libcurl yaml-cpp ${YAML_CPP_LIBRARIES} nlohmann_json::nlohmann_json)

# add_executable(fedn-grpc "rpc.cpp")
# # if ${ARMADILLO_LIBRARY} is set, link it
# if(DEFINED ${ARMADILLO_LIBRARY})
#   target_link_libraries(fedn-grpc fedn_grpc_proto absl::flags absl::flags_parse ${_REFLECTION} ${_GRPC_GRPCPP} ${_PROTOBUF_LIBPROTOBUF} ${ARMADILLO_LIBRARY})
# else()
#   target_link_libraries(fedn-grpc fedn_grpc_proto absl::flags absl::flags_parse ${_REFLECTION} ${_GRPC_GRPCPP} ${_PROTOBUF_LIBPROTOBUF})
# endif()



# # CLIENT LIBRARY fednlib

# Add my_client as an executable
add_executable(my_client "my_client.cpp")

# # Add fednlib as a library
add_library(fednlib STATIC "fednlib.cpp")

# # Link the fednlib with libraries
target_link_libraries(fednlib PRIVATE 
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

# Link the executable with libraries
target_link_libraries(my_client
  fednlib
  fedn_grpc_proto
  yaml-cpp
  nlohmann_json::nlohmann_json
  ${CURL_LIBRARIES}
  ${_REFLECTION}
  ${_GRPC_GRPCPP}
  ${_PROTOBUF_LIBPROTOBUF}
)

# Include directories
include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})