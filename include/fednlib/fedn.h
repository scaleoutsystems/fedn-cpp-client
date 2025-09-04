#ifndef FEDNCLIENT_H
#define FEDNCLIENT_H

#include <string>
#include <memory>
#include <map>
#include <grpcpp/grpcpp.h>

#include "fedn.grpc.pb.h"
#include "fedn.pb.h"
#include "grpc.h"
#include "http.h"

// for the ClientOptions struct
#include "ClientOptions.hpp"    

using grpc::ChannelInterface;

namespace fedn {
class FednClient {
public:
    //FednClient(std::string configFilePath);
    explicit FednClient(const ClientOptions& opts); 
    std::map<std::string, std::string> getCombinerConfig();
    std::shared_ptr<ChannelInterface> setupGrpcChannel(std::map<std::string, std::string> combinerConfig);
    void run(std::shared_ptr<GrpcClient> grpcClient);
    std::shared_ptr<ChannelInterface> getChannel();
    std::shared_ptr<HttpClient> getHttpClient();
    std::shared_ptr<GrpcClient> getGrpcClient();

    void setAuthScheme(std::string authScheme);
    void setCombinerHost(std::string host);
    void setInsecureCombiner(bool insecure);
    void setInsecureController(bool insecure);
    void setInsecure(bool insecure);
    void setProxyHost(std::string proxyHost);
    void setToken(std::string token);
    void setControllerUrl(std::string apiUrl);
    void setClientId(std::string clientId);
    void setName(std::string name);
    void setPackage(std::string package);
    void setPreferredCombiner(std::string preferredCombiner);
    // asynchronous methods
    bool try_connect_once();       
    void tick_online(std::shared_ptr<GrpcClient> grpc);
    
private:
    ClientOptions opts_;
    std::shared_ptr<GrpcClient> grpcClient;
    std::shared_ptr<HttpClient> httpClient;
    std::shared_ptr<ChannelInterface> channel;
    std::map<std::string, std::string> controllerConfig;
    std::map<std::string, std::string> combinerConfig;

    std::map<std::string, std::string> assignCombiner();
};
}

#endif // FEDNCLIENT_H