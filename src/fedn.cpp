#include <iostream>
#include <yaml-cpp/yaml.h>
#include <nlohmann/json.hpp>
#include <thread>

#include "../include/fednlib/fedn.h"
#include "../include/fednlib/utils.h"

using json = nlohmann::json;

class MyCustomAuthenticator : public grpc::MetadataCredentialsPlugin {
public:
    MyCustomAuthenticator(const grpc::string& ticket) : ticket_(ticket) {}

    grpc::Status GetMetadata(
        grpc::string_ref service_url, grpc::string_ref method_name,
        const grpc::AuthContext& channel_auth_context,
        std::multimap<grpc::string, grpc::string>* metadata) override {
        metadata->insert(std::make_pair("authorization", ticket_));
        return grpc::Status::OK;
    }

private:
    grpc::string ticket_;
};

class CustomMetadata : public grpc::MetadataCredentialsPlugin {
public:
    CustomMetadata(const grpc::string& key, const grpc::string& value)
        : key_(key), value_(value) {}

    grpc::Status GetMetadata(
        grpc::string_ref service_url, grpc::string_ref method_name,
        const grpc::AuthContext& channel_auth_context,
        std::multimap<grpc::string, grpc::string>* metadata) override {
        metadata->insert(std::make_pair(key_, value_));
        return grpc::Status::OK;
    }

private:
    grpc::string key_;
    grpc::string value_;
};

FednClient::FednClient(std::string configFilePath) {
    // Read HTTP configuration from the "client.yaml" file
    YAML::Node config = YAML::LoadFile(configFilePath);
    controllerConfig = readControllerConfig(config);
    combinerConfig = readCombinerConfig(config);

    // Create a Client instance with the API URL and token (if provided)
    httpClient = std::make_shared<HttpClient>(controllerConfig["api_url"], controllerConfig["token"]);
}

std::map<std::string, std::string> FednClient::getCombinerConfig() {
    #ifdef DEBUG
    std::cout << "DEBUG Combiner configuration PRE-ASSIGNMENT:" << std::endl;
    for (auto const& x : combinerConfig) {
        std::cout << "  " << x.first << ": " << x.second << std::endl;
    }
    #endif
    
    if (combinerConfig["host"].empty()) {
        // Assign to combiner
        combinerConfig = assign();
        #ifdef DEBUG
        std::cout << "DEBUG Combiner configuration POST-ASSIGNMENT:" << std::endl;
        for (auto const& x : combinerConfig) {
            std::cout << "  " << x.first << ": " << x.second << std::endl;
        }
        #endif
    }
    return combinerConfig;
}

std::shared_ptr<ChannelInterface> FednClient::getChannel() {
    return channel;
}

void FednClient::run(std::shared_ptr<GrpcClient> customGrpcClient) {
    // Add the custom gRPC client to the FednClient object
    grpcClient = customGrpcClient;

    // Set name, id and chunk size
    grpcClient->SetName(controllerConfig["name"]);
    grpcClient->SetId(controllerConfig["client_id"]);

    // Start heart beat thread and listen to model update requests
    std::thread HeartBeatThread(SendIntervalHeartBeat, grpcClient.get(), 10);
    grpcClient->ConnectTaskStream();
    HeartBeatThread.join();
}

std::map<std::string, std::string> FednClient::assign() {
    // Request assignment to combiner
    json httpResponseData = httpClient->assign(controllerConfig);

    // Pretty print of the response
    std::cout << "Response: " << httpResponseData.dump(4) << std::endl;

    // Setup gRPC channel configuration
    combinerConfig["host"] = httpResponseData["host"];
    combinerConfig["proxy_host"] = httpResponseData["fqdn"];
    combinerConfig["token"] = httpClient->getToken();

    return combinerConfig;
}

std::shared_ptr<ChannelInterface> FednClient::setupGrpcChannel(std::map<std::string, std::string> combinerConfig) {
    // Instantiate the client. It requires a channel, out of which the actual RPCs
    // are created. This channel models a connection to an server specified by
    // the argument "--target=".

    std::cout << "Server host: " << combinerConfig["host"] << std::endl;

    // initialize credentials
    std::shared_ptr<grpc::ChannelCredentials> creds;
    if (combinerConfig["insecure"] == "true") {
        std::cout << "Using insecure channel" << std::endl;
        // Create an call credentials object for use with an insecure channel
        creds = grpc::InsecureChannelCredentials();
    } else {
        std::cout << "Using secure channel" << std::endl;
        // check if token is empty
        if (combinerConfig["token"].empty()) {
            throw std::runtime_error("Token is empty, exiting...");
        }
        // check if auth_scheme is empty
        if (combinerConfig["auth_scheme"].empty()) {
            throw std::runtime_error("Auth scheme is empty, exiting...");
        }
        // Check if auth_scheme is Token or Bearer
        if (combinerConfig["auth_scheme"] != "Token" && combinerConfig["auth_scheme"] != "Bearer") {
            throw std::runtime_error("Invalid auth scheme, exiting...");
        }
        // Create authorization header value string for metadata
        std::string header_value = combinerConfig["auth_scheme"] + (std::string) " " + combinerConfig["token"];

        // Create call credentials
        auto call_creds = grpc::MetadataCredentialsFromPlugin(
        std::unique_ptr<grpc::MetadataCredentialsPlugin>(
            new MyCustomAuthenticator(header_value)));

        // Create channel credentials
        auto channel_creds = grpc::SslCredentials(grpc::SslCredentialsOptions());

        // Get the server host to metadata
        auto metadata_creds = grpc::MetadataCredentialsFromPlugin(
        std::unique_ptr<grpc::MetadataCredentialsPlugin>(
            new CustomMetadata("grpc-server", combinerConfig["host"])));

        // Create intermediate composite channel credentials
        auto inter_creds = grpc::CompositeChannelCredentials(channel_creds, call_creds);
        // Create composite channel credentials
        creds = grpc::CompositeChannelCredentials(inter_creds, metadata_creds);
    }
    // Check if proxy host is set, and change host to proxy host, server host will be in metadata
    if (!combinerConfig["proxy_host"].empty()) {
        std::cout << "Proxy host: " << combinerConfig["proxy_host"] << std::endl;
        combinerConfig["host"] = combinerConfig["proxy_host"];
    }
    //Create a channel using the credentials created above
    grpc::ChannelArguments args;

    // Sample way of setting keepalive arguments on the client channel. Here we
    // are configuring a keepalive time period of 20 seconds, with a timeout of 10
    // seconds. Additionally, pings will be sent even if there are no calls in
    // flight on an active connection.
    args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, 20 * 1000 /*20 sec*/);
    args.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 10 * 1000 /*10 sec*/);
    args.SetInt(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);

    channel = grpc::CreateCustomChannel(combinerConfig["host"], creds, args);

    return channel;
}

std::shared_ptr<GrpcClient> FednClient::getGrpcClient() {
    return grpcClient;
}

std::shared_ptr<HttpClient> FednClient::getHttpClient() {
    return httpClient;
}

void FednClient::setAuthScheme(std::string authScheme) {
    combinerConfig["auth_scheme"] = authScheme;
}

void FednClient::setCombinerHost(std::string host) {
    combinerConfig["host"] = host;
}

void FednClient::setInsecure(bool insecure) {
    combinerConfig["insecure"] = insecure ? "true" : "false";
}

void FednClient::setProxyHost(std::string proxyHost) {
    combinerConfig["proxy_host"] = proxyHost;
}

void FednClient::setToken(std::string token) {
    controllerConfig["token"] = token;
    combinerConfig["token"] = token;
}