#!/bin/bash

set -e  # Exit immediately if a command exits with a non-zero status.
set -x  # Print commands and their arguments as they are executed.

# Set default installation directory if GRPC_INSTALL_DIR is not set
GRPC_INSTALL_DIR=${GRPC_INSTALL_DIR:-$HOME/.local}

# Install prerequisites
sudo apt-get update
sudo apt-get install -y build-essential autoconf libtool pkg-config cmake

# Clone the gRPC repository
git clone --recurse-submodules -b v1.56.0 https://github.com/grpc/grpc

# Navigate to the gRPC directory
cd grpc

# Create a build directory
mkdir -p cmake/build
cd cmake/build

# Run CMake to configure the build
cmake -DgRPC_INSTALL=ON \
      -DgRPC_BUILD_TESTS=OFF \
      -DCMAKE_CXX_STANDARD=17 \
      -DCMAKE_INSTALL_PREFIX=$GRPC_INSTALL_DIR \
      ../..

# Build gRPC and its dependencies
make -j$(nproc)

# Install gRPC and its dependencies
sudo make install

# Add gRPC to PATH
export PATH="$GRPC_INSTALL_DIR/bin:$PATH"

sudo apt-get update
sudo apt-get install -y libcurl4-openssl-dev
sudo apt-get install -y libyaml-cpp-dev
sudo apt-get install -y nlohmann-json3-dev