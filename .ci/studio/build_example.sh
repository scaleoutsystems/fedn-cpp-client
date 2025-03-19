# build fednlib
mkdir -p build
pushd build
cmake ..
make -j$(nproc)
popd

# Build example client
pushd examples/$FEDN_EXAMPLE
mkdir -p build
cd build
cmake ..
make -j$(nproc)
popd