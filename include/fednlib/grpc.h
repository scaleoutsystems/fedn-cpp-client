#ifndef GRPCCLIENT_H
#define GRPCCLIENT_H

#include <string>
#include <memory>
#include <grpcpp/grpcpp.h>
#include "fedn.grpc.pb.h"
#include "fedn.pb.h"

using grpc::ChannelInterface;
using fedn::Connector;
using fedn::Combiner;
using fedn::ModelService;
using fedn::TaskRequest;

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

void SendIntervalHeartBeat(GrpcClient* client, int intervalSeconds);

#endif // GRPCCLIENT_H