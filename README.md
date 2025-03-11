# fedn-cpp-client
A FEDn client in C++.

**Note:** This is a prototype and is still in active development so the interface might change frequently! The purpose of this prototype is to demonstrate how the C++ client can be used to connect to the federated network. In the example below, there is no actual model training being performed on the client.

## fednlib API
To create a FEDn client in C++, the user creates a C++ source file where they implement their machine learning code and use the FEDn library API `fednlib` to connect the client to the federated network. The `examples` folder contains code that showcases how to use the `fednlib` API to connect a client to a combiner and process task requests such as training and validation. The example `my-client` is a minimal example that shows how the user implements their own machine learning code by overriding the methods `train`and `validate` of the `GrpcClient` class, and connect the client to the network in the `main` function. The `mnist-libtorch` example trains a simple neural network with libtorch (C++ interface to PyTorch) to solve the MNIST classification problem and gives a more concrete example of how to implement C++ machine learning code with FEDn.

* `train`: The user starts by reading the model from a binary file into the preferred format (depending on the ML library that is used), implements the machine learning logic, and saves the updated model back to a file in binary format. In the example `my_client.cpp`, this function simply reads the global model into memory and writes it back to file.
* `validate`: The user starts by reading the model from a binary file, computes the preferred validation metrics, saves the metrics in a JSON object and writes the JSON to file. In the example `my_client.cpp`, this function creates a JSON with mock validation data and writes it to file.
* `predict`: The user starts by reading the model from a binary file, makes predictions, saves the prediction data in a JSON object and writes the JSON to file. In the example `my_client.cpp`, this function creates a JSON with mock prediciton data and writes it to file.
* `main`: The user starts by creating an object of the class `FednClient`, passing the client configuration file path to the constructor. Then the user gets the combiner configuration from the `FednClient` object and uses it to setup a gRPC channel. Then the user creates an object of the custom class which inherits from `GrpcCient` and overrides the functions `train` and `validate` as described above, passing the gRPC channel to the constructor. Finally the user invokes the `run` method on the `FednClient` object to connect the client to the task stream from the assigned combiner.

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

**Note:** If you are running on Mac, you might need to uncomment these lines in `CMakeLists.txt` and replace `<path-to-yaml-cpp>` with the path where `yaml-cpp` is located on your machine before building:

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

#### Build the client executable
Now that the library is built, we can build the client executable. Here we show how to build the example client `my-client`, but the process is analogous for any FEDn C++ client file. Standing in the `my-client` folder:

    mkdir -p build
    cd build
    cmake ..
    make -j 4

## Connecting client to the server

## Fedn Studio
If you want to connect your client to your Studio project, update the `client.yaml` file located in the base directory of `fedn-cpp-client` with the following content: 

```yaml
discover_host: api.fedn.scaleoutsystems.com/test-sad-reducer
name: client-cpp
token: eNiIsInR5cCI6IkpXVCJ9eyJ0b2tlbl90eXBlIjoiYWNjZXNzIiwiZXhwIjoxNzQ0MTcpRNvxypK9T8fOTVRdmkfcKYtlHG9U95AyzJe7O1Pshs 
client_id: c2580eca-6a75-49f8-a0a5-4bd97bfa4a37
package: local
```
Replace the values based on your Studio's project settings. You can find this information in the `Connect Client` section under the `Clients` tab.

### Local Servers
**Note:** This section is only necessary if you are deploying FEDn locally. Skip this section if you are using Studio.

This is a prototype and certain configurations (such as client name and combiner assignment) has been hardcoded for the pseudo-distributed docker compose setup in FEDn. We will be using a compute package written in python, there will not be any ML updates to models on the C++ client, instead it will just reupload the same global model weights. Validation is also not included (not implemented yet).

First deploy e.g FEDn mnist-keras example: https://github.com/scaleoutsystems/fedn/tree/master/examples/mnist-keras

    git clone https://github.com/scaleoutsystems/fedn.git
    git checkout feature/cpp
    cd fedn/examples/mnist-pytorch

start services:

    docker-compose -f ../../docker-compose.yaml up -d --build minio mongo mongo-express api-server combiner

It will take some time to build the images, go grab a coffee!!

Next, follow the instructions in the README inside fedn/examples/mnist-keras for section "Preparing the environment, the local data, the compute package and seed model". Once you have the package and the seed model you can either upload these via using the [APIClient](https://fedn.readthedocs.io/en/stable/fedn.network.api.html#fedn.network.api.client.APIClient) (obs that you need to install the FEDn API to your local environment, for example using a virtual python environment).

## Start the C++ client
Standing in `my-client/build`, run the following command:

    ./my-client

**Note:** You can change the name of the client executable in the `CMakeLists.txt` file for your client.

This message should now be printed to the terminal once every few seconds:

    Response: Heartbeat received

The client is then waiting for model update requests from the combiner. **Start** a training session either via the Studio dashboard or the Python APIClient. The expected output should look similar to this:

    TaskRequest ModelID: 7f37dd0d-e067-4ebc-9707-21ed72b41077
    TaskRequest: TaskType:2
    Updating local model: 7f37dd0d-e067-4ebc-9707-21ed72b41077
    Downloading model: 7f37dd0d-e067-4ebc-9707-21ed72b41077
    ModelResponseID: 7f37dd0d-e067-4ebc-9707-21ed72b41077
    ModelResponseStatus: 1
    Download in progress: 7f37dd0d-e067-4ebc-9707-21ed72b41077
    Downloaded size: 193561 bytes
    ModelResponseID: 7f37dd0d-e067-4ebc-9707-21ed72b41077
    ModelResponseStatus: 0
    Download complete for model: 7f37dd0d-e067-4ebc-9707-21ed72b41077
    Downloaded size: 193561 bytes
    Disconnecting from DownloadStream
    Saving model to file: 7f37dd0d-e067-4ebc-9707-21ed72b41077
    modelData saved to file ./7f37dd0d-e067-4ebc-9707-21ed72b41077.bin successfully

You can now check the model trail through the Python APIClient, the length of the list should be greater than 1. If you are using Studio, you can also see the model updates in the dashboard.

## Tear down
Close C++ client with Ctrl+C

Stop local services (This step is not applicable if you are using FEDn Studio.):

    cd fedn/examples/mnist-pytorch
    docker-compose -f ../../docker-compose.yaml -f docker-compose.override.yaml down -v



