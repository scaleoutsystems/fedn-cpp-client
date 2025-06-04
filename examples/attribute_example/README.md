# fedn cpp client for attribute example
A FEDn client used for showcasing attributes, metrics and telemetry for the C++ client. 

**Note:** This is a prototype and is still in active development, so the interface may change frequently. The purpose of this prototype is to demonstrate a full client implementation that allows clients written in different languages to jointly train a global model.

## fednlib API
To create a FEDn client in C++, the user creates a C++ source file where they implement their machine learning code and use the FEDn library API `fednlib` to connect the client to the federated network. The `examples` folder contains code that showcases how to use the `fednlib` API to connect a client to a combiner and process task requests such as training and validation. 

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

### Build the C++ client
Before building the client executable, start by building the `fednlib` library.

#### Build the FEDn library *fednlib*
Run the following commands to build the library `fednlib`. Standing in the project root directory `fedn-cpp-client`:
    
    cmake -S . -B build
    cmake --build build


#### Build the client executable
Now that the libraries are installed, we can build the client executable. Here we show how to build the example client `attribute-example`. 

    cmake -S . -B build
    cmake --build build

After the last command you should have `attribute-example` executable ready to use. 

## Connecting client to the server

## Fedn Studio
Now lets connect your client to your Studio project, update the `client.yaml` file located in the base directory of `fedn-cpp-client-cnpy` with the following content: 

```yaml
discover_host: api.fedn.scaleoutsystems.com/test-sad-reducer
name: mnist-client-cpp
token: <project-based-client-token>
client_id: <unique-client-id>
package: local
```
Replace the values based on your Studio's project settings. You can find this information in the `Connect Client` section under the `Clients` tab.

## Seed model
The seed model needs to be uploaded to the Studio before starting the training. This project only mocks the training so any seed model suffices and can be grabed from other. 

## Start the C++ client
Standing in `attribute-example/build`, run the following command:

    ./attribute-example

**Note:** You can change the name of the client executable in the `CMakeLists.txt` file for your client.

This message should now be printed to the terminal once every few seconds:

    Response: Heartbeat received





