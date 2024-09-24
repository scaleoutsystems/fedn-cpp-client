#include "../include/fednlib.h"

class CustomGrpcClient : public GrpcClient {
public:
    CustomGrpcClient(std::shared_ptr<ChannelInterface> channel) : GrpcClient(channel) {}

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
    FednClient client("../../../client.yaml");

    std::map<std::string, std::string> combinerConfig = client.getCombinerConfig();

    std::shared_ptr<ChannelInterface> channel = client.setupGrpcChannel(combinerConfig);

    std::shared_ptr<HttpClient> http_client = client.getHttpClient();
    
    std::shared_ptr<GrpcClient> customGrpcClient = std::make_shared<CustomGrpcClient>(channel);

    client.run(customGrpcClient);

    return 0;
}