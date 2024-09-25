#ifndef CLIENTLIB_H
#define CLIENTLIB_H

#include <string>
#include <memory>
#include "yaml-cpp/yaml.h"
#include "nlohmann/json.hpp"
#include <curl/curl.h>

#include <grpcpp/grpcpp.h>

#include "fedn.grpc.pb.h"
#include "fedn.pb.h"

using json = nlohmann::json;
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

void SaveModelToFile(const std::string& modelData, const std::string& modelPath);
void SaveMetricsToFile(const json& metrics, const std::string& metricPath);
std::string LoadModelFromFile(const std::string& modelPath);
json LoadMetricsFromFile(const std::string& metricPath);
void DeleteFileFromDisk(const std::string& path);
std::string generateRandomUUID();

class HttpClient {
public:
    HttpClient(const std::string& apiUrl, const std::string& token);
    ~HttpClient();
    json assign(const json& requestData);
    std::string getToken();

private:
    CURL* curl;
    std::string apiUrl;
    std::string token;
};

class GrpcClient {
public:
    GrpcClient(std::shared_ptr<ChannelInterface> channel);
    void HeartBeat();
    void ConnectTaskStream();
    std::string DownloadModel(const std::string& modelID);
    void UploadModel(std::string& modelID, std::string& modelData);
    virtual void UpdateLocalModel(const std::string& modelID, const std::string& requestData);
    virtual void Train(const std::string& inModelPath, const std::string& outModelPath);
    void ValidateGlobalModel(const std::string& modelID, TaskRequest& requestData);
    virtual void Validate(const std::string& inModelPath, const std::string& outMetricPath);
    void SendModelUpdate(const std::string& modelID, std::string& modelUpdateID, const std::string& config);
    void SendModelValidation(const std::string& modelID, const std::string& metricData, TaskRequest& requestData);
    void SetName(const std::string& name);
    void SetId(const std::string& id);
    void SetChunkSize(std::size_t chunkSize);
    size_t GetChunkSize();

private:
    std::unique_ptr<Connector::Stub> connectorStub_;
    std::unique_ptr<Combiner::Stub> combinerStub_;
    std::unique_ptr<ModelService::Stub> modelserviceStub_;
    std::string name_;
    std::string id_;
    std::size_t chunkSize; // 1 MB by default, change this to suit your needs
};

class FednClient {
public:
    FednClient(std::string configFilePath);
    std::map<std::string, std::string> getCombinerConfig();
    std::shared_ptr<ChannelInterface> setupGrpcChannel(std::map<std::string, std::string> combinerConfig);
    void run(std::shared_ptr<GrpcClient> grpcClient);
    std::shared_ptr<ChannelInterface> getChannel();
    std::shared_ptr<HttpClient> getHttpClient();
    std::shared_ptr<GrpcClient> getGrpcClient();

    void setAuthScheme(std::string authScheme);
    void setHost(std::string host);
    void setInsecure(bool insecure);
    void setProxyHost(std::string proxyHost);
    void setToken(std::string token);

private:
    std::shared_ptr<GrpcClient> grpcClient;
    std::shared_ptr<HttpClient> httpClient;
    std::shared_ptr<ChannelInterface> channel;
    json controllerConfig;
    std::map<std::string, std::string> combinerConfig;
    // json commandLineArguments;

    std::map<std::string, std::string> assign();
    // void readCommandLineArguments(int argc, char** argv);
};

#endif // CLIENTLIB_H