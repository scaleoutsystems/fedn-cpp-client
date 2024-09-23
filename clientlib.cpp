//  - Client

// Imports - HttpClient
#include <iostream>
#include <string>
#include <stdlib.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

using json = nlohmann::json;

// Imports - GrpcClient
#include <sstream>
#include <memory>
#include <vector>
#include <random>
#include <iomanip>
#include <fstream>
#include <chrono>
#include <thread>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"

#include <grpcpp/grpcpp.h>

#include "fedn.grpc.pb.h"
#include "fedn.pb.h"

// Imports - CppClient
#include "clientlib.h"

// Include Armadillo for matrix operations
// #include <mlpack.hpp>

ABSL_FLAG(std::string, name, "test", "Name of client, (OBS! Must be same as used in http-client)");
ABSL_FLAG(std::string, id, "test123", "ID of client");
ABSL_FLAG(std::string, server_host, "localhost:12080", "Server host");
ABSL_FLAG(std::string, proxy_host, "", "Proxy host");
ABSL_FLAG(std::string, token, "", "Token for authentication");
ABSL_FLAG(std::string, auth_scheme, "Bearer", "Authentication scheme");
ABSL_FLAG(bool, insecure, false, "Use an insecure grpc channel");
ABSL_FLAG(std::string, in, "../../client.yaml", "Client configuration file");

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

// Code - HttpClient
// Callback function to write HTTP response data into a string
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t totalSize = size * nmemb;
    output->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

HttpClient::HttpClient(const std::string& apiUrl, const std::string& token = "") : apiUrl(apiUrl), token(token) {
    curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Error initializing libcurl." << std::endl;
        std::exit(1);
    }
}

HttpClient::~HttpClient() {
    if (curl) {
        curl_easy_cleanup(curl);
    }
}

json HttpClient::assign(const json& requestData) {
    // Convert the JSON data to a string
    std::string jsonData = requestData.dump();

    // add endpoint /add_client to the apiUrl
    apiUrl += "/add_client";

    // Set libcurl options for the POST request
    curl_easy_setopt(curl, CURLOPT_URL, apiUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonData.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    // allow all redirects
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    // Set the Content-Type header
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    // Get Environment variable for token scheme
    char* token_scheme;
    token_scheme = std::getenv("FEDN_AUTH_SCHEME");
    if (token_scheme == NULL) {
        token_scheme = (char*) "Bearer";
    }
    std::string fillString = token_scheme + (std::string) " ";

    // Set the token as a header if it's provided
    if (!token.empty()) {
        headers = curl_slist_append(headers, ("Authorization: " + fillString + token).c_str());
    }

    // Set the headers for the POST request
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // Response data will be stored here
    std::string responseData;

    // Set the response data string as the write callback parameter
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseData);

    // Perform the HTTP POST request
    CURLcode res = curl_easy_perform(curl);

    // Get status code
    long statusCode;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);

    // Check for HTTP errors
    if (statusCode < 200 || statusCode >= 300){
        std::cerr << "HTTP error: " << statusCode << std::endl;
        return json(); // Return an empty JSON object in case of an error
    }

    // Check for errors
    if (res != CURLE_OK) {
        std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        return json(); // Return an empty JSON object in case of an error
    }

    // Parse and return the response JSON
    try {
        if (!responseData.empty() && responseData[0] == '{') {
            json responseJson = json::parse(responseData);
            return responseJson;
        } else {
            std::cerr << "Invalid or empty response data." << std::endl;
            // Print the response data
            std::cout << responseData << std::endl;
            return json(); // Return an empty JSON object in case of invalid or empty response data
        }
    } catch (const std::exception& e) {
        std::cerr << "Error parsing response JSON: " << e.what() << std::endl;
        return json(); // Return an empty JSON object in case of parsing error
    }
}

std::string HttpClient::getToken() {
    return token;
}

json validateHttpRequestData(YAML::Node config) {
    // Read requestData from the config
    std::cout << "Reading request data from config" << std::endl;
    json requestData;
    requestData["client_id"] = config["client_id"].as<std::string>();
    requestData["name"] = config["name"].as<std::string>();
    requestData["package"] = "remote";

    // Check if the client_id is valid
    if (!requestData["client_id"].is_string()) {
        throw std::runtime_error("Invalid client_id.");
    }
    
    // Check if preferred_combiner is in the config, else use default empty string
    if (config["preferred_combiner"]) {
        requestData["preferred_combiner"] = config["preferred_combiner"].as<std::string>();
        // Check if the preferred_combiner is valid
        if (!requestData["preferred_combiner"].is_string()) {
            throw std::runtime_error("Invalid preferred_combiner.");
        }
    } else {
        requestData["preferred_combiner"] = "";
    }

    return requestData;
}

std::string readApiUrl(YAML::Node config) {
    // Read API endpoint URL from the config
    const std::string apiUrl = config["discover_host"].as<std::string>();

    // Check if the API URL is valid
    if (apiUrl.empty()) {
        throw std::runtime_error("Invalid API URL.");
    }

    return apiUrl;
}

std::string readToken(YAML::Node config) {
    // Check if there is a "token" key in the config
    std::string token;
    if (config["token"]) {
        token = config["token"].as<std::string>();
        // Check if the token is valid

        // TODO Is it necessary to check if token is string? Check in the original code!
        if (token.empty()) { // && !config["token"].is_string()) {
            throw std::runtime_error("Invalid token.");
        }
    }

    return token;
}

// Code - GrpcClient
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


GrpcClient::GrpcClient(std::shared_ptr<ChannelInterface> channel)
      : connectorStub_(Connector::NewStub(channel)),
        combinerStub_(Combiner::NewStub(channel)),
        modelserviceStub_(ModelService::NewStub(channel)) {}

  // Assembles the client's payload, sends it and presents the response back
  // from the server.
void GrpcClient::HeartBeat() {
    // Data we are sending to the server.
    Client* client = new Client();
    client->set_name(name_);
    client->set_role(WORKER);
    client->set_client_id(id_);

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

void GrpcClient::ConnectModelUpdateStream() {
    // Data we are sending to the server.
    Client* client = new Client();
    client->set_name(name_);
    client->set_role(WORKER);
    client->set_client_id(id_);

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
        this->UpdateLocalModel(modelUpdate.model_id(), modelUpdate.data());
      }
      else if (modelUpdate.type() == StatusType::MODEL_VALIDATION) {
        // TODO: Implement model validation
        std::cout << "Model validation not implemented, skipping..." << std::endl;
      }
      else if (modelUpdate.type() == StatusType::INFERENCE) {
        // TODO: Implement model inference
        std::cout << "Model inference not implemented, skipping..." << std::endl;
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
std::string GrpcClient::DownloadModel(const std::string& modelID) {

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
   * Upload local model to server in chunks.
   * 
   * @param modelID The model ID to upload.
   * @param modelData The model data to upload.
   * @param chunkSize The size of each chunk to upload.
   */
void GrpcClient::UploadModel(std::string& modelID, std::string& modelData, size_t chunkSize) {
      // response 
      ModelResponse response;
      // context
      ClientContext context;

      // Client
      Client* client = new Client();
      client->set_name(name_);
      client->set_role(WORKER);
      client->set_client_id(id_);

      // Get ClientWriter from stream
      std::unique_ptr<ClientWriter<ModelRequest> > writer(
          modelserviceStub_->Upload(&context, &response));

      // Calculate the number of chunks
      size_t totalSize = modelData.size();
      size_t offset = 0;

      std::cout << "Upload in progress: " << modelID << std::endl;

      while (offset < totalSize) {
          ModelRequest request;
          size_t currentChunkSize = std::min(chunkSize, totalSize - offset);
          request.set_data(modelData.data() + offset, currentChunkSize);
          request.set_id(modelID);
          request.set_status(ModelStatus::IN_PROGRESS);
          // Pass ownership of client to protobuf message only for the first chunk
          if (offset == 0) {
              request.set_allocated_sender(client);
          }

          if (!writer->Write(request)) {
              // Broken stream.
              std::cout << "Upload failed for model: " << modelID << std::endl;
              std::cout << "Disconnecting from UploadStream" << std::endl;
              grpc::Status status = writer->Finish();
              return;
          }
          std::cout << "Uploading chunk: " << offset << " - " << offset + currentChunkSize << std::endl;
          offset += currentChunkSize;
      }

      // Finish writing to stream with final message
      ModelRequest requestFinal;
      requestFinal.set_id(modelID);
      requestFinal.set_status(ModelStatus::OK);
      writer->Write(requestFinal);
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
  // * Save model to file.
  // *
  // * @param modelData The model data to save.
  // * @param modelID The model ID to save. Will be used as filename.
  // * @param path The path to save the model to.
void GrpcClient::SaveModelToFile(const std::string& modelData, const std::string& modelID, const std::string& path) {
    // Write the binary string to a file
    // Create an ofstream object and open the file in binary mode
    std::ofstream outFile(path + modelID + ".bin", std::ios::binary);

    // Check if the file was opened successfully
    if (!outFile) {
        std::cerr << "Error opening file for writing" << std::endl;
    }

    // Write the binary string to the file
    outFile.write(modelData.c_str(), modelData.size());

    // Close the file
    outFile.close();

    std::cout << "modelData saved to file successfully" << std::endl;
  }
  //**
  // * Load model from file.
  // *
  // * @param modelID The model ID to load. Will be used as filename.
  // * @param path The path to load the model from.
  // * @return String containing binary data of the model.
std::string GrpcClient::LoadModelFromFile(const std::string& modelID, const std::string& path) {
    // Create an ifstream object and open the file in binary mode
    std::ifstream inFile(path + modelID + ".bin", std::ios::binary);
    // Check if the file was opened successfully
    if (!inFile) {
        std::cerr << "Error opening file for reading" << std::endl;
    }
    // Get the length of the file
    inFile.seekg(0, inFile.end);
    int length = inFile.tellg();
    inFile.seekg(0, inFile.beg);
    // Create a string object to hold the data
    std::string data(length, '\0');
    // Read the file
    inFile.read(&data[0], length);
    // Close the file
    inFile.close();
    // Return the data
    return data;
  }
  //
  //**
  // * Delete model from disk.
  // *
  // * @param modelID The model ID to delete.
  // * @param path The path to delete the model from.
void GrpcClient::DeleteModelFromDisk(const std::string& modelID, const std::string& path) {
    // Delete the file
    if (remove((path + modelID + ".bin").c_str()) != 0) {
        std::cerr << "Error deleting file" << std::endl;
    }
    else {
        std::cout << "File deleted successfully: " << modelID << std::endl;
    }
  }
  //**
  // * Update local model with global model as "seed" model.
  // * This is where ML code should be executed.
  // *
  // * @param modelID The model ID (Global) to update local model with.
  // * @param requestData The data field from ModelUpdate request, should contain the round config
void GrpcClient::UpdateLocalModel(const std::string& modelID, const std::string& requestData) {
    std::cout << "Updating local model: " << modelID << std::endl;
    // Download model from server
    std::cout << "Downloading model: " << modelID << std::endl;
    std::string modelData = GrpcClient::DownloadModel(modelID);
    // Save model to file
    // TODO: model should be saved to a temporary file and chunks should be written to it
    std::cout << "Saving model to file: " << modelID << std::endl;
    GrpcClient::SaveModelToFile(modelData, modelID, "./");
    // Dummy code: update model with modelData
    std::cout << "Dummy code: Updating local model with global model as seed!" << std::endl;
    // Example of loading a matrix from a binary file using Armadillo
    // arma::mat loadedData;
    // loadedData.load("./" + modelID + ".bin", arma::raw_binary);
    // std::cout << "Loaded data: " << loadedData << std::endl;
    // Update the matrix (model)
    // loadedData += 1;
    // Create random UUID4 for model update
    std::cout << "Generating random UUID for model update" << std::endl;
    std::string modelUpdateID = GrpcClient::generateRandomUUID();
    // Save the matrix as raw binary file
    // Using Armadillo to save the matrix as a binary file
    // loadedData.save("./" + modelUpdateID + ".bin", arma::raw_binary);
    // Using own code to save the matrix as a binary file, dymmy code. Remove if using Armadillo
    GrpcClient::SaveModelToFile(modelData, modelUpdateID, "./");
    // Read the binary string from the file
    // Load model from file
    std::cout << "Loading model from file: " << modelUpdateID << std::endl;
    std::string data = GrpcClient::LoadModelFromFile(modelUpdateID, "./");

    // Upload model to server
    GrpcClient::UploadModel(modelUpdateID, data, GrpcClient::chunkSize);
    // Send model update response to server
    GrpcClient::SendModelUpdate(modelID, modelUpdateID, requestData);
    // Delete model from disk
    GrpcClient::DeleteModelFromDisk(modelID, "./");
    GrpcClient::DeleteModelFromDisk(modelUpdateID, "./");
  }
  //**
  // * Send model update message to server.
  // *
  // * @param modelID The model ID (Global) to send model update for.
  // * @param modelUpdateID The model update ID (Local) to send model update for.
  // * @param config The round config
void GrpcClient::SendModelUpdate(const std::string& modelID, std::string& modelUpdateID, const std::string& config) {
   // Send model update response to server
    Client client;
    client.set_name(name_);
    client.set_role(WORKER);
    client.set_client_id(id_);

    
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
std::string GrpcClient::generateRandomUUID() {
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

void GrpcClient::SetName(const std::string& name) {
    name_ = name;
}

void GrpcClient::SetId(const std::string& id) {
    id_ = id;
}

void SendIntervalHeartBeat(GrpcClient* client, int intervalSeconds) {
  while (true) {
      client->HeartBeat();
      std::this_thread::sleep_for(std::chrono::seconds(intervalSeconds));
  }
}

// Code - Client
CppClient::CppClient(int argc, char** argv) {
    // Parse command line arguments
    readCommandLineArguments(argc, argv);

    // Read HTTP configuration from the "client.yaml" file
    const std::string configFile = commandLineArguments["in"];
    YAML::Node config = YAML::LoadFile(configFile);
    // printYAML(config);
    httpRequestData = validateHttpRequestData(config);

    // Read API endpoint URL from the config
    const std::string apiUrl = "https://" + readApiUrl(config);
    
    // Check if there is a "token" key in the config
    std::string token = readToken(config);

    // Create a Client instance with the API URL and token (if provided)
    httpClient = std::make_unique<HttpClient>(apiUrl, token);

    // Assign
    json grpcChannelConfig = assign();

    // Create gRPC channel
    createChannel(grpcChannelConfig);
}

std::shared_ptr<ChannelInterface> CppClient::getChannel() {
    return channel;
}

void CppClient::run(std::shared_ptr<GrpcClient> customGrpcClient) {
    // Add the custom gRPC client to the Client object
    grpcClient = customGrpcClient;

    // Set name, id and chunk size
    grpcClient->SetName(httpRequestData["name"]);
    grpcClient->SetId(httpRequestData["client_id"]);

    // Start heart beat thread and listen to model update requests
    std::thread HeartBeatThread(SendIntervalHeartBeat, grpcClient.get(), 10);
    grpcClient->ConnectModelUpdateStream();
    HeartBeatThread.join();
}

json CppClient::assign() {
    // Request assignment to combiner
    json httpResponseData = httpClient->assign(httpRequestData);

    // Pretty print of the response
    std::cout << "Response: " << httpResponseData.dump(4) << std::endl;

    // Setup gRPC channel configuration
    json grpcChannelConfig;
    grpcChannelConfig["insecure"] = commandLineArguments["insecure"];
    grpcChannelConfig["host"] = httpResponseData["host"];
    grpcChannelConfig["proxy_host"] = httpResponseData["fqdn"];
    grpcChannelConfig["token"] = httpClient->getToken();
    grpcChannelConfig["auth_scheme"] = commandLineArguments["auth_scheme"];
    return grpcChannelConfig;
}

void CppClient::createChannel(json grpcChannelConfig) {
    // Instantiate the client. It requires a channel, out of which the actual RPCs
    // are created. This channel models a connection to an server specified by
    // the argument "--target=".

    std::cout << "Server host: " << grpcChannelConfig["host"] << std::endl;

    // initialize credentials
    std::shared_ptr<grpc::ChannelCredentials> creds;
    if (grpcChannelConfig["insecure"]) {
    std::cout << "Using insecure channel" << std::endl;
    // Create an call credentials object for use with an insecure channel
    creds = grpc::InsecureChannelCredentials();
    } else {
    std::cout << "Using secure channel" << std::endl;
    // check if token is empty
    if (grpcChannelConfig["token"].empty()) {
        throw std::runtime_error("Token is empty, exiting...");
    }
    // check if auth_scheme is empty
    if (grpcChannelConfig["auth_scheme"].empty()) {
        throw std::runtime_error("Auth scheme is empty, exiting...");
    }
    // Check if auth_scheme is Token or Bearer
    if (grpcChannelConfig["auth_scheme"] != "Token" && grpcChannelConfig["auth_scheme"] != "Bearer") {
        throw std::runtime_error("Invalid auth scheme, exiting...");
    }
    // Create authorization header value string for metadata
    std::string header_value = grpcChannelConfig["auth_scheme"].get<std::string>() + (std::string) " " + grpcChannelConfig["token"].get<std::string>();

    // Create call credentials
    auto call_creds = grpc::MetadataCredentialsFromPlugin(
    std::unique_ptr<grpc::MetadataCredentialsPlugin>(
        new MyCustomAuthenticator(header_value)));

    // Create channel credentials
    auto channel_creds = grpc::SslCredentials(grpc::SslCredentialsOptions());

    // Get the server host to metadata
    auto metadata_creds = grpc::MetadataCredentialsFromPlugin(
    std::unique_ptr<grpc::MetadataCredentialsPlugin>(
        new CustomMetadata("grpc-server", grpcChannelConfig["host"])));

    // Create intermediate composite channel credentials
    auto inter_creds = grpc::CompositeChannelCredentials(channel_creds, call_creds);
    // Create composite channel credentials
    creds = grpc::CompositeChannelCredentials(inter_creds, metadata_creds);
    }
    // Check if proxy host is set, and change host to proxy host, server host will be in metadata
    if (!grpcChannelConfig["proxy_host"].empty()) {
    std::cout << "Proxy host: " << grpcChannelConfig["proxy_host"] << std::endl;
    grpcChannelConfig["host"] = grpcChannelConfig["proxy_host"];
    }
    //Create a channel using the credentials created above
    grpc::ChannelArguments args;

    // Sample way of setting keepalive arguments on the client channel. Here we
    // are configuring a keepalive time period of 20 seconds, with a timeout of 10
    // seconds. Additionally, pings will be sent even if there are no calls in
    // flight on an active connection.
    args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, 20 * 1000 /*20 sec*/);
    args.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 10 * 1000 /*10 sec*/);
    args.SetInt(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);

    channel = grpc::CreateCustomChannel(grpcChannelConfig["host"], creds, args);
}

void CppClient::readCommandLineArguments(int argc, char** argv) {
    absl::ParseCommandLine(argc, argv);
    commandLineArguments["name"] = absl::GetFlag(FLAGS_name);
    commandLineArguments["id"] = absl::GetFlag(FLAGS_id);
    commandLineArguments["server_host"] = absl::GetFlag(FLAGS_server_host);
    commandLineArguments["proxy_host"] = absl::GetFlag(FLAGS_proxy_host);
    commandLineArguments["token"] = absl::GetFlag(FLAGS_token);
    commandLineArguments["auth_scheme"] = absl::GetFlag(FLAGS_auth_scheme);
    commandLineArguments["insecure"] = absl::GetFlag(FLAGS_insecure);
    commandLineArguments["in"] = absl::GetFlag(FLAGS_in);
}