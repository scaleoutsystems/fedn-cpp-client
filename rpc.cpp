#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"

#include <grpcpp/grpcpp.h>

#include "fedn.grpc.pb.h"
#include "fedn.pb.h"


ABSL_FLAG(std::string, target, "localhost:12080", "Server address");

using grpc::ChannelInterface;
using grpc::ClientContext;
using grpc::Status;
using grpc::ClientReader;
using fedn::Connector;
using fedn::Combiner;
using fedn::ModelService;
using fedn::Heartbeat;
using fedn::ModelUpdate;
using fedn::ModelUpdateRequest;
using fedn::ModelResponse;
using fedn::ModelRequest;
using fedn::ModelStatus;
using fedn::Client;
using fedn::ClientAvailableMessage;
using fedn::WORKER;
using fedn::Response;

class GreeterClient {
 public:
  GreeterClient(std::shared_ptr<ChannelInterface> channel)
      : connectorStub_(Connector::NewStub(channel)),
        combinerStub_(Combiner::NewStub(channel)),
        modelserviceStub_(ModelService::NewStub(channel)) {}

  // Assembles the client's payload, sends it and presents the response back
  // from the server.
  void SayHello() {
    // Data we are sending to the server.
    Client client;
    client.set_name("test");
    client.set_role(WORKER);

    Heartbeat request;
    request.set_allocated_sender(&client);

    // Container for the data we expect from the server.
    Response reply;

    // Context for the client. It could be used to convey extra information to
    // the server and/or tweak certain RPC behaviors.
    ClientContext context;

    // The actual RPC.
    Status status = connectorStub_ ->SendHeartbeat(&context, request, &reply);

    // Print response attribute from fedn::Response
    std::cout << "Response: " << reply.response() << std::endl;

    // Garbage collect the request object.
    request.release_sender();
  
    // Act upon its status.
    if (!status.ok()) {
      // Print response attribute from fedn::Response
      std::cout << "HeartbeatResponse: " << reply.response() << std::endl;
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
    }
  }

  void ConnectModelUpdateStream() {
    // Data we are sending to the server.
    Client client;
    client.set_name("test");
    client.set_role(WORKER);

    ClientAvailableMessage request;

    request.set_allocated_sender(&client);

    ClientContext context;

    // Get ClientReader from stream
    std::unique_ptr<ClientReader<ModelUpdateRequest> > reader(
        combinerStub_->ModelUpdateRequestStream(&context, request));

    // Read from stream
    ModelUpdateRequest modelUpdate;
    while (reader->Read(&modelUpdate)) {
      std::cout << "ModelUpdateRequest: " << modelUpdate.model_id() << std::endl;
      GreeterClient::UpdateLocalModel(modelUpdate.model_id());
      
    }
    reader->Finish();
    std::cout << "Disconnecting from ModelUpdateRequestStream" << std::endl;
  }
  std::vector<char> DownloadModel(const std::string& modelID) {

    // request 
    ModelRequest request;
    request.set_id(modelID);

    // context
    ClientContext context;

    // Get ClientReader from stream
    std::unique_ptr<ClientReader<ModelResponse> > reader(
        modelserviceStub_->Download(&context, request));

    // Collection for data
    std::vector<char> accumulatedData;

    // Read from stream
    ModelResponse modelResponse;
    while (reader->Read(&modelResponse)) {
      std::cout << "ModelResponseID: " << modelResponse.id() << std::endl;
      std::cout << "ModelResponseStatus: " << modelResponse.status() << std::endl;
      if (modelResponse.status() == ModelStatus::IN_PROGRESS) {
        const std::string& dataResponse = modelResponse.data();
        accumulatedData.insert(accumulatedData.end(), dataResponse.begin(), dataResponse.end());
        std::cout << "Download in progress: " << modelResponse.id() << std::endl;
        std::cout << "Downloaded size: " << accumulatedData.size() << " bytes" << std::endl;

      } else if (modelResponse.status() == ModelStatus::OK) {
        // Print download complete
        std::cout << "Download complete for model: " << modelResponse.id() << std::endl;
      
      }
      else if (modelResponse.status() == ModelStatus::FAILED) {
        // Print download failed
        std::cout << "Download failed: internal server error" << std::endl;
      
      }

    }
    reader->Finish();
    std::cout << "Downloaded size: " << accumulatedData.size() << " bytes" << std::endl;
    std::cout << "Disconnecting from DownloadStream" << std::endl;
    
    // return accumulatedData
    return accumulatedData;
    }

  void SendModelUpdateResponse(ModelUpdate modelUpdate) {
    
  }

  void UpdateLocalModel(const std::string& modelID) {
    std::cout << "Updating local model: " << modelID << std::endl;
    // Download model from server
    std::vector<char> modelData = GreeterClient::DownloadModel(modelID);
    modelData.shrink_to_fit(); // Deallocates excess memory
  }

 private:
  std::unique_ptr<Connector::Stub> connectorStub_;
  std::unique_ptr<Combiner::Stub> combinerStub_;
  std::unique_ptr<ModelService::Stub> modelserviceStub_;
};

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  // Instantiate the client. It requires a channel, out of which the actual RPCs
  // are created. This channel models a connection to an endpoint specified by
  // the argument "--target=" which is the only expected argument.
  std::string target_str = absl::GetFlag(FLAGS_target);
  // We indicate that the channel isn't authenticated (use of
  // InsecureChannelCredentials()).
  GreeterClient greeter(
      grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));
  greeter.SayHello();
  greeter.ConnectModelUpdateStream();
  return 0;
}