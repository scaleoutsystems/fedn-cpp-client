# fedn-cpp
A FEDn client in C++.

Obs this is just a prototype and is still very much in development! The perpose of this prototype is to demonstrate the capabilities of the C++ client to connect to the federated network. In the example below, there is no model training being performed on the client. Instead, the global model is downloaded to the client and the same model is sent back to the combiner (demonstrating the connectivity of model updates).

## Build from source
Follow the gRPC C++ quickstart guide to build and locally install gRPC and Protocol Buffers.
Obs that you don't have to follow the helloworld example, but it's a good way to check that the installation was successful:

https://grpc.io/docs/languages/cpp/quickstart/

### Other dependencies
    sudo apt-get install libcurl4-openssl-dev nlohmann-json3-dev libyaml-cpp-dev


### Build the C++ client

    mkdir -p cmake/build
    pushd cmake/build
    cmake ../..
    make -j 4
    popd


## Starting Servers
This is a prototype and certain configurations (such as client name and combiner assignment) has been hardcoded for the pseudo-distributed docker compose setup in FEDn. We will be using a compute package written in python, there will not be any ML updates to models on the C++ client, instead it will just reupload the same global model weights. Validation is also not included (not implemented yet).

First deploy e.g FEDn mnist-keras example: https://github.com/scaleoutsystems/fedn/tree/master/examples/mnist-keras

    git clone https://github.com/scaleoutsystems/fedn.git
    git checkout feature/SK-544
    cd fedn/examples/mnist-keras

start services:

    docker-compose -f ../../docker-compose.yaml up -d --build minio mongo mongo-express api-server dashboard combiner

It will take some time to build the images, go grab a coffee!!

Next, follow the instructions in the README inside fedn/examples/mnist-keras for section "Preparing the environment, the local data, the compute package and seed model". Once you have the package and the seed model you can either upload these via the dashboard (http://localhost:8090) or using the [APIClient](https://fedn.readthedocs.io/en/develop/fedn.network.api.html#fedn.network.api.client.APIClient) (obs that you need to install the FEDn API to your local environment, for example using a virtual python environment).

## Connect C++ client
First ask for combiner assignent via the REST-API using the HTTP client:

    cd cmake
    build/http-client

The response contains combiner config. Obs if the client returns status code 203 it's probably because the compute package has not been set. 
Next, start the gRPC client:

    ./fedn-grpc

The expected output should be:'

    Response: Heartbeat received from client test

The client is then waiting for model update requests from combiner.

Start a training session either via the dashboard or APIClient.

The expected output should look similar to this:

    ModelUpdateRequest: 5a8263bd-e003-4a3e-adcf-0ad532cf831c
    Updating local model: 5a8263bd-e003-4a3e-adcf-0ad532cf831c
    ModelResponseID: 5a8263bd-e003-4a3e-adcf-0ad532cf831c
    ModelResponseStatus: 1
    Download in progress: 5a8263bd-e003-4a3e-adcf-0ad532cf831c
    Downloaded size: 194960 bytes
    ModelResponseID: 5a8263bd-e003-4a3e-adcf-0ad532cf831c
    ModelResponseStatus: 0
    Download complete for model: 5a8263bd-e003-4a3e-adcf-0ad532cf831c
    Downloaded size: 194960 bytes
    Disconnecting from DownloadStream
    Dummy code: Updating local model with global model as seed!
    Upload complete for local model: d8d95f70-407d-193c-cfb5-fdbbbdedc18f
    Response: ModelRequest (Upload): Received final ModeStatus.OK
    SendModelUpdate: 5a8263bd-e003-4a3e-adcf-0ad532cf831c
    SendModelUpdate: Response: SendModelUpdate: ModelUpdate 5a8263bd-e003-4a3e-adcf-0ad532cf831c from client  test

You can now check the model trail, the length of the list should be greater than 1.

## Tear down
Close C++ client with Ctrl+C

Stop services:

    cd fedn/examples/mnist-keras
    docker-compose -f ../../docker-compose.yaml -f docker-compose.override.yaml down -v



