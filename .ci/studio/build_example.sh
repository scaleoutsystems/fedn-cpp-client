pushd examples/$FEDN_EXAMPLE
mkdir -p build
cd build
cmake ..
make -j$(nproc)
popd