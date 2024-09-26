#include <iostream>
#include <string>
#include <stdlib.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

using json = nlohmann::json;

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
#include "google/protobuf/timestamp.pb.h"

#include "fedn.grpc.pb.h"
#include "fedn.pb.h"

#include "../include/fednlib.h"

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
using fedn::ModelValidation;
using fedn::TaskRequest;
using fedn::ModelResponse;
using fedn::ModelRequest;
using fedn::ModelStatus;
using fedn::StatusType;
using fedn::Client;
using fedn::ClientAvailableMessage;
using fedn::WORKER;
using fedn::Response;

/**
 * Callback for handling data received from a network request.
 * It appends the received HTTP response data to the provided output string.
 * 
 * @param contents Pointer to the data received.
 * @param size Size of each data element.
 * @param nmemb Number of data elements.
 * @param output Pointer to the string where the received data will be appended.
 * @return The total size of the data processed (size * nmemb).
 */
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

json HttpClient::assign(std::map<std::string, std::string> controllerConfig) {
    // Get request body as JSON
    json requestData = {
        {"client_id", controllerConfig["client_id"]},
        {"name", controllerConfig["name"]},
        {"package", controllerConfig["package"]},
        {"preferred_combiner", controllerConfig["preferred_combiner"]}
    };
    
    // Convert the JSON data to a string
    std::string jsonData = requestData.dump();

    // add endpoint /add_client to the apiUrl
    const std::string addClientApiUrl = apiUrl + "/add_client";

    // Set libcurl options for the POST request
    curl_easy_setopt(curl, CURLOPT_URL, addClientApiUrl.c_str());
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
        modelserviceStub_(ModelService::NewStub(channel)) {
            this->SetChunkSize(1024 * 1024);
        }

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
      std::cout << status.error_code() << ": " << status.error_message() << std::endl;
    }
}

void GrpcClient::ConnectTaskStream() {
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
    // TODO: Generalize this to handle different types of TaskRequests, i.e don't call TaskRequest "modelUpdate"
    TaskRequest task;
    while (reader->Read(&task)) {
      std::cout << "TaskRequest ModelID: " << task.model_id() << std::endl;
      std::cout << "TaskRequest: TaskType:" << task.type() << std::endl;
      if (task.type() == StatusType::MODEL_UPDATE) {
        this->UpdateLocalModel(task.model_id(), task.data());
      }
      else if (task.type() == StatusType::MODEL_VALIDATION) {
        // TODO: Implement model validation
        this->ValidateGlobalModel(task.model_id(), task);
      }
      else if (task.type() == StatusType::INFERENCE) {
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
        } 
        else if (modelResponse.status() == ModelStatus::OK) {
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
void GrpcClient::UploadModel(std::string& modelID, std::string& modelData) {
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
    size_t chunkSize = this->GetChunkSize();
    size_t totalSize = modelData.size();
    size_t offset = 0;

    std::cout << "Upload in progress: " << modelID << std::endl;
    std::cout << "Chunk size: " << chunkSize << " bytes" << std::endl;

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
        std::cout << status.error_code() << ": " << status.error_message() << std::endl;
        // Print message from response
        std::cout << "Response: " << response.message() << std::endl;
    }
}
/**
 * Save model to file.
 *
 * @param modelData The model data to save.
 * @param modelID The model ID to save. Will be used as filename.
 * @param path The path to save the model to.
 */
void SaveModelToFile(const std::string& modelData, const std::string& modelPath) {
    // Write the binary string to a file
    // Create an ofstream object and open the file in binary mode
    std::ofstream outFile(modelPath, std::ios::binary);

    // Check if the file was opened successfully
    if (!outFile) {
        std::cerr << "Error opening file for writing" << std::endl;
    }

    // Write the binary string to the file
    outFile.write(modelData.c_str(), modelData.size());

    // Close the file
    outFile.close();

    std::cout << "modelData saved to file " << modelPath << " successfully" << std::endl;
}

/**
 * Load model from file.
 *
 * @param modelID The model ID to load. Will be used as filename.
 * @param path The path to load the model from.
 * @return String containing binary data of the model.
 */
std::string LoadModelFromFile(const std::string& modelPath) {
    // Create an ifstream object and open the file in binary mode
    std::ifstream inFile(modelPath, std::ios::binary);
    // Check if the file was opened successfully
    if (!inFile) {
        std::cerr << "Error opening file " << modelPath << " for reading" << std::endl;
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
/**
 * Load metrics from file.
 * 
 * @param metricPath The path to load the metrics from.
 * @return JSON object containing the metrics.
 */
json LoadMetricsFromFile(const std::string& metricPath) {
    // Create an ifstream object and open the file as json
    std::ifstream inFile(metricPath);
    // Check if the file was opened successfully
    if (!inFile) {
        std::cerr << "Error opening file " << metricPath << " for reading" << std::endl;
    }
    // Create a json object to hold the data
    json metrics;
    try {
        metrics = json::parse(inFile);
    } catch (const nlohmann::detail::type_error e) {
        inFile.close();
        std::cerr << "Error parsing metrics JSON: " << e.what() << std::endl;
    }
    // Close the file
    inFile.close();
    // Return the data
    std::cout << "Metrics loaded from file " << metricPath << " successfully" << std::endl;
    return metrics;
}

/**
 * Save metrics to file.
 * 
 * @param metrics The metrics to save, as a JSON object.
 * @param metricPath The path to save the metrics to.
 */
void SaveMetricsToFile(const json& metrics, const std::string& metricPath) {
    // Create an ofstream object and open the file in binary mode
    std::ofstream outFile(metricPath);
    // Check if the file was opened successfully
    if (!outFile) {
        std::cerr << "Error opening file for writing" << std::endl;
    }
    // Write the json to the file
    outFile << metrics.dump(4);
    // Close the file
    outFile.close();
    std::cout << "Metrics saved to file " << metricPath << " successfully" << std::endl;
}

/**
 * Delete file from disk.
 *
 * @param path The path to delete the model from.
 */
void DeleteFileFromDisk(const std::string& path) {
    // Delete the file
    if (remove((path).c_str()) != 0) {
        std::cerr << "Error deleting file" << std::endl;
    }
    else {
        std::cout << "File deleted successfully: " << path << std::endl;
    }
}
/**
 * This function loads a the current local model from a file, trains the model and saves the model update to file.
 * 
 * @param modelID The ID of the global model to be used as a seed.
 * @param modelUpdateID The ID for the updated local model.
 * @param path The path where the model files are located.
 */
void GrpcClient::Train(const std::string& inModelPath, const std::string& outModelPath) {
    // Using own code to load the matrix from a binary file, dymmy code. Remove if using Armadillo
    std::string modelData = LoadModelFromFile(inModelPath);
    
    // Send the same model back as update
    std::string modelUpdateData = modelData;
    
    // Using own code to save the matrix as a binary file, dymmy code. Remove if using Armadillo
    SaveModelToFile(modelUpdateData, outModelPath);
}

/**
 * Update local model with global model as "seed" model.
 * This is where ML code should be executed.
 *
 * @param modelID The model ID (Global) to update local model with.
 * @param requestData The data field from ModelUpdate request, should contain the round config
 */
void GrpcClient::UpdateLocalModel(const std::string& modelID, const std::string& requestData) {
    std::cout << "Updating local model: " << modelID << std::endl;

    // Download model from server
    std::cout << "Downloading model: " << modelID << std::endl;
    std::string modelData = GrpcClient::DownloadModel(modelID);

    // Save model to file
    // TODO: model should be saved to a temporary file and chunks should be written to it
    std::cout << "Saving model to file: " << modelID << std::endl;
    SaveModelToFile(modelData, std::string("./") + modelID + std::string(".bin"));

    // Create random UUID4 for model update
    std::string modelUpdateID = generateRandomUUID();
    std::cout << "Generated random UUID " << modelUpdateID << " for model update" << std::endl;

    // Train the model
    this->Train(std::string("./") + modelID + std::string(".bin"), std::string("./") + modelUpdateID + std::string(".bin"));

    // Read the binary string from the file
    std::cout << "Loading model from file: " << modelUpdateID << std::endl;
    std::string data = LoadModelFromFile(std::string("./") + modelUpdateID + std::string(".bin"));

    // Upload model to server
    GrpcClient::UploadModel(modelUpdateID, data);

    // Send model update response to server
    GrpcClient::SendModelUpdate(modelID, modelUpdateID, requestData);

    // Delete model from disk
    DeleteFileFromDisk(std::string("./") + modelID + std::string(".bin"));
    DeleteFileFromDisk(std::string("./") + modelUpdateID + std::string(".bin"));
}
/**
 * Send model update message to server.
 *
 * @param modelID The model ID (Global) to send model update for.
 * @param modelUpdateID The model update ID (Local) to send model update for.
 * @param config The round config
 */
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

    // TODO: get metadata from Train function
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
    else {
      std::cout << "SendModelUpdate: Response: " << response.response() << std::endl;
    }
    // Garbage collect the client object.
    Client *clientCollect = modelUpdate.release_sender();
}
void GrpcClient::SendModelValidation(const std::string& modelID, const std::string& metricData, TaskRequest& requestData) {
    // Send model validation response to server
    Client client;
    client.set_name(name_);
    client.set_role(WORKER);
    client.set_client_id(id_);

    ModelValidation validation;
    validation.set_allocated_sender(&client);
    validation.set_model_id(modelID);
    validation.set_data(metricData);
    validation.set_session_id(requestData.session_id());

    // TODO: get metadata from Train function
    // string as json
    std::ostringstream oss;
    std::string metadata = "{\"validation_metadata\": {\"num_examples\": 3000}}";
    validation.set_meta(metadata);
    
    // get current date and time
    google::protobuf::Timestamp* timestamp = validation.mutable_timestamp();
    // Get the current time
    auto now = std::chrono::system_clock::now();
    // Convert to time_t
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    // Convert to Timestamp
    timestamp->set_seconds(now_c);
    timestamp->set_nanos(0);

    // The actual RPC.
    ClientContext context;
    Response response;
    Status status = combinerStub_->SendModelValidation(&context, validation, &response);
    std::cout << "SendModelValidation: " << validation.model_id() << std::endl;

    if (!status.ok()) {
      std::cout << "SendModelValidation: failed for model: " << modelID << std::endl;
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
      std::cout << "SendModelValidation: Response: " << response.response() << std::endl;
    }
    else {
      std::cout << "SendModelValidation: Response: " << response.response() << std::endl;
    }
    // Garbage collect the client object.
    Client *clientCollect = validation.release_sender();
}


void GrpcClient::Validate(const std::string& inModelPath, const std::string& outMetricPath) {
    // Placeholder for model validation logic
    std::cout << "Validating model: " << inModelPath << std::endl;
}

void GrpcClient::ValidateGlobalModel(const std::string& modelID, TaskRequest& requestData) {
    std::cout << "Validating global model: " << modelID << std::endl;

    // Download model from server
    std::cout << "Downloading model: " << modelID << std::endl;
    std::string modelData = GrpcClient::DownloadModel(modelID);

    // Save model to file
    // TODO: model should be saved to a temporary file and chunks should be written to it
    std::cout << "Saving model to file: " << modelID << std::endl;
    SaveModelToFile(modelData, std::string("./") + modelID + std::string(".bin"));

    const std::string metricPath = std::string("./") + modelID + std::string(".json");

    // Validate the model
    this->Validate(std::string("./") + modelID + std::string(".bin"), metricPath);

    // Read the metric file from disk
    std::cout << "Loading metric from file: " << metricPath << std::endl;
    json metricData = LoadMetricsFromFile(metricPath);

    // Send model validation response to server
    GrpcClient::SendModelValidation(modelID, metricData.dump(), requestData);

    // Delete metrics file from disk
    DeleteFileFromDisk(metricPath);
    // Delete model from disk
    DeleteFileFromDisk(std::string("./") + modelID + std::string(".bin"));
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

void GrpcClient::SetName(const std::string& name) {
    name_ = name;
}

void GrpcClient::SetId(const std::string& id) {
    id_ = id;
}

void GrpcClient::SetChunkSize(std::size_t chunkSize) {
    this->chunkSize = chunkSize;
}

std::size_t GrpcClient::GetChunkSize() {
    return chunkSize;
}

void SendIntervalHeartBeat(GrpcClient* client, int intervalSeconds) {
  while (true) {
      client->HeartBeat();
      std::this_thread::sleep_for(std::chrono::seconds(intervalSeconds));
  }
}

std::map<std::string, std::string> readCombinerConfig(YAML::Node configFile) {
    // Read HTTP configuration from the "client.yaml" file
    std::map<std::string, std::string> combinerConfig;

    if (configFile["combiner"]) {
        combinerConfig["host"] = configFile["combiner"].as<std::string>();
    }
    if (configFile["proxy_server"]) {
        combinerConfig["proxy_host"] = configFile["proxy_server"].as<std::string>();
    }
    if (configFile["insecure"]) {
        combinerConfig["insecure"] = configFile["insecure"].as<std::string>();
    }
    else {
        combinerConfig["insecure"] = "false";
    }
    if (configFile["token"]) {
        combinerConfig["token"] = configFile["token"].as<std::string>();
    }
    if (configFile["auth_scheme"]) {
        combinerConfig["auth_scheme"] = configFile["auth_scheme"].as<std::string>();
    }
    else {
        combinerConfig["auth_scheme"] = "Bearer";
    }

    return combinerConfig;
}

std::map<std::string, std::string> readControllerConfig(YAML::Node config) {
    // Read requestData from the config
    std::cout << "Reading HTTP request data from config file" << std::endl;
    std::map<std::string, std::string> controllerConfig;

    // Check if there is a valid "discover_host" key in the config
    std::string apiUrl = config["discover_host"].as<std::string>();
    if (apiUrl.empty()) {
        throw std::runtime_error("Invalid discover_host.");
    }
    controllerConfig["api_url"] = "https://" + apiUrl;

    // Check if there is a "token" key in the config file
    std::string token = "";
    if (config["token"]) {
        token = config["token"].as<std::string>();
        // Check if the "token" value is valid
        if (token.empty()) {
            throw std::runtime_error("Invalid token.");
        }
    }
    controllerConfig["token"] = token;

    controllerConfig["client_id"] = config["client_id"].as<std::string>();
    controllerConfig["name"] = config["name"].as<std::string>();
    if (config["package"]) {
        controllerConfig["package"] = config["package"].as<std::string>();
    } else {
        controllerConfig["package"] = "remote";
    }
    
    // Check if preferred_combiner is in the config, else use default empty string
    if (config["preferred_combiner"]) {
        controllerConfig["preferred_combiner"] = config["preferred_combiner"].as<std::string>();
    } else {
        controllerConfig["preferred_combiner"] = "";
    }

    return controllerConfig;
}

// Code - Client
FednClient::FednClient(std::string configFilePath) {
    // Read HTTP configuration from the "client.yaml" file
    YAML::Node config = YAML::LoadFile(configFilePath);
    controllerConfig = readControllerConfig(config);
    combinerConfig = readCombinerConfig(config);

    // Create a Client instance with the API URL and token (if provided)
    httpClient = std::make_shared<HttpClient>(controllerConfig["api_url"], controllerConfig["token"]);
}

std::map<std::string, std::string> FednClient::getCombinerConfig() {
    #ifdef DEBUG
    std::cout << "DEBUG Combiner configuration PRE-ASSIGNMENT:" << std::endl;
    for (auto const& x : combinerConfig) {
        std::cout << "  " << x.first << ": " << x.second << std::endl;
    }
    #endif
    
    if (combinerConfig["host"].empty()) {
        // Assign to combiner
        combinerConfig = assign();
        #ifdef DEBUG
        std::cout << "DEBUG Combiner configuration POST-ASSIGNMENT:" << std::endl;
        for (auto const& x : combinerConfig) {
            std::cout << "  " << x.first << ": " << x.second << std::endl;
        }
        #endif
    }
    return combinerConfig;
}

std::shared_ptr<ChannelInterface> FednClient::getChannel() {
    return channel;
}

void FednClient::run(std::shared_ptr<GrpcClient> customGrpcClient) {
    // Add the custom gRPC client to the FednClient object
    grpcClient = customGrpcClient;

    // Set name, id and chunk size
    grpcClient->SetName(controllerConfig["name"]);
    grpcClient->SetId(controllerConfig["client_id"]);

    // Start heart beat thread and listen to model update requests
    std::thread HeartBeatThread(SendIntervalHeartBeat, grpcClient.get(), 10);
    grpcClient->ConnectTaskStream();
    HeartBeatThread.join();
}

std::map<std::string, std::string> FednClient::assign() {
    // Request assignment to combiner
    json httpResponseData = httpClient->assign(controllerConfig);

    // Pretty print of the response
    std::cout << "Response: " << httpResponseData.dump(4) << std::endl;

    // Setup gRPC channel configuration
    combinerConfig["host"] = httpResponseData["host"];
    combinerConfig["proxy_host"] = httpResponseData["fqdn"];
    combinerConfig["token"] = httpClient->getToken();

    return combinerConfig;
}

std::shared_ptr<ChannelInterface> FednClient::setupGrpcChannel(std::map<std::string, std::string> combinerConfig) {
    // Instantiate the client. It requires a channel, out of which the actual RPCs
    // are created. This channel models a connection to an server specified by
    // the argument "--target=".

    std::cout << "Server host: " << combinerConfig["host"] << std::endl;

    // initialize credentials
    std::shared_ptr<grpc::ChannelCredentials> creds;
    if (combinerConfig["insecure"] == "true") {
        std::cout << "Using insecure channel" << std::endl;
        // Create an call credentials object for use with an insecure channel
        creds = grpc::InsecureChannelCredentials();
    } else {
        std::cout << "Using secure channel" << std::endl;
        // check if token is empty
        if (combinerConfig["token"].empty()) {
            throw std::runtime_error("Token is empty, exiting...");
        }
        // check if auth_scheme is empty
        if (combinerConfig["auth_scheme"].empty()) {
            throw std::runtime_error("Auth scheme is empty, exiting...");
        }
        // Check if auth_scheme is Token or Bearer
        if (combinerConfig["auth_scheme"] != "Token" && combinerConfig["auth_scheme"] != "Bearer") {
            throw std::runtime_error("Invalid auth scheme, exiting...");
        }
        // Create authorization header value string for metadata
        std::string header_value = combinerConfig["auth_scheme"] + (std::string) " " + combinerConfig["token"];

        // Create call credentials
        auto call_creds = grpc::MetadataCredentialsFromPlugin(
        std::unique_ptr<grpc::MetadataCredentialsPlugin>(
            new MyCustomAuthenticator(header_value)));

        // Create channel credentials
        auto channel_creds = grpc::SslCredentials(grpc::SslCredentialsOptions());

        // Get the server host to metadata
        auto metadata_creds = grpc::MetadataCredentialsFromPlugin(
        std::unique_ptr<grpc::MetadataCredentialsPlugin>(
            new CustomMetadata("grpc-server", combinerConfig["host"])));

        // Create intermediate composite channel credentials
        auto inter_creds = grpc::CompositeChannelCredentials(channel_creds, call_creds);
        // Create composite channel credentials
        creds = grpc::CompositeChannelCredentials(inter_creds, metadata_creds);
    }
    // Check if proxy host is set, and change host to proxy host, server host will be in metadata
    if (!combinerConfig["proxy_host"].empty()) {
        std::cout << "Proxy host: " << combinerConfig["proxy_host"] << std::endl;
        combinerConfig["host"] = combinerConfig["proxy_host"];
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

    channel = grpc::CreateCustomChannel(combinerConfig["host"], creds, args);

    return channel;
}

std::shared_ptr<GrpcClient> FednClient::getGrpcClient() {
    return grpcClient;
}

std::shared_ptr<HttpClient> FednClient::getHttpClient() {
    return httpClient;
}

void FednClient::setAuthScheme(std::string authScheme) {
    combinerConfig["auth_scheme"] = authScheme;
}

void FednClient::setCombinerHost(std::string host) {
    combinerConfig["host"] = host;
}

void FednClient::setInsecure(bool insecure) {
    combinerConfig["insecure"] = insecure ? "true" : "false";
}

void FednClient::setProxyHost(std::string proxyHost) {
    combinerConfig["proxy_host"] = proxyHost;
}

void FednClient::setToken(std::string token) {
    controllerConfig["token"] = token;
    combinerConfig["token"] = token;
}