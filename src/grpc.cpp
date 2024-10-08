
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

/**
 * @brief Constructs a new GrpcClient object.
 * 
 * This constructor initializes the GrpcClient with the provided gRPC channel.
 * It creates stubs for the Connector, Combiner, and ModelService services.
 * Additionally, it sets the default chunk size for data transfer.
 * 
 * @param channel A shared pointer to a gRPC ChannelInterface used to communicate with the server.
 */
GrpcClient::GrpcClient(std::shared_ptr<ChannelInterface> channel)
      : connectorStub_(Connector::NewStub(channel)),
        combinerStub_(Combiner::NewStub(channel)),
        modelserviceStub_(ModelService::NewStub(channel)) {
            this->setChunkSize(1024 * 1024);
        }

/**
 * @brief Sends a heartbeat message to the server.
 *
 * This function creates a heartbeat message containing client information 
 * and sends it to the server using gRPC. It prints the server's response 
 * and handles any errors that occur during the RPC.
 *
 * The heartbeat message includes the client's name, role, and ID. The 
 * server's response is printed to the standard output.
 */
void GrpcClient::heartBeat() {
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

/**
 * @brief Establishes a connection to the TaskStream and processes incoming tasks.
 * 
 * This function sets up a gRPC client, sends a ClientAvailableMessage to the server,
 * and listens for TaskRequest messages from the combiner. Depending on the type of 
 * TaskRequest received, it performs different actions such as updating the local model,
 * validating the global model, or handling model inference.
 * 
 * @note The function currently only handles MODEL_UPDATE and MODEL_VALIDATION task types.
 *       Model inference is not yet implemented.
 */
void GrpcClient::connectTaskStream() {
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
        this->updateLocalModel(task.model_id(), task.data());
      }
      else if (task.type() == StatusType::MODEL_VALIDATION) {
        this->validateGlobalModel(task.model_id(), task);
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
 * @brief Downloads a model from the server using the provided model ID.
 *
 * This function sends a request to the server to download a model identified by the given model ID.
 * It reads the model data from the server in a streaming manner and accumulates the data until the
 * download is complete or fails.
 *
 * @param modelID The ID of the model to be downloaded.
 * @return A string containing the accumulated model data.
 */
std::string GrpcClient::downloadModel(const std::string& modelID) {

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
 * @brief Uploads a model to the server in chunks.
 * 
 * This function uploads a model to the server by dividing the model data into chunks
 * and sending each chunk sequentially. It uses gRPC for communication and handles
 * the streaming of data to the server.
 * 
 * @param modelID The unique identifier for the model being uploaded.
 * @param modelData The binary data of the model to be uploaded.
 */
void GrpcClient::uploadModel(std::string& modelID, std::string& modelData) {
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
    size_t chunkSize = this->getChunkSize();
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
 * @brief (To override) Trains the model by loading it from a file, processing it, and saving the updated model to another file.
 * 
 * This function should perform the following steps:
 * 1. Load the model data from the specified input file path.
 * 2. Process the model data with ML computations. Base version below just sends the same model back as an update.
 * 3. Save the updated model data to the specified output file path in binary format.
 * 
 * @param inModelPath The file path to load the model data from.
 * @param outModelPath The file path to save the updated model data to.
 */
void GrpcClient::train(const std::string& inModelPath, const std::string& outModelPath) {
    // Using own code to load the matrix from a binary file, dymmy code. Remove if using Armadillo
    std::string modelData = loadModelFromFile(inModelPath);
    
    // Send the same model back as update
    std::string modelUpdateData = modelData;
    
    // Using own code to save the matrix as a binary file, dymmy code. Remove if using Armadillo
    saveModelToFile(modelUpdateData, outModelPath);
}

/**
 * @brief Updates the local model by downloading it from the server, training it, and uploading the updated model back to the server.
 * 
 * This function performs the following steps:
 * 1. Downloads the model from the server using the provided model ID.
 * 2. Saves the downloaded model to a file.
 * 3. Generates a random UUID for the model update.
 * 4. Trains the model using the saved file and generates an updated model file.
 * 5. Loads the updated model from the file.
 * 6. Uploads the updated model to the server.
 * 7. Sends a model update response to the server.
 * 8. Deletes the temporary model files from the disk.
 * 
 * @param modelID The ID of the model to be updated.
 * @param requestData Additional request data to be sent with the model update via gRPC.
 */
void GrpcClient::updateLocalModel(const std::string& modelID, const std::string& requestData) {
    std::cout << "Updating local model: " << modelID << std::endl;

    // Download model from server
    std::cout << "Downloading model: " << modelID << std::endl;
    std::string modelData = GrpcClient::downloadModel(modelID);

    // Save model to file
    // TODO: model should be saved to a temporary file and chunks should be written to it
    std::cout << "Saving model to file: " << modelID << std::endl;
    saveModelToFile(modelData, std::string("./") + modelID + std::string(".bin"));

    // Create random UUID4 for model update
    std::string modelUpdateID = generateRandomUUID();
    std::cout << "Generated random UUID " << modelUpdateID << " for model update" << std::endl;

    // train the model
    this->train(std::string("./") + modelID + std::string(".bin"), std::string("./") + modelUpdateID + std::string(".bin"));

    // Read the binary string from the file
    std::cout << "Loading model from file: " << modelUpdateID << std::endl;
    std::string data = loadModelFromFile(std::string("./") + modelUpdateID + std::string(".bin"));

    // Upload model to server
    GrpcClient::uploadModel(modelUpdateID, data);

    // Send model update response to server
    GrpcClient::sendModelUpdate(modelID, modelUpdateID, requestData);

    // Delete model from disk
    deleteFileFromDisk(std::string("./") + modelID + std::string(".bin"));
    deleteFileFromDisk(std::string("./") + modelUpdateID + std::string(".bin"));
}

/**
 * @brief Sends a model update to the server.
 * 
 * This function constructs a model update message and sends it to the server using gRPC.
 * It includes metadata such as the client information, model ID, model update ID, timestamp,
 * and configuration.
 * 
 * @param modelID The ID of the model being updated.
 * @param modelUpdateID The ID of the model update.
 * @param config The configuration string for the model update.
 */
void GrpcClient::sendModelUpdate(const std::string& modelID, std::string& modelUpdateID, const std::string& config) {
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

    // TODO: get metadata from train function
    // string as json
    std::ostringstream oss;
    std::string metadata = "{\"training_metadata\": {\"epochs\": 1, \"batch_size\": 1, \"num_examples\": 3000}}";
    modelUpdate.set_meta(metadata);
    modelUpdate.set_config(config);

    // The actual RPC.
    ClientContext context;
    Response response;
    Status status = combinerStub_->SendModelUpdate(&context, modelUpdate, &response);
    std::cout << "sendModelUpdate: " << modelUpdate.model_id() << std::endl;

    if (!status.ok()) {
      std::cout << "sendModelUpdate: failed for model: " << modelID << std::endl;
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
      std::cout << "sendModelUpdate: Response: " << response.response() << std::endl;
    }
    else {
      std::cout << "sendModelUpdate: Response: " << response.response() << std::endl;
    }
    // Garbage collect the client object.
    Client *clientCollect = modelUpdate.release_sender();
}

/**
 * @brief Sends model validation data to the server.
 *
 * This function constructs a model validation message and sends it to the server
 * using gRPC. It includes client information, model ID, metric data, session ID,
 * metadata, and a timestamp.
 *
 * @param modelID The ID of the model being validated.
 * @param metricData A JSON object containing the metric data for the model validation.
 * @param requestData A TaskRequest object containing the session ID and other request data.
 */
void GrpcClient::sendModelValidation(const std::string& modelID, json& metricData, TaskRequest& requestData) {
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

    // TODO: get metadata from train function
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
    std::cout << "sendModelValidation: " << validation.model_id() << std::endl;

    if (!status.ok()) {
      std::cout << "sendModelValidation: failed for model: " << modelID << std::endl;
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
      std::cout << "sendModelValidation: Response: " << response.response() << std::endl;
    }
    else {
      std::cout << "sendModelValidation: Response: " << response.response() << std::endl;
    }
    // Garbage collect the client object.
    Client *clientCollect = validation.release_sender();
}

/**
 * @brief (To override) Validates the model located at the specified input path and outputs the metrics to the specified output path.
 * 
 * This function serves as a placeholder for the model validation logic. It currently outputs a message indicating
 * the model being validated.
 * 
 * This function should perform the following steps:
 * 1. Load the model data from the specified input file path.
 * 2. Calculate the validation metrics for the model.
 * 3. Save the model validation metrics in JSON format to the specified output file path.
 * 
 * @param inModelPath The file path to the input model that needs to be validated.
 * @param outMetricPath The file path where the validation metrics will be saved.
 */
void GrpcClient::validate(const std::string& inModelPath, const std::string& outMetricPath) {
    // Placeholder for model validation logic
    std::cout << "Validating model: " << inModelPath << std::endl;
}

/**
 * @brief Validates the global model by downloading it, saving it to a file, validating it, and sending the validation response to the server.
 * 
 * This function performs the following steps:
 * 1. Downloads the model from the server using the provided model ID.
 * 2. Saves the downloaded model to a temporary file.
 * 3. Validates the model and saves the validation metrics to a file.
 * 4. Reads the validation metrics from the file.
 * 5. Sends the model validation response to the server.
 * 6. Deletes the temporary files (model and metrics) from the disk.
 * 
 * @param modelID The ID of the model to be validated.
 * @param requestData The task request data to be sent along with the validation response via gRPC.
 */
void GrpcClient::validateGlobalModel(const std::string& modelID, TaskRequest& requestData) {
    std::cout << "Validating global model: " << modelID << std::endl;

    // Download model from server
    std::cout << "Downloading model: " << modelID << std::endl;
    std::string modelData = GrpcClient::downloadModel(modelID);

    // Save model to file
    // TODO: model should be saved to a temporary file and chunks should be written to it
    std::cout << "Saving model to file: " << modelID << std::endl;
    saveModelToFile(modelData, std::string("./") + modelID + std::string(".bin"));

    const std::string metricPath = std::string("./") + modelID + std::string(".json");

    // validate the model
    this->validate(std::string("./") + modelID + std::string(".bin"), metricPath);

    // Read the metric file from disk
    std::cout << "Loading metric from file: " << metricPath << std::endl;
    json metricData = loadMetricsFromFile(metricPath);

    // Send model validation response to server
    GrpcClient::sendModelValidation(modelID, metricData, requestData);

    // Delete metrics file from disk
    deleteFileFromDisk(metricPath);
    // Delete model from disk
    deleteFileFromDisk(std::string("./") + modelID + std::string(".bin"));
}

/**
 * @brief Sets the name for the GrpcClient.
 * 
 * This function assigns the provided name to the GrpcClient instance.
 * 
 * @param name The name to be set for the GrpcClient.
 */
void GrpcClient::setName(const std::string& name) {
    name_ = name;
}

/**
 * @brief Sets the ID for the GrpcClient.
 * 
 * This function assigns the provided ID to the GrpcClient instance.
 * 
 * @param id The ID to be set for the GrpcClient.
 */
void GrpcClient::setId(const std::string& id) {
    id_ = id;
}

/**
 * @brief Sets the chunk size for the gRPC client.
 * 
 * This method allows you to specify the size of the chunks that the gRPC client
 * will use for data transmission.
 * 
 * @param chunkSize The size of the chunks in bytes.
 */
void GrpcClient::setChunkSize(std::size_t chunkSize) {
    this->chunkSize = chunkSize;
}

/**
 * @brief Retrieves the size of the chunk.
 * 
 * This function returns the size of the chunk that is used by the GrpcClient.
 * 
 * @return std::size_t The size of the chunk.
 */
std::size_t GrpcClient::getChunkSize() {
    return chunkSize;
}

/**
 * @brief Continuously sends heartbeat signals to the server at specified intervals.
 *
 * This function runs an infinite loop that sends a heartbeat signal to the server
 * using the provided GrpcClient instance. After sending each heartbeat, the function
 * pauses for the specified interval before sending the next heartbeat.
 *
 * @param client A pointer to the GrpcClient instance used to send heartbeat signals.
 * @param intervalSeconds The interval, in seconds, between consecutive heartbeat signals.
 */
void sendIntervalHeartBeat(GrpcClient* client, int intervalSeconds) {
  while (true) {
      client->heartBeat();
      std::this_thread::sleep_for(std::chrono::seconds(intervalSeconds));
  }
}