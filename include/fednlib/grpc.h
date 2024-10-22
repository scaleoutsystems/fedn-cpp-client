#ifndef GRPCCLIENT_H
#define GRPCCLIENT_H

#include <string>
#include <memory>
#include <grpcpp/grpcpp.h>
#include "fedn.grpc.pb.h"
#include "fedn.pb.h"
#include "nlohmann/json.hpp"

using grpc::ChannelInterface;
using fedn::Connector;
using fedn::Combiner;
using fedn::ModelService;
using fedn::TaskRequest;

using json = nlohmann::json;

class GrpcClient {
public:
    GrpcClient(std::shared_ptr<ChannelInterface> channel);
    void heartBeat();
    void connectTaskStream();
    std::string downloadModel(const std::string& modelID);
    void downloadModelToFile(const std::string& modelID, const std::string& modelPath);
    void uploadModel(std::string& modelID, std::string& modelData);
    void uploadModelFromFile(const std::string& modelID, const std::string& modelPath);
    virtual void updateLocalModel(const std::string& modelID, const std::string& requestData);
    virtual void train(const std::string& inModelPath, const std::string& outModelPath);
    void validateGlobalModel(const std::string& modelID, TaskRequest& requestData);
    virtual void validate(const std::string& inModelPath, const std::string& outMetricPath);
    virtual void predict(const std::string& modelPath, const std::string& outputPath);
    void predictGlobalModel(const std::string& modelID, TaskRequest& requestData);
    void sendModelUpdate(const std::string& modelID, std::string& modelUpdateID, const std::string& config);
    void sendModelValidation(const std::string& modelID, json& metricData, TaskRequest& requestData);
    void sendModelPrediction(const std::string& modelID, json& predictionData, TaskRequest& requestData);
    void setName(const std::string& name);
    void setId(const std::string& id);
    void setChunkSize(std::size_t chunkSize);
    size_t getChunkSize();

private:
    std::unique_ptr<Connector::Stub> connectorStub_;
    std::unique_ptr<Combiner::Stub> combinerStub_;
    std::unique_ptr<ModelService::Stub> modelserviceStub_;
    std::string name_;
    std::string id_;
    std::size_t chunkSize; // 1 MB by default, change this to suit your needs
};

void sendIntervalHeartBeat(GrpcClient* client, int intervalSeconds);

#endif // GRPCCLIENT_H