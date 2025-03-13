# fedn cpp client for MNIST example
A FEDn client 

**Note:** This is a prototype and is still in active development, so the interface may change frequently. The purpose of this prototype is to demonstrate a full client implementation that allows clients written in different languages to jointly train a global model.

## fednlib API
To create a FEDn client in C++, the user creates a C++ source file where they implement their machine learning code and use the FEDn library API `fednlib` to connect the client to the federated network. The `examples` folder contains code that showcases how to use the `fednlib` API to connect a client to a combiner and process task requests such as training and validation. The `mnist-libtorch-stdvec` example trains a simple neural network with libtorch (C++ interface to PyTorch) to solve the MNIST classification problem and gives a more concrete example of how to implement C++ machine learning code with FEDn.

* `train`: The user starts by reading the model from a binary file into the preferred format (depending on the ML library that is used), implements the machine learning logic, and saves the updated model back to a file in binary format. In the example `mnist-libtorch-stdvec.cpp`, this function simply reads the global model into memory, start the training using `libtorch` and writes it back to file.
* `validate`: The user starts by reading the model from a binary file, computes the preferred validation metrics, saves the metrics in a JSON object and writes the JSON to file. In the example `mnist-libtorch-stdvec.cpp`, this function creates a JSON with validation data and writes it to file.
* `main`: The user starts by creating an object of the class `FednClient`, passing the client configuration file path to the constructor. Then the user gets the combiner configuration from the `FednClient` object and uses it to setup a gRPC channel. Then the user creates an object of the custom class which inherits from `GrpcCient` and overrides the functions `train` and `validate` as described above, passing the gRPC channel to the constructor. Finally the user invokes the `run` method on the `FednClient` object to connect the client to the task stream from the assigned combiner.

**NOTE**: This example is designed to work only with the following neural network architecture: 

```c
 Net() {
        fc1 = register_module("fc1", torch::nn::Linear(784, 64));
        fc2 = register_module("fc2", torch::nn::Linear(64, 32));
        fc3 = register_module("fc3", torch::nn::Linear(32, 10));
    }
```

Below are instruction for building the library and client executable from source.

## Build from source
Follow the gRPC C++ quickstart guide to build and locally install gRPC and Protocol Buffers.
Obs that you don't have to follow the helloworld example, but it's a good way to check that the installation was successful:

https://grpc.io/docs/languages/cpp/quickstart/

### Other dependencies

#### On Linux
    sudo apt-get install libcurl4-openssl-dev nlohmann-json3-dev libyaml-cpp-dev

#### On Mac
    brew install curl nlohmann-json yaml-cpp

**Note:** If you are running on Mac, you might need to uncomment these lines in `CMakeLists.txt` and replace paths marked with placeholders such as `<path-to-yaml-cpp>` with the path where the given library (`yaml-cpp` in this case) is located on your machine before building:

    set(YAML_CPP_DIR "<path-to-yaml-cpp>")
    ...
    include_directories(${YAML_CPP_DIR}/include)
    link_directories(${YAML_CPP_DIR}/lib)

**Note:** If the installation was done using the package manager, the default path for `yaml-cpp` on Linux is typically `/usr`.

### Build the C++ client
Before building the client executable, start by building the `fednlib` library.

#### Build the FEDn library *fednlib*
Run the following commands to build the library `fednlib`. Standing in the project root directory `fedn-cpp-client`:
    
    mkdir -p build
    pushd build
    cmake ..
    make -j 4
    popd

#### Needed Extra Libraries

We need to download and install `libtorch`:

Follow the installation instructions on this page:
https://pytorch.org/cppdocs/installing.html

**NOTE**  If you encounter errors during linking, ensure that the correct version `C++11 ABI` is installed and used.

#### Build the client executable
Now that the libraries are installed, we can build the client executable. Here we show how to build the example client `mnist-libtorch-stdvec`. Ensure that all paths specified in the `CMakeLists.txt` file are correct.:

    mkdir -p build
    cd build
    cmake ..
    make -j 4

After the last command you should have `mnist-libtorch-stdvec` executable ready to use. 

## Connecting client to the server

## Fedn Studio
Now lets connect your client to your Studio project, update the `client.yaml` file located in the root directory of `fedn-cpp-client` with the following content: 

```yaml
discover_host: api.fedn.scaleoutsystems.com/test-sad-reducer
name: mnist-client-cpp
token: <project-based-client-token>
client_id: <unique-client-id>
package: local
```
Replace the values based on your Studio's project settings. You can find this information in the `Connect Client` section under the `Clients` tab.

## Dataset for the client
For this demo example, we have created an object-store bucket and placed a small dataset in it. The function `downloadMNISTData` in `mnist-libtorch-stdvec.cpp` contains the implementation. You can modify the function to access additional or different partitions of the dataset. 

## Seed model
The seed model needs to be uploaded to Studio before starting the training. On the start of the C++ client it generates the seed model (`seed.bin`) in the `mnist-libtorch-stdvec` directory. 

## Start the C++ client
Standing in `mnist-libtorch-stdvec/build`, run the following command:

    ./mnist-libtorch-stdvec

**Note:** You can change the name of the client executable in the `CMakeLists.txt` file for your client.

This message should now be printed to the terminal once every few seconds:

    Response: Heartbeat received

The client is then waiting for model update requests from the combiner. **Start** a training session either via the Studio dashboard or the Python APIClient. 

**Note:** When starting a session you need to set the helper type to `"binaryhelper"` to enable aggregation on the server side for models saved in `.bin` format!

The expected output should look similar to this:

    Response: Heartbeat received
    Download in progress: 7bc93a97-0f11-468a-a272-ba0cd96685bf
    Downloaded size: 196957 bytes
    ModelResponseID: 7bc93a97-0f11-468a-a272-ba0cd96685bf
    ModelResponseStatus: 0
    Download complete for model: 7bc93a97-0f11-468a-a272-ba0cd96685bf
    modelData saved to file ./7bc93a97-0f11-468a-a272-ba0cd96685bf.bin successfully
    Downloaded size: 196957 bytes
    Disconnecting from DownloadStream
    Generated random UUID bad50b14-feb5-3859-3d47-be13a39d4c57 for model update
    USER-DEFINED CODE: Training model...
    Loading model parameters from ./7bc93a97-0f11-468a-a272-ba0cd96685bf.bin...
    Model file exist: ./7bc93a97-0f11-468a-a272-ba0cd96685bf.binfound!
    Archive:  ./7bc93a97-0f11-468a-a272-ba0cd96685bf.bin
    inflating: ./7bc93a97-0f11-468a-a272-ba0cd96685bf.bin_extracted/0.npy
    inflating: ./7bc93a97-0f11-468a-a272-ba0cd96685bf.bin_extracted/1.npy
    inflating: ./7bc93a97-0f11-468a-a272-ba0cd96685bf.bin_extracted/2.npy
    inflating: ./7bc93a97-0f11-468a-a272-ba0cd96685bf.bin_extracted/3.npy
    inflating: ./7bc93a97-0f11-468a-a272-ba0cd96685bf.bin_extracted/4.npy
    inflating: ./7bc93a97-0f11-468a-a272-ba0cd96685bf.bin_extracted/5.npy
    Model parameters loaded successfully!
    Cleanup complete: Removed ./7bc93a97-0f11-468a-a272-ba0cd96685bf.bin_extracted
    Train Epoch: 1 [32/60000] Loss: 0.460253
    Train Epoch: 1 [3232/60000] Loss: 0.470339
    Train Epoch: 1 [6432/60000] Loss: 0.38841
    Train Epoch: 1 [9632/60000] Loss: 0.531511
    Train Epoch: 1 [12832/60000] Loss: 0.956416
    Train Epoch: 1 [16032/60000] Loss: 0.584171
    Train Epoch: 1 [19232/60000] Loss: 0.92976
    Train Epoch: 1 [22432/60000] Loss: 0.854746
    Train Epoch: 1 [25632/60000] Loss: 0.42284
    Train Epoch: 1 [28832/60000] Loss: 0.464639

You can now check the model trail on the Studio or through the Python APIClient, the length of the list should be greater than 1. If you are using Studio, you can also see the model updates in the dashboard.



