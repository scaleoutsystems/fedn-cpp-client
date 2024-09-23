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
    FednClient client("../../client.yaml");

    client.setHost("newtest-wrr-fedn-combiner");

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

    customGrpcClient->HeartBeat();
    // client.run(customGrpcClient);

    return 0;
}