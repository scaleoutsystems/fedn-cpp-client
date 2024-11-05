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


/**
 * @brief Constructs a new FednClient object.
 * 
 * This constructor initializes the FednClient by reading the client configuration
 * from the specified YAML file. It sets up the controller and combiner
 * configurations and creates an HTTP client instance with the API URL and token
 * provided in the configuration file.
 * 
 * @param configFilePath The path to the YAML configuration file.
 */
FednClient::FednClient(std::string configFilePath) {
    // Read HTTP configuration from the "client.yaml" file
    YAML::Node config = YAML::LoadFile(configFilePath);
    controllerConfig = readControllerConfig(config);
    combinerConfig = readCombinerConfig(config);

    // Create a Client instance with the API URL and token (if provided)
    httpClient = std::make_shared<HttpClient>(controllerConfig["api_url"], controllerConfig["token"]);
}

/**
 * @brief Retrieves the combiner configuration for the FednClient.
 *
 * This function checks if the "host" entry in the combiner configuration is empty.
 * If it is empty, it assigns a new combiner configuration by calling the assignCombiner() method.
 * The function returns the combiner configuration as a map of string key-value pairs.
 *
 * @return std::map<std::string, std::string> The combiner configuration.
 */
std::map<std::string, std::string> FednClient::getCombinerConfig() {
    #ifdef DEBUG
    std::cout << "DEBUG Combiner configuration PRE-ASSIGNMENT:" << std::endl;
    for (auto const& x : combinerConfig) {
        std::cout << "  " << x.first << ": " << x.second << std::endl;
    }
    #endif
    
    if (combinerConfig["host"].empty()) {
        // Assign to combiner
        combinerConfig = assignCombiner();
        #ifdef DEBUG
        std::cout << "DEBUG Combiner configuration POST-ASSIGNMENT:" << std::endl;
        for (auto const& x : combinerConfig) {
            std::cout << "  " << x.first << ": " << x.second << std::endl;
        }
        #endif
    }
    return combinerConfig;
}

/**
 * @brief Retrieves the channel associated with the FednClient.
 * 
 * This function returns a shared pointer to the ChannelInterface that
 * represents the communication channel used by the FednClient.
 * 
 * @return std::shared_ptr<ChannelInterface> The shared pointer to the channel.
 */
std::shared_ptr<ChannelInterface> FednClient::getChannel() {
    return channel;
}

/**
 * @brief Runs the FednClient with a custom gRPC client.
 * 
 * This function sets the name and ID of the gRPC client based on the controller
 * configuration. It then starts the heart beat thread and listens to model update
 * requests from the combiner.
 * 
 * @param customGrpcClient The custom gRPC client to run.
 */
void FednClient::run(std::shared_ptr<GrpcClient> customGrpcClient) {
    // Add the custom gRPC client to the FednClient object
    grpcClient = customGrpcClient;

    // Set name, id and chunk size
    grpcClient->setName(controllerConfig["name"]);
    grpcClient->setId(controllerConfig["client_id"]);

    // Start heart beat thread and listen to model update requests
    std::thread HeartBeatThread(sendIntervalHeartBeat, grpcClient.get(), 10);
    grpcClient->connectTaskStream();
    HeartBeatThread.join();
}

/**
 * @brief Assigns the client to a combiner.
 * 
 * This function sends an HTTP request to the controller to assign the client to a combiner.
 * It then extracts the host and token from the response and sets the combiner configuration
 * accordingly. The function returns the combiner configuration as a map of string key-value pairs.
 * 
 * @return std::map<std::string, std::string> The combiner configuration.
 */
std::map<std::string, std::string> FednClient::assignCombiner() {
    // Request assignment to combiner
    json httpResponseData = httpClient->assign(controllerConfig);

    // Pretty print of the response
    std::cout << "Response: " << httpResponseData.dump(4) << std::endl;

    if (combinerConfig["insecure"] == "true") {
        // Concatenate host and port for insecure connection
        std::string host = httpResponseData["host"].get<std::string>();
        int port = httpResponseData["port"].get<int>();
        combinerConfig["host"] = host + ":" + std::to_string(port);
    } else {
        // For secure connection, use the host as is
        combinerConfig["host"] = httpResponseData["host"].get<std::string>();
    }

    // Setup gRPC channel configuration
    combinerConfig["proxy_host"] = httpResponseData["fqdn"].is_null() ? "" : httpResponseData["fqdn"].get<std::string>();
    combinerConfig["token"] = httpClient->getToken();

    return combinerConfig;
}

/**
 * @brief Sets up the gRPC channel for the FednClient.
 * 
 * This function sets up the gRPC channel based on the combiner configuration provided.
 * It initializes the credentials based on the "insecure" flag and the "token" and "auth_scheme"
 * values in the combiner configuration. It then creates a channel using the specified host and
 * credentials. If a proxy host is provided, the host is set to the proxy host and the server host
 * is set in the metadata. The function returns a shared pointer to the ChannelInterface.
 * 
 * @param combinerConfig The combiner configuration.
 * @return std::shared_ptr<ChannelInterface> The shared pointer to the channel.
 */
std::shared_ptr<ChannelInterface> FednClient::setupGrpcChannel(std::map<std::string, std::string> combinerConfig) {
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
    args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, 60 * 1000 /*20 sec*/);
    args.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 20 * 1000 /*10 sec*/);
    args.SetInt(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);

    channel = grpc::CreateCustomChannel(combinerConfig["host"], creds, args);

    return channel;
}


/**
 * @brief Retrieves the gRPC client instance.
 *
 * This function returns a shared pointer to the gRPC client instance
 * associated with the FednClient.
 *
 * @return std::shared_ptr<GrpcClient> A shared pointer to the gRPC client.
 */
std::shared_ptr<GrpcClient> FednClient::getGrpcClient() {
    return grpcClient;
}

/**
 * @brief Retrieves the HTTP client instance.
 * 
 * This function returns a shared pointer to the HttpClient instance
 * used by the FednClient.
 * 
 * @return std::shared_ptr<HttpClient> A shared pointer to the HttpClient instance.
 */
std::shared_ptr<HttpClient> FednClient::getHttpClient() {
    return httpClient;
}

/**
 * @brief Sets the authentication scheme for the FednClient.
 * 
 * This method updates the combiner configuration with the specified 
 * authentication scheme.
 * 
 * @param authScheme A string representing the authentication scheme to be set.
 */
void FednClient::setAuthScheme(std::string authScheme) {
    combinerConfig["auth_scheme"] = authScheme;
}

/**
 * @brief Sets the host address for the combiner configuration.
 * 
 * This function updates the combiner configuration with the specified host address.
 * 
 * @param host A string representing the host address to be set in the combiner configuration.
 */
void FednClient::setCombinerHost(std::string host) {
    combinerConfig["host"] = host;
}

/**
 * @brief Sets the insecure mode in the combiner configuration.
 *
 * This method configures the client to operate in insecure mode if the 
 * parameter is set to true.
 *
 * @param insecure A boolean value indicating whether to enable insecure mode.
 *                 - true: Enable insecure mode.
 *                 - false: Disable insecure mode.
 */
void FednClient::setInsecureCombiner(bool insecure) {
    combinerConfig["insecure"] = insecure ? "true" : "false";
}

/**
 * @brief Sets the insecure mode in the controller configuration.
 *
 * This method configures the client to operate in insecure mode for controller
 * communications if the parameter is set to true.
 *
 * @param insecure A boolean value indicating whether to enable insecure mode.
 *                 - true: Enable insecure mode.
 *                 - false: Disable insecure mode.
 */
void FednClient::setInsecureController(bool insecure) {
    controllerConfig["insecure"] = insecure ? "true" : "false";
}

/**
 * @brief Sets the insecure mode for both combiner and controller configurations.
 *
 * This method configures the client to operate in insecure mode for both
 * combiner and controller communications if the parameter is set to true.
 *
 * @param insecure A boolean value indicating whether to enable insecure mode.
 *                 - true: Enable insecure mode for both combiner and controller.
 *                 - false: Disable insecure mode for both combiner and controller.
 */
void FednClient::setInsecure(bool insecure) {
    setInsecureCombiner(insecure);
    setInsecureController(insecure);
}

/**
 * @brief Sets the proxy host for the FednClient.
 * 
 * This function updates the combiner configuration with the specified proxy host.
 * 
 * @param proxyHost The proxy host to be set.
 */
void FednClient::setProxyHost(std::string proxyHost) {
    combinerConfig["proxy_host"] = proxyHost;
}

/**
 * @brief Sets the token for the FednClient.
 * 
 * This method updates the token in both the controllerConfig and combinerConfig
 * with the provided token string.
 * 
 * @param token The token string to be set.
 */
void FednClient::setToken(std::string token) {
    controllerConfig["token"] = token;
    combinerConfig["token"] = token;
}

/**
 * @brief Sets the API URL for the FednClient.
 * 
 * This method updates the controller configuration with the specified API URL.
 * 
 * @param apiUrl The API URL to be set.
 */
void FednClient::setControllerUrl(std::string apiUrl) {
    controllerConfig["api_url"] = apiUrl;
}

/**
 * @brief Sets the client ID for the FednClient.
 * 
 * This method updates the controller configuration with the specified client ID.
 * 
 * @param clientId The client ID to be set.
 */
void FednClient::setClientId(std::string clientId) {
    controllerConfig["client_id"] = clientId;
}

/**
 * @brief Sets the name for the FednClient.
 * 
 * This method updates the controller configuration with the specified name.
 * 
 * @param name The name to be set.
 */
void FednClient::setName(std::string name) {
    controllerConfig["name"] = name;
}

/**
 * @brief Sets the package for the FednClient.
 * 
 * This method updates the controller configuration with the specified package.
 * 
 * @param package The package to be set.
 */
void FednClient::setPackage(std::string package) {
    controllerConfig["package"] = package;
}

/**
 * @brief Sets the preferred combiner for the FednClient.
 * 
 * This method updates the controller configuration with the specified preferred combiner.
 * 
 * @param preferredCombiner The preferred combiner to be set.
 */
void FednClient::setPreferredCombiner(std::string preferredCombiner) {
    controllerConfig["preferred_combiner"] = preferredCombiner;
}