
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <fstream>

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
using fedn::ModelPrediction;
using fedn::ModelResponse;
using fedn::ModelRequest;
using fedn::ModelStatus;
using fedn::StatusType;
using fedn::Client;
using fedn::ClientAvailableMessage;
using fedn::CLIENT;
using fedn::Response;
using fedn::ModelMetric;
using fedn::NamedMetric;

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
    client->set_role(CLIENT);
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
 * validating the global model, or handling model prediction.
 * 
 * @note The function currently only handles MODEL_UPDATE and MODEL_VALIDATION task types.
 *       Model prediction is not yet implemented.
 */
void GrpcClient::connectTaskStream() {
    // Data we are sending to the server.
    Client* client = new Client();
    client->set_name(name_);
    client->set_role(CLIENT);
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
        this->loggingContext = LoggingContext(task);
        this->updateLocalModel(task.model_id(), task.data());
        this->loggingContext.reset();
      }
      else if (task.type() == StatusType::MODEL_VALIDATION) {
        this->loggingContext = LoggingContext(task);
        this->validateGlobalModel(task.model_id(), task);
        this->loggingContext.reset();
      }
      else if (task.type() == StatusType::MODEL_PREDICTION) {
        this->loggingContext = LoggingContext(task);
        this->predictGlobalModel(task.model_id(), task);
        this->loggingContext.reset();
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

     // Set client
    Client* client = new Client();
    client->set_name(name_);
    client->set_role(CLIENT);
    client->set_client_id(id_);

    // Pass ownership of client to protobuf message
    request.set_allocated_sender(client);

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
 * @brief Streams a model from the server to a local file.
 *
 * This function streams a model identified by the given modelID from the server
 * and writes it to a file specified by modelPath. It uses gRPC for communication
 * with the server and handles the streaming of data in chunks.
 *
 * @param modelID The ID of the model to be streamed.
 * @param modelPath The path to the file where the streamed model will be saved.
 *
 * @note If the file cannot be opened for writing, an error message is printed.
 * @note The function assumes that the server sends the model data in chunks and
 *       that the status of the model response indicates the progress of the download.
 */
void GrpcClient::downloadModelToFile(const std::string& modelID, const std::string& modelPath) {
    std::cout << "Buffering model " << modelID << "..." << std::endl;

    // request 
    ModelRequest request;
    request.set_id(modelID);

    // context
    ClientContext context;

    // Set client
    Client* client = new Client();
    client->set_name(name_);
    client->set_role(CLIENT);
    client->set_client_id(id_);

    // Pass ownership of client to protobuf message
    request.set_allocated_sender(client);

    // Get ClientReader from stream
    std::unique_ptr<ClientReader<ModelResponse> > reader(
        modelserviceStub_->Download(&context, request));

    // Create an ofstream object and open the file in binary mode
    std::ofstream outFile(modelPath, std::ios::binary); // Before stream loop

    // Check if the file was opened successfully
    if (!outFile) {
        std::cerr << "Error opening file for writing" << std::endl;
    }

    // Number of bytes streamed so far
    size_t streamedDataSize = 0;

    // Read from stream
    ModelResponse modelResponse;
    while (reader->Read(&modelResponse)) {
        std::cout << "ModelResponseID: " << modelResponse.id() << std::endl;
        std::cout << "ModelResponseStatus: " << modelResponse.status() << std::endl;
        if (modelResponse.status() == ModelStatus::IN_PROGRESS) {
            const std::string& dataResponse = modelResponse.data();
            // Increment number of bytes streamed
            streamedDataSize += dataResponse.size();
            // Write the binary string to the file
            outFile.write(dataResponse.c_str(), dataResponse.size()); // In each iteration. After chunk is downloaded
            std::cout << "Download in progress: " << modelResponse.id() << std::endl;
            // TODO Calculate and print current written size
            std::cout << "Downloaded size: " << streamedDataSize << " bytes" << std::endl;
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

    // Close the file
    outFile.close();
    std::cout << "modelData saved to file " << modelPath << " successfully" << std::endl;

    reader->Finish();
    std::cout << "Downloaded size: " << streamedDataSize << " bytes" << std::endl;
    std::cout << "Disconnecting from DownloadStream" << std::endl;
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
    client->set_role(CLIENT);
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

void GrpcClient::uploadModelFromFile(const std::string& modelID, const std::string& modelPath) {
    // Create an ifstream object and open the file in binary mode
    std::ifstream inFile(modelPath, std::ios::binary);
    // Check if the file was opened successfully
    if (!inFile) {
        std::cerr << "Error opening file " << modelPath << " for reading" << std::endl;
        return;
    }

    // Get the length of the file
    inFile.seekg(0, inFile.end);
    size_t totalSize = inFile.tellg();
    inFile.seekg(0, inFile.beg);

    // response 
    ModelResponse response;
    // context
    ClientContext context;

    // Client
    Client* client = new Client();
    client->set_name(name_);
    client->set_role(CLIENT);
    client->set_client_id(id_);

    // Get ClientWriter from stream
    std::unique_ptr<ClientWriter<ModelRequest> > writer(
        modelserviceStub_->Upload(&context, &response));

    // Calculate the number of chunks
    size_t chunkSize = this->getChunkSize();
    size_t offset = 0;

    std::cout << "Upload in progress: " << modelID << std::endl;
    std::cout << "Chunk size: " << chunkSize << " bytes" << std::endl;

    while (offset < totalSize) {
        ModelRequest request;
        size_t currentChunkSize = std::min(chunkSize, totalSize - offset);
        std::string buffer(currentChunkSize, '\0');
        inFile.read(&buffer[0], currentChunkSize);
        std::cout << "File pointer position: " << inFile.tellg() << std::endl;
        request.set_data(buffer);
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
            inFile.close();
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
    inFile.close();

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

    // Generate random UUIDs for temporary files
    std::string tempModelFile = generateRandomUUID();
    std::string modelUpdateID = generateRandomUUID();
    
    // Create file paths
    std::string inModelPath = "./" + tempModelFile + ".bin";
    std::string outModelPath = "./" + modelUpdateID + ".bin";

    // Stream model and write it to file
    downloadModelToFile(modelID, inModelPath);

    std::cout << "Generated random UUID " << modelUpdateID << " for model update" << std::endl;

    // train the model
    this->train(inModelPath, outModelPath);

    std::cout << "Streaming model from file: " << modelUpdateID << std::endl;
    GrpcClient::uploadModelFromFile(modelUpdateID, outModelPath);

    // Send model update response to server
    GrpcClient::sendModelUpdate(modelID, modelUpdateID, requestData);

    // Delete model from disk
    deleteFileFromDisk(inModelPath);
    deleteFileFromDisk(outModelPath);
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

    // Generate random UUIDs for temporary files
    std::string tempModelFile = generateRandomUUID();
    std::string tempMetricFile = generateRandomUUID();
    
    // Create file paths
    std::string modelPath = "./" + tempModelFile + ".bin";
    std::string metricPath = "./" + tempMetricFile + ".json";

    // Stream model to file
    downloadModelToFile(modelID, modelPath);

    // validate the model
    this->validate(modelPath, metricPath);

    // Read the metric file from disk
    std::cout << "Loading metric from file: " << metricPath << std::endl;
    json metricData = loadMetricsFromFile(metricPath);

    // Send model validation response to server
    GrpcClient::sendModelValidation(modelID, metricData, requestData);

    // Delete temporary files from disk
    deleteFileFromDisk(metricPath);
    deleteFileFromDisk(modelPath);
}

/**
 * @brief (To override) Performs model prediction using the specified model and saves the results to the given output path.
 *
 * This function loads a model from the provided file path, performs prediction, and saves the prediction results
 * to the specified output file. The prediction process is currently mocked with placeholder data and should be overridden
 * by the user of this library API.
 *
 * @param modelPath The file path to the model to be used for prediction.
 * @param outputPath The file path where the prediction results will be saved.
 */
void GrpcClient::predict(const std::string& modelPath, const std::string& outputPath) {
    // Placeholder for model prediction logic
    std::cout << "Performing model prediction on model: " << modelPath << std::endl;

    std::string modelData = loadModelFromFile(modelPath);

    // Mock model prediction data classificaion
    json predictionData = {
        {"prediction", 1},
        {"confidence", 0.95}
    };

    // Save prediction data to file
    saveMetricsToFile(predictionData, outputPath);
}

/**
 * @brief Performs prediction using a global model identified by modelID.
 *
 * This function downloads a model from the server, saves it to a file, performs
 * prediction using the model, reads the prediction data from a file, and sends the
 * prediction results back to the server. Finally, it cleans up by deleting the
 * model and output files from disk.
 *
 * @param modelID The identifier of the model to be used for prediction.
 * @param requestData The task request data to be sent along with the prediction results.
 */
void GrpcClient::predictGlobalModel(const std::string& modelID, TaskRequest& requestData) {
    

    // Generate random UUIDs for temporary files
    std::string tempModelFile = generateRandomUUID();
    std::string tempPredictionFile = generateRandomUUID();
    
    // Create file paths
    std::string modelPath = "./" + tempModelFile + ".bin";
    std::string predictionPath = "./" + tempPredictionFile + ".json";

    // Stream model to file
    downloadModelToFile(modelID, modelPath);

    // Perform model prediction
    this->predict(modelPath, predictionPath);

    // Read the prediction data from the file
    std::cout << "Loading prediction data from file: " << predictionPath << std::endl;
    json predictionData = loadMetricsFromFile(predictionPath);

    // Send model prediction response to server
    GrpcClient::sendModelPrediction(modelID, predictionData, requestData);

    // Delete temporary files from disk
    deleteFileFromDisk(modelPath);
    deleteFileFromDisk(predictionPath);
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
    client.set_role(CLIENT);
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
    client.set_role(CLIENT);
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
 * @brief Sends a model prediction response to the server.
 *
 * This function constructs a `ModelPrediction` message with the provided model ID,
 * prediction data, and request data, and sends it to the server using gRPC.
 *
 * @param modelID The ID of the model for which the prediction is being sent.
 * @param predictionData The prediction data in JSON format.
 * @param requestData The task request data containing session information.
 */
void GrpcClient::sendModelPrediction(const std::string& modelID, json& predictionData, TaskRequest& requestData) {
    // Send model prediction response to server
    Client client;
    client.set_name(name_);
    client.set_role(CLIENT);
    client.set_client_id(id_);

    ModelPrediction prediction;

    // Pass ownership of client to protobuf message
    prediction.set_allocated_sender(&client);
    prediction.set_model_id(modelID);
    prediction.set_data(predictionData.dump());
    prediction.set_prediction_id(requestData.session_id());

    // string as json
    std::ostringstream oss;
    std::string metadata = "{\"prediction_metadata\": {\"num_examples\": 3000}}";
    prediction.set_meta(metadata);
    
    // get current date and time
    google::protobuf::Timestamp* timestamp = prediction.mutable_timestamp();
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
    Status status = combinerStub_->SendModelPrediction(&context, prediction, &response);
    std::cout << "sendModelPrediction: " << prediction.model_id() << std::endl;

    if (!status.ok()) {
      std::cout << "sendModelPrediction: failed for model: " << modelID << std::endl;
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
      std::cout << "sendModelPrediction: Response: " << response.response() << std::endl;
    }
    else {
      std::cout << "sendModelPrediction: Response: " << response.response() << std::endl;
    }
    // Get the ownership of the client object back so it is deleted correctly at end of scope
    Client *clientCollect = prediction.release_sender();    
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


bool GrpcClient::log_metrics(const std::map<std::string, float>& metrics, const std::optional<int> step, const bool commit){
    // Add step and commit information if provided
    if (step.has_value()) {
        loggingContext.setStep(step.value());
    }
    std::string roundId = loggingContext.getRoundId();
    std::string modelId = loggingContext.getModelId();
    std::string sessionId = loggingContext.getSessionId();
    int loggingStep = loggingContext.getStep();
    if (commit){
        loggingContext.incrementStep();
    }

    return this->sendModelMetrics(metrics, this->name_, this->id_, modelId, sessionId, roundId, loggingStep);
}

bool GrpcClient::sendModelMetrics(const std::map<std::string, float>& metrics, 
        const std::string& name, 
        const std::string& client_id, 
        const std::string& modelID, 
        const std::string& roundID, 
        const std::string& sessionID, 
        const int step){
    ModelMetric modelMetric;
    Client* client = modelMetric.mutable_sender();
    client->set_name(name_);
    client->set_role(CLIENT);
    client->set_client_id(id_);
    modelMetric.set_model_id(modelID);
    modelMetric.set_round_id(roundID);
    modelMetric.set_session_id(sessionID);
    modelMetric.mutable_step()->set_value(step);

    for (const auto& metric : metrics) {
        auto* metricEntry = modelMetric.add_metrics();
        metricEntry->set_key(metric.first);
        metricEntry->set_value(metric.second);
    }

    ClientContext context;
    Response response;
    Status status = connectorStub_->SendModelMetric(&context, modelMetric, &response);
    std::cout << "sendModelMetrics: " << modelMetric.model_id() << std::endl;

    if (!status.ok()) {
        std::cout << "sendModelMetrics: failed for model: " << modelID << std::endl;
        std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
        std::cout << "sendModelMetrics: Response: " << response.response() << std::endl;
        return false;
    }
    else {
        std::cout << "sendModelMetrics: Response: " << response.response() << std::endl;
    }
    return true;
}


LoggingContext::LoggingContext(TaskRequest& requestData) {
    this->sessionId = requestData.session_id();
    this->modelId = requestData.model_id();
    
    if (requestData.type() == StatusType::MODEL_UPDATE) {
        json roundData = json::parse(requestData.data());
        this->roundId = roundData["round_id"];
    }
    this->step = 0;
}