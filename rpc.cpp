#include <iostream>
#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"

#include <grpcpp/grpcpp.h>

#include "fedn.grpc.pb.h"
#include "fedn.pb.h"


ABSL_FLAG(std::string, target, "localhost:12080", "Server address");

using grpc::ChannelInterface;
using grpc::ClientContext;
using grpc::Status;
using fedn::Connector;
using fedn::Combiner;
using fedn::ModelService;
using fedn::Heartbeat;
using fedn::ModelUpdateRequest;
using fedn::Client;
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
      std::cout << "Response: " << reply.response() << std::endl;
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
    }
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
  return 0;
}