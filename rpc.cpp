#include <iostream>
#include <sstream>
#include <memory>
#include <string>
#include <vector>
#include <random>
#include <iomanip>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"

#include <grpcpp/grpcpp.h>

#include "fedn.grpc.pb.h"
#include "fedn.pb.h"

ABSL_FLAG(std::string, name, "test", "Name of client, (OBS! Must be same as used in http-client)");
ABSL_FLAG(std::string, server_host, "localhost:12080", "Server host");
ABSL_FLAG(std::string, proxy_host, "", "Proxy host");
ABSL_FLAG(std::string, token, "", "Token for authentication");
ABSL_FLAG(std::string, auth_scheme, "Token", "Authentication scheme");
ABSL_FLAG(bool, insecure, false, "Use an insecure grpc channel");

using grpc::ChannelInterface;
using grpc::ClientContext;
using grpc::Status;
using grpc::ClientReader;
using grpc::ClientWriter;
using grpc::ClientReaderWriter;
using fedn::Connector;
using fedn::Combiner;
using fedn::ModelService;
using fedn::Heartbeat;
using fedn::ModelUpdate;
using fedn::TaskRequest;
using fedn::ModelResponse;
using fedn::ModelRequest;
using fedn::ModelStatus;
using fedn::StatusType;
using fedn::Client;
using fedn::ClientAvailableMessage;
using fedn::WORKER;
using fedn::Response;

class MyCustomAuthenticator : public grpc::MetadataCredentialsPlugin {
 public:
  MyCustomAuthenticator(const grpc::string& ticket) : ticket_(ticket) {}

  grpc::Status GetMetadata(
      grpc::string_ref service_url, grpc::string_ref method_name,
      const grpc::AuthContext& channel_auth_context,
      std::multimap<grpc::string, grpc::string>* metadata) override {
    metadata->insert(std::make_pair("authorization", ticket_));
    return grpc::Status::OK;
  }

 private:
  grpc::string ticket_;
};

class CustomMetadata : public grpc::MetadataCredentialsPlugin {
 public:
  CustomMetadata(const grpc::string& key, const grpc::string& value)
      : key_(key), value_(value) {}

  grpc::Status GetMetadata(
      grpc::string_ref service_url, grpc::string_ref method_name,
      const grpc::AuthContext& channel_auth_context,
      std::multimap<grpc::string, grpc::string>* metadata) override {
    metadata->insert(std::make_pair(key_, value_));
    return grpc::Status::OK;
  }

 private:
  grpc::string key_;
  grpc::string value_;
};


class GrpcClient {
 public:
  GrpcClient(std::shared_ptr<ChannelInterface> channel)
      : connectorStub_(Connector::NewStub(channel)),
        combinerStub_(Combiner::NewStub(channel)),
        modelserviceStub_(ModelService::NewStub(channel)) {}

  // Assembles the client's payload, sends it and presents the response back
  // from the server.
  void SayHello() {
    // Data we are sending to the server.
    Client* client = new Client();
    client->set_name(name_);
    client->set_role(WORKER);

    Heartbeat request;
    // Pass ownership of client to protobuf message
    request.set_allocated_sender(client);

    // Container for the data we expect from the server.
    Response reply;

    // Context for the client. It could be used to convey extra information to
    // the server and/or tweak certain RPC behaviors.
    ClientContext context;

    // The actual RPC.
    Status status = connectorStub_ ->SendHeartbeat(&context, request, &reply);

    // Print response attribute from fedn::Response
    std::cout << "Response: " << reply.response() << std::endl;
  
    // Garbage collect the client object.
    //Client* client = request.release_sender();
  
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
    Client* client = new Client();
    client->set_name(name_);
    client->set_role(WORKER);

    ClientAvailableMessage request;
    // Pass ownership of client to protobuf message
    request.set_allocated_sender(client);

    ClientContext context;
    // Add metadata to context
    context.AddMetadata("client", name_);

    // Get ClientReader from stream
    std::unique_ptr<ClientReader<TaskRequest> > reader(
        combinerStub_->TaskStream(&context, request));

    // Read from stream
    TaskRequest modelUpdate;
    while (reader->Read(&modelUpdate)) {
      std::cout << "TaskRequest ModelID: " << modelUpdate.model_id() << std::endl;
      std::cout << "TaskRequest: TaskType:" << modelUpdate.type() << std::endl;
      if (modelUpdate.type() == StatusType::MODEL_UPDATE) {
        GrpcClient::UpdateLocalModel(modelUpdate.model_id(), modelUpdate.data());
      }
      else if (modelUpdate.type() == StatusType::MODEL_VALIDATION) {
        // TODO: Implement model validation
        std::cout << "Model validation not implemented, skipping..." << std::endl;
      }
      
    }
    reader->Finish();
    std::cout << "Disconnecting from TaskStream" << std::endl;
  }
  /**
  * Download global model from server.
  *
  * @return A vector of char containing the model data.
  */
  std::string DownloadModel(const std::string& modelID) {

    // request 
    ModelRequest request;
    request.set_id(modelID);

    // context
    ClientContext context;

    // Get ClientReader from stream
    std::unique_ptr<ClientReader<ModelResponse> > reader(
        modelserviceStub_->Download(&context, request));

    // Collection for data
    std::string accumulatedData;

    // Read from stream
    ModelResponse modelResponse;
    while (reader->Read(&modelResponse)) {
      std::cout << "ModelResponseID: " << modelResponse.id() << std::endl;
      std::cout << "ModelResponseStatus: " << modelResponse.status() << std::endl;
      if (modelResponse.status() == ModelStatus::IN_PROGRESS) {
        const std::string& dataResponse = modelResponse.data();
        accumulatedData += dataResponse;
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

  /**
   * Upload local model to server.
   * 
   * @param modelID The model ID to upload.
   * @param modelData The model data to upload.
   */
  void UploadModel(std::string& modelID, std::string& modelData) {
    // response 
    ModelResponse response;
    // context
    ClientContext context;

    // Client
    Client* client = new Client();
    client->set_name(name_);
    client->set_role(WORKER);

    // Get ClientWriter from stream
    std::unique_ptr<ClientWriter<ModelRequest> > writer(
        modelserviceStub_->Upload(&context, &response));

    ModelRequest request;
    std ::string test = modelData;
    request.set_data(test.data(), test.size());
    request.set_id(modelID);
    request.set_status(ModelStatus::IN_PROGRESS);
    // Pass ownership of client to protobuf message
    request.set_allocated_sender(client);

    if (!writer->Write(request)) {
        // Broken stream.
        std::cout << "Upload failed for model: " << modelID << std::endl;
        std::cout << "Disconnecting from UploadStream" << std::endl;
        grpc::Status status = writer->Finish();
        return;
    }
    test.shrink_to_fit();
    // Finish writing to stream
    //ModelRequest requestFinal;
    request.clear_status();
    request.set_status(ModelStatus::OK);
    // When ModelStatus is OK, the data field should be empty
    request.clear_data();
    writer->Write(request);
    writer->WritesDone();
    grpc::Status status = writer->Finish();

    if (status.ok()) {
      std::cout << "Upload complete for local model: " << modelID << std::endl;
      // Print message from response
      std::cout << "Response: " << response.message() << std::endl;
    } else {
      std::cout << "Upload failed for model: " << modelID << std::endl;
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
      // Print message from response
      std::cout << "Response: " << response.message() << std::endl;
    }
  }
  //**
  // * Update local model with global model as "seed" model.
  // * This is where ML code should be executed.
  // *
  // * @param modelID The model ID (Global) to update local model with.
  // * @param requestData The data field from ModelUpdate request, should contain the round config
  void UpdateLocalModel(const std::string& modelID, const std::string& requestData) {
    std::cout << "Updating local model: " << modelID << std::endl;
    // Download model from server
    std::string modelData = GrpcClient::DownloadModel(modelID);
    // Dummy code: update model with modelData
    std::cout << "Dummy code: Updating local model with global model as seed!" << std::endl;
    // Create random UUID4 for model update
    std::string modelUpdateID = GrpcClient::generateRandomUUID();
    // Upload model to server
    GrpcClient::UploadModel(modelUpdateID, modelData);
    // Send model update response to server
    GrpcClient::SendModelUpdate(modelID, modelUpdateID, requestData);
  }
  //**
  // * Send model update message to server.
  // *
  // * @param modelID The model ID (Global) to send model update for.
  // * @param modelUpdateID The model update ID (Local) to send model update for.
  // * @param config The round config
  void SendModelUpdate(const std::string& modelID, std::string& modelUpdateID, const std::string& config) {
   // Send model update response to server
    Client client;
    client.set_name(name_);
    client.set_role(WORKER);

    
    ModelUpdate modelUpdate;
    
    modelUpdate.set_allocated_sender(&client);
    modelUpdate.set_model_update_id(modelUpdateID);
    modelUpdate.set_model_id(modelID);

    // get current date and time to string
    time_t now = time(0);
    tm *ltm = localtime(&now);
    std::stringstream ss;
    ss << std::put_time(ltm, "%Y-%m-%d %H:%M:%S");
    std::string timeString = ss.str();
    modelUpdate.set_timestamp(timeString);

    // TODO: get metadata from local model
    // string as json
    std::ostringstream oss;
    std::string metadata = "{\"training_metadata\": {\"epochs\": 1, \"batch_size\": 1, \"num_examples\": 3000}}";
    modelUpdate.set_meta(metadata);
    modelUpdate.set_config(config);

    // The actual RPC.
    ClientContext context;
    Response response;
    Status status = combinerStub_->SendModelUpdate(&context, modelUpdate, &response);
    std::cout << "SendModelUpdate: " << modelUpdate.model_id() << std::endl;

    if (!status.ok()) {
      std::cout << "SendModelUpdate: failed for model: " << modelID << std::endl;
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
      std::cout << "SendModelUpdate: Response: " << response.response() << std::endl;
    }
    // Print response attribute from fedn::Response
    std::cout << "SendModelUpdate: Response: " << response.response() << std::endl;
    // Garbage collect the client object.
    Client *clientCollect = modelUpdate.release_sender();
  }
  /**
   * Generate a random UUID version 4 string.
   *
   * @return A random UUID version 4 string.
   */
  std::string generateRandomUUID() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);

    std::stringstream ss;
    for (int i = 0; i < 32; ++i) {
        if (i == 8 || i == 12 || i == 16 || i == 20) {
            ss << '-';
        }
        int randomValue = dis(gen);
        ss << std::hex << randomValue;
    }

    return ss.str();
}

 private:
  std::unique_ptr<Connector::Stub> connectorStub_;
  std::unique_ptr<Combiner::Stub> combinerStub_;
  std::unique_ptr<ModelService::Stub> modelserviceStub_;
  std::string name_ = absl::GetFlag(FLAGS_name);
  static const size_t chunkSize = 1024 * 1024; // 1 MB, change this to suit your needs 
};

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  // Instantiate the client. It requires a channel, out of which the actual RPCs
  // are created. This channel models a connection to an server specified by
  // the argument "--target=".
  std::string host = absl::GetFlag(FLAGS_server_host);
  std::cout << "Server host: " << host << std::endl;

  // Get the insecure flag
  bool insecure = absl::GetFlag(FLAGS_insecure);
  // initialize credentials
  std::shared_ptr<grpc::ChannelCredentials> creds;
  if (insecure) {
    std::cout << "Using insecure channel" << std::endl;
    // Create an call credentials object for use with an insecure channel
    creds = grpc::InsecureChannelCredentials();
  } else {
    std::cout << "Using secure channel" << std::endl;
    // Get the token
    std::string token = absl::GetFlag(FLAGS_token);
    // check if token is empty
    if (token.empty()) {
      std::cerr << "Token is empty, exiting..." << std::endl;
      return 1;
    }
    // Get the auth scheme
    std::string auth_scheme = absl::GetFlag(FLAGS_auth_scheme);
    // check if auth_scheme is empty
    if (auth_scheme.empty()) {
      std::cerr << "Auth scheme is empty, exiting..." << std::endl;
      return 1;
    }
    // Check if auth_scheme is Token or Bearer
    if (auth_scheme != "Token" && auth_scheme != "Bearer") {
      std::cerr << "Invalid auth scheme, exiting..." << std::endl;
      return 1;
    }
    // Create authorization header value string for metadata
    std::string header_value = auth_scheme + (std::string) " " + token;
   
    // Create call credentials
    auto call_creds = grpc::MetadataCredentialsFromPlugin(
    std::unique_ptr<grpc::MetadataCredentialsPlugin>(
        new MyCustomAuthenticator(header_value)));

    // Create channel credentials
    auto channel_creds = grpc::SslCredentials(grpc::SslCredentialsOptions());

    // Get the server host to metadata
    auto metadata_creds = grpc::MetadataCredentialsFromPlugin(
    std::unique_ptr<grpc::MetadataCredentialsPlugin>(
        new CustomMetadata("grpc-server", host)));

    // Create intermediate composite channel credentials
    auto inter_creds = grpc::CompositeChannelCredentials(channel_creds, call_creds);
    // Create composite channel credentials
    creds = grpc::CompositeChannelCredentials(inter_creds, metadata_creds);
  }
  // Check if proxy host is set, and change host to proxy host, server host will be in metadata
  std::string proxy_host = absl::GetFlag(FLAGS_proxy_host);
  if (!proxy_host.empty()) {
    std::cout << "Proxy host: " << proxy_host << std::endl;
    host = proxy_host;
  }
  //Create a channel using the credentials created above
  GrpcClient greeter(
      grpc::CreateChannel(host, creds));
  greeter.SayHello();
  greeter.ConnectModelUpdateStream();
  return 0;
  }