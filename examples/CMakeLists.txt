cmake_minimum_required(VERSION 3.10)
project(hw-cpp-example VERSION 1.0 DESCRIPTION "Example FEDn client in C++")


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
set(ARMADILLO_LIBRARY "<path-to-armadillo>")
set(ARMADILLO_INCLUDE_DIR "<path-to-armadillo-include-dir>")
set(ARMADILLO_INCLUDE_DIRS "<path-to-armadillo-include-dirs>")

link_directories(/opt/homebrew/opt/armadillo/lib)

# Proto file for project
get_filename_component(fedn_proto "../src/protos/fedn.proto" ABSOLUTE)
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

# Manually specify the path where fednlib was built (adjust path if necessary)
set(FEDNLIB_DIR "${CMAKE_SOURCE_DIR}/../build")  # Adjust this path to point to where libfednlib.a is built

# Include fednlib's include directory for fednlib.h and other headers
include_directories(${CMAKE_SOURCE_DIR}/../include)  # Point to the headers in the parent include directory

# Specify the directory where the built fednlib library is located
link_directories(${FEDNLIB_DIR})  # Add the directory where libfednlib.a resides

# Include the directory where cnpy.h is located
include_directories("../../cnpy")

# Set the C++ standard to C++17, which is required by LibTorch
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set the path to the directory where LibTorch was unzipped
set(CMAKE_PREFIX_PATH "../../Documents/libtorch")

# Find LibTorch (PyTorch C++ API)
find_package(Torch REQUIRED)

# Include LibTorch headers
include_directories(${TORCH_INCLUDE_DIRS})

set(CMAKE_BUILD_RPATH "<path-to-libtorch>")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -I/opt/homebrew/opt/libomp/include")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L/opt/homebrew/opt/libomp/lib")

find_package(Armadillo REQUIRED)

# Add my_client as an executable
add_executable(mnist-pytorch "mnist-pytorch.cpp")

# Link the executable with libraries
target_link_libraries(mnist-pytorch
  fednlib
  fedn_grpc_proto
  yaml-cpp
  nlohmann_json::nlohmann_json
  ${CURL_LIBRARIES}
  ${_REFLECTION}
  ${_GRPC_GRPCPP}
  ${_PROTOBUF_LIBPROTOBUF}
  ${TORCH_LIBRARIES} 
  # ../../cnpy/build/libcnpy.a
  <path-to-libtorch-a>
  armadillo
)

# Set the include directories for the example
include_directories(${CMAKE_SOURCE_DIR}/include)

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl")#,-rpath ${TORCH_INSTALL_PREFIX}/lib")