#include <fednlib.h>

class CustomGrpcClient : public GrpcClient {
public:
    CustomGrpcClient(std::shared_ptr<ChannelInterface> channel) : GrpcClient(channel) {}
    
    // void UpdateLocalModel(const std::string& modelID, const std::string& requestData) override {
    //     // Custom model update code
    //     std::cout << "Custom model update code" << modelID << std::endl;

    //     // Download model from server
    //     std::cout << "Downloading model: " << modelID << std::endl;
    //     std::string modelData = GrpcClient::DownloadModel(modelID);

    //     // Save model to file
    //     // TODO: model should be saved to a temporary file and chunks should be written to it
    //     std::cout << "Saving model to file: " << modelID << std::endl;
    //     SaveModelToFile(modelData, std::string("./") + modelID);

    //     // Create random UUID4 for model update
    //     std::cout << "Generating random UUID for model update" << std::endl;
    //     std::string modelUpdateID = generateRandomUUID();

    //     // Train the model
    //     this->Train(std::string("./") + modelID, std::string("./") + modelUpdateID);

    //     // Read the binary string from the file
    //     std::cout << "Loading model from file: " << modelUpdateID << std::endl;
    //     std::string data = LoadModelFromFile(std::string("./") + modelUpdateID + std::string(".bin"));

    //     // Upload model to server
    //     GrpcClient::UploadModel(modelUpdateID, data);

    //     // Send model update response to server
    //     GrpcClient::SendModelUpdate(modelID, modelUpdateID, requestData);

    //     DeleteModelFromDisk(std::string("./") + modelID + std::string(".bin"));
    //     DeleteModelFromDisk(std::string("./") + modelUpdateID + std::string(".bin"));
    // }

    void Train(const std::string& inModelPath, const std::string& outModelPath) override {

        // std::cout << "Dummy code: Updating local model with global model as seed!" << std::endl;
        // // Dummy code: update model with modelData
        // // Example of loading a matrix from a binary file using Armadillo
        // arma::mat loadedData;
        // loadedData.load(inModelPath, arma::raw_binary);
        // std::cout << "Loaded data: " << loadedData << std::endl;
        // // Update the matrix (model)
        // loadedData += 1;

        // std::string modelUpdateData = modelData;
        
        // // Save the matrix as raw binary file
        // // Using Armadillo to save the matrix as a binary file
        // loadedData.save(outModelPath, arma::raw_binary);

        // Using own code to load the matrix from a binary file, dymmy code. Remove if using Armadillo
        std::string modelData = LoadModelFromFile(inModelPath);
        
        // Send the same model back as update
        std::string modelUpdateData = modelData;
        std::cout << "Training model..." << std::endl;
        std::cout << "In model: " << inModelPath << std::endl;
        std::cout << "Out model: " << outModelPath << std::endl;
        
        // Using own code to save the matrix as a binary file, dymmy code. Remove if using Armadillo
        SaveModelToFile(modelUpdateData, outModelPath);
    }
};

int main(int argc, char** argv) {
    FednClient client("../../client.yaml");

    // client.setHost("newtest-wrr-fedn-combiner");

    std::map<std::string, std::string> combinerConfig = client.getCombinerConfig();
    // i client.yaml combiner info_
    //    key: beskrivning
    //    combiner: combiner hostn name "host" in code
    //    proxy_server: grpc.fedn.ai, called proxy_host in code     Channel(host=grpc.fedn.scaleout.com, creds) + metadata["grpc-server"] = combiner, Channel(host=combiner, creds)
    //    insecure: false (optional)
    //    token: token (optional)
    //    auth_scheme: Bearer (optional)

    std::shared_ptr<ChannelInterface> channel = client.setupGrpcChannel(combinerConfig);

    std::shared_ptr<HttpClient> http_client = client.getHttpClient();
    
    std::shared_ptr<GrpcClient> customGrpcClient = std::make_shared<CustomGrpcClient>(channel);

    // customGrpcClient->HeartBeat();
    client.run(customGrpcClient);

    return 0;
}