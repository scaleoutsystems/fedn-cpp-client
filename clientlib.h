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

class HttpClient {
public:
    HttpClient(const std::string& apiUrl, const std::string& token = "");
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
    virtual void UpdateLocalModel(const std::string& modelID, const std::string& requestData) = 0;
    void HeartBeat();
    void ConnectModelUpdateStream();
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
    json readCommandLineArguments(int argc, char** argv);
};

#endif // CLIENTLIB_H