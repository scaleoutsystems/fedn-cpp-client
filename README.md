# fedn-cpp

## Build from source
For grpc: 

sudo apt install -y build-essential autoconf libtool pkg-config



mkdir -p cmake/build
pushd cmake/build
cmake ../..
make -j 4

