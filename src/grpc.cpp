
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>

#include "../include/fednlib/grpc.h"
#include "../include/fednlib/utils.h"
#include "google/protobuf/timestamp.pb.h"

using grpc::ClientContext;
using grpc::Status;
using grpc::ClientReader;
using grpc::ClientWriter;

using fedn::Heartbeat;
using fedn::ModelUpdate;
using fedn::ModelValidation;
using fedn::ModelResponse;
using fedn::ModelRequest;
using fedn::ModelStatus;
using fedn::StatusType;
using fedn::Client;
using fedn::ClientAvailableMessage;
using fedn::WORKER;
using fedn::Response;

GrpcClient::GrpcClient(std::shared_ptr<ChannelInterface> channel)
      : connectorStub_(Connector::NewStub(channel)),
        combinerStub_(Combiner::NewStub(channel)),
        modelserviceStub_(ModelService::NewStub(channel)) {
            this->SetChunkSize(1024 * 1024);
        }

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
void GrpcClient::SendModelValidation(const std::string& modelID, json& metricData, TaskRequest& requestData) {
    // Send model validation response to server
    Client client;
    client.set_name(name_);
    client.set_role(WORKER);
    client.set_client_id(id_);

    ModelValidation validation;
    validation.set_allocated_sender(&client);
    validation.set_model_id(modelID);
    validation.set_data(metricData.dump());
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
    GrpcClient::SendModelValidation(modelID, metricData, requestData);

    // Delete metrics file from disk
    DeleteFileFromDisk(metricPath);
    // Delete model from disk
    DeleteFileFromDisk(std::string("./") + modelID + std::string(".bin"));
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