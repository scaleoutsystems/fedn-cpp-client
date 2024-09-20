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

class HttpClient {
public:
    HttpClient(const std::string& apiUrl, const std::string& token);
    ~HttpClient();
    json assign(const json& requestData);

private:
    CURL* curl;
    std::string apiUrl;
    std::string token;
};

class GrpcClient {
public:
    GrpcClient(std::shared_ptr<ChannelInterface> channel);
    void HeartBeat();
    void ConnectModelUpdateStream();
    std::string DownloadModel(const std::string& modelID);
    void UploadModel(std::string& modelID, std::string& modelData, size_t chunkSize);
    void SaveModelToFile(const std::string& modelData, const std::string& modelID, const std::string& path);
    std::string LoadModelFromFile(const std::string& modelID, const std::string& path);
    void DeleteModelFromDisk(const std::string& modelID, const std::string& path);
    virtual void UpdateLocalModel(const std::string& modelID, const std::string& requestData);
    void SendModelUpdate(const std::string& modelID, std::string& modelUpdateID, const std::string& config);
    std::string generateRandomUUID();
    void SetName(const std::string& name);
    void SetId(const std::string& id);

private:
    private:
    std::unique_ptr<Connector::Stub> connectorStub_;
    std::unique_ptr<Combiner::Stub> combinerStub_;
    std::unique_ptr<ModelService::Stub> modelserviceStub_;
    std::string name_;
    std::string id_;
    static const size_t chunkSize = 1024 * 1024; // 1 MB, change this to suit your needs
};

class CppClient {
public:
    CppClient(int argc, char** argv);
    std::shared_ptr<ChannelInterface> setupGrpcChannel();
    void run(std::shared_ptr<GrpcClient> grpcClient);
    std::shared_ptr<ChannelInterface> getChannel();

private:
    std::shared_ptr<GrpcClient> grpcClient;
    std::unique_ptr<HttpClient> httpClient;
    std::shared_ptr<ChannelInterface> channel;
    json httpRequestData;
    json commandLineArguments;

    json assign();
    void createChannel(json grpcChannelConfig);
    void readCommandLineArguments(int argc, char** argv);
};

#endif // CLIENTLIB_H