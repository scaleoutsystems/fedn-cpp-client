cmake_minimum_required(VERSION 3.10)
project(hw-cpp-example VERSION 1.0 DESCRIPTION "Example FEDn client in C++")

# Find required dependencies for HTTP client
find_package(CURL REQUIRED)

if(NOT CURL_FOUND)
    message(FATAL_ERROR "libcurl not found! Please install or set the path.")
endif()

# Find required dependencies for YAML
set(YAML_CPP_DIR "<path-to-yaml-cpp>")
find_package(yaml-cpp REQUIRED)

if(NOT yaml-cpp_FOUND)
    message(FATAL_ERROR "yaml-cpp not found! Please install or set the path.")
endif()

include_directories(${YAML_CPP_DIR}/include)
link_directories(${YAML_CPP_DIR}/lib)
include_directories(${YAML_CPP_INCLUDE_DIRS})

# Find required dependencies for JSON
find_package(nlohmann_json REQUIRED)

if(NOT nlohmann_json_FOUND)
    message(FATAL_ERROR "nlohmann_json not found! Please install or set the path.")
endif()

# Protobuf and gRPC
option(protobuf_MODULE_COMPATIBLE TRUE)
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

find_package(gRPC CONFIG REQUIRED)
message(STATUS "Using gRPC ${gRPC_VERSION}")

set(_GRPC_GRPCPP gRPC::grpc++)
if(CMAKE_CROSSCOMPILING)
    find_program(_GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)
else()
    set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)
endif()

# Set the C++ standard to C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set the path to LibTorch
set(CMAKE_PREFIX_PATH "<path-to-libtorch>") # Set the path to the directory where you have unzip the libtorch. 
find_package(Torch REQUIRED)

if(NOT Torch_FOUND)
    message(FATAL_ERROR "libtorch not found! Please install or set the path.")
endif()

include_directories(${TORCH_INCLUDE_DIRS})

# set(CMAKE_BUILD_RPATH " <path-to-lib-dir>")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -I<path-to-include-dir>")
# set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L")

message(STATUS "TORCH_INCLUDE_DIRS: ${TORCH_INCLUDE_DIRS}")
message(STATUS "TORCH_LIBRARIES: ${TORCH_LIBRARIES}")


# Armadillo
#set(ARMADILLO_LIBRARY "/usr/lib/libarmadillo.so")
#set(ARMADILLO_INCLUDE_DIR "/usr/include")
#set(ARMADILLO_INCLUDE_DIRS "${ARMADILLO_INCLUDE_DIR}")
#link_directories(/usr/lib)
#find_package(Armadillo REQUIRED)

# Proto file for project
get_filename_component(fedn_proto "../../src/protos/fedn.proto" ABSOLUTE)
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
    DEPENDS "${fedn_proto}"
)

# Include generated *.pb.h files
include_directories("${CMAKE_CURRENT_BINARY_DIR}")

# fedn_grpc_proto
add_library(fedn_grpc_proto
    ${fedn_grpc_srcs}
    ${fedn_grpc_hdrs}
    ${fedn_proto_srcs}
    ${fedn_proto_hdrs}
)
target_link_libraries(fedn_grpc_proto
    ${_REFLECTION}
    ${_GRPC_GRPCPP}
    ${_PROTOBUF_LIBPROTOBUF}
)

# Find cnpy
include_directories("<path-to-cnpy-includes>")  # Ensure cnpy headers are included
link_directories("<path-to-cnpy-libraries>")  # Ensure cnpy library is linked. For example: /usr/local/lib 



# Manually specify the path where fednlib was built
set(FEDNLIB_DIR "${CMAKE_SOURCE_DIR}/../../build")  # Adjust this path to point to where libfednlib.a is built

# Include fednlib's include directory for fednlib.h and other headers
include_directories("${CMAKE_SOURCE_DIR}/../../include")  # Point to the headers in the parent include directory

# Specify the directory where the built fednlib library is located
link_directories(${FEDNLIB_DIR})  # Add the directory where libfednlib.a resides

# Add my-client as an executable
add_executable(mnist-libtorch "mnist-libtorch.cpp")

target_link_libraries(mnist-libtorch
  fednlib
  fedn_grpc_proto
  yaml-cpp
  nlohmann_json::nlohmann_json
  ${CURL_LIBRARIES}
  ${_REFLECTION}
  ${_GRPC_GRPCPP}
  ${_PROTOBUF_LIBPROTOBUF}
# armadillo
  pthread
  ${TORCH_LIBRARIES}
  cnpy
)
