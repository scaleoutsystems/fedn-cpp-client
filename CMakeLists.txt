cmake_minimum_required(VERSION 3.18)
project(fedn-cpp-client LANGUAGES CXX)

# Let old subprojects (like cnpy) configure without hard errors
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# (macOS) scrub as-needed flags that cause linking pain on Darwin
if(APPLE)
  foreach(_var
    CMAKE_EXE_LINKER_FLAGS
    CMAKE_MODULE_LINKER_FLAGS
    CMAKE_SHARED_LINKER_FLAGS
    CMAKE_STATIC_LINKER_FLAGS
  )
    string(REPLACE "-Wl,--as-needed"    "" ${_var} "${${_var}}")
    string(REPLACE "-Wl,--no-as-needed" "" ${_var} "${${_var}}")
    string(REPLACE "--as-needed"        "" ${_var} "${${_var}}")
    string(REPLACE "--no-as-needed"     "" ${_var} "${${_var}}")
  endforeach()
endif()

include(FetchContent)

# ---- Dependencies ----
find_package(ZLIB REQUIRED)
find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)

# pick the correct imported target name
if (TARGET yaml-cpp::yaml-cpp)
  set(YAMLCPP_TARGET yaml-cpp::yaml-cpp)
elseif (TARGET yaml-cpp)
  set(YAMLCPP_TARGET yaml-cpp)
else()
  message(FATAL_ERROR "yaml-cpp imported target not found. Pass -Dyaml-cpp_DIR to the yaml-cpp *Config* directory.")
endif()


# If you installed protobuf/grpc to ~/.local
option(protobuf_MODULE_COMPATIBLE TRUE)
list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/.local/lib/cmake")

find_package(Protobuf CONFIG REQUIRED)
message(STATUS "Using protobuf ${Protobuf_VERSION}")

find_package(gRPC CONFIG REQUIRED)
message(STATUS "Using gRPC ${gRPC_VERSION}")

set(_PROTOBUF_LIBPROTOBUF protobuf::libprotobuf)
set(_GRPC_GRPCPP        gRPC::grpc++)
set(_REFLECTION         gRPC::grpc++_reflection)
set(_PROTOC             $<TARGET_FILE:protobuf::protoc>)
set(_GRPC_CPP_PLUGIN    $<TARGET_FILE:gRPC::grpc_cpp_plugin>)

# cnpy via FetchContent
FetchContent_Declare(
  cnpy
  GIT_REPOSITORY https://github.com/rogersce/cnpy.git
  GIT_TAG        master
)
FetchContent_MakeAvailable(cnpy)
FetchContent_GetProperties(cnpy)
set(CNPY_INCLUDE_DIR "${cnpy_SOURCE_DIR}" CACHE INTERNAL "cnpy include dir")
target_include_directories(cnpy INTERFACE "${cnpy_SOURCE_DIR}")

# ---- Protos (generated once at root) ----
get_filename_component(FEDN_PROTO "${CMAKE_CURRENT_SOURCE_DIR}/src/protos/fedn.proto" ABSOLUTE)
get_filename_component(FEDN_PROTO_DIR "${FEDN_PROTO}" PATH)

set(GEN_PROTO_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GEN_PROTO_DIR}")

set(FEDN_PROTO_SRCS "${GEN_PROTO_DIR}/fedn.pb.cc")
set(FEDN_PROTO_HDRS "${GEN_PROTO_DIR}/fedn.pb.h")
set(FEDN_GRPC_SRCS "${GEN_PROTO_DIR}/fedn.grpc.pb.cc")
set(FEDN_GRPC_HDRS "${GEN_PROTO_DIR}/fedn.grpc.pb.h")

add_custom_command(
  OUTPUT "${FEDN_PROTO_SRCS}" "${FEDN_PROTO_HDRS}" "${FEDN_GRPC_SRCS}" "${FEDN_GRPC_HDRS}"
  COMMAND ${_PROTOC}
  ARGS --grpc_out "${GEN_PROTO_DIR}"
       --cpp_out  "${GEN_PROTO_DIR}"
       -I "${FEDN_PROTO_DIR}"
       --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN}"
       "${FEDN_PROTO}"
  DEPENDS "${FEDN_PROTO}"
  COMMENT "Generating gRPC and Protobuf sources"
)

add_library(fedn_grpc_proto STATIC
  "${FEDN_PROTO_SRCS}" "${FEDN_PROTO_HDRS}"
  "${FEDN_GRPC_SRCS}"  "${FEDN_GRPC_HDRS}"
)
target_include_directories(fedn_grpc_proto PUBLIC "${GEN_PROTO_DIR}")
target_link_libraries(fedn_grpc_proto
  PUBLIC
    ${_GRPC_GRPCPP}
    ${_PROTOBUF_LIBPROTOBUF}
    ${_REFLECTION}
)

# ---- fednlib (define ONCE, link everything here) ----
add_library(fednlib STATIC
  src/http.cpp
  src/grpc.cpp
  src/fedn.cpp
  src/utils.cpp
)

target_include_directories(fednlib
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${GEN_PROTO_DIR}"           # so consumers can #include generated headers via fednlib
)

target_link_libraries(fednlib
  PUBLIC
    fedn_grpc_proto
    CURL::libcurl
    ${YAMLCPP_TARGET}  
    nlohmann_json::nlohmann_json
  PRIVATE
    cnpy
    ZLIB::ZLIB
)

# Optional LibTorch example (off by default)
option(BUILD_MNIST_LIBTORCH "Build LibTorch example" OFF)
if(BUILD_MNIST_LIBTORCH)
  find_package(Torch REQUIRED)
  add_executable(mnist_libtorch_stdvec examples/mnist-libtorch-stdvec/mnist-libtorch-stdvec.cpp)
  target_link_libraries(mnist_libtorch_stdvec PRIVATE fednlib "${TORCH_LIBRARIES}")
  set_property(TARGET mnist_libtorch_stdvec PROPERTY CXX_STANDARD 14)
endif()

# ---- Example client (NO proto generation here; just link) ----
add_subdirectory(examples/my-client)
