#include <clientlib.h>

class CustomGrpcClient : public GrpcClient {
public:
    CustomGrpcClient(std::shared_ptr<ChannelInterface> channel) : GrpcClient(channel) {}
    
    void UpdateLocalModel(const std::string& modelID, const std::string& requestData) override {
        // Custom model update code
        std::cout << "Custom model update code" << std::endl;
    }
};

int main(int argc, char** argv) {
    CppClient client(argc, argv);
    
    std::shared_ptr<GrpcClient> customGrpcClient = std::make_shared<CustomGrpcClient>(client.getChannel());

    client.run(customGrpcClient);

    return 0;
}