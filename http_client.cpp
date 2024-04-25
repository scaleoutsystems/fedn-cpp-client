#include <iostream>
#include <string>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

using json = nlohmann::json;

// Callback function to write HTTP response data into a string
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t totalSize = size * nmemb;
    output->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

class HttpClient {
public:
    HttpClient(const std::string& apiUrl, const std::string& token = "") : apiUrl(apiUrl), token(token) {
        curl = curl_easy_init();
        if (!curl) {
            std::cerr << "Error initializing libcurl." << std::endl;
            std::exit(1);
        }
    }

    ~HttpClient() {
        if (curl) {
            curl_easy_cleanup(curl);
        }
    }

    json assign(const json& requestData) {
        // Convert the JSON data to a string
        std::string jsonData = requestData.dump();

        // add endpoint /add_client to the apiUrl
        apiUrl += "/add_client";

        // Set libcurl options for the POST request
        curl_easy_setopt(curl, CURLOPT_URL, apiUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonData.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);

        // Set the Content-Type header
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        // Set the token as a header if it's provided
        if (!token.empty()) {
            headers = curl_slist_append(headers, ("Authorization: Token " + token).c_str());
        }

        // Set the headers for the POST request
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // Response data will be stored here
        std::string responseData;

        // Set the response data string as the write callback parameter
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseData);

        // Perform the HTTP POST request
        CURLcode res = curl_easy_perform(curl);

        // Get status code
        long statusCode;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);

        // Check for HTTP errors
        if (statusCode < 200 || statusCode >= 300) {
            std::cerr << "HTTP error: " << statusCode << std::endl;
            return json(); // Return an empty JSON object in case of an error
        }

        // Check for errors
        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            return json(); // Return an empty JSON object in case of an error
        }

        // Parse and return the response JSON
        try {
            json responseJson = json::parse(responseData);
            return responseJson;
        } catch (const std::exception& e) {
            std::cerr << "Error parsing response JSON: " << e.what() << std::endl;
            return json(); // Return an empty JSON object in case of parsing error
        }
    }

private:
    CURL* curl;
    std::string apiUrl;
    std::string token;
};

int main() {
    // Read configuration from the "client.yaml" file
    const std::string configFile = "../client.yaml";
    YAML::Node config = YAML::LoadFile(configFile);

    // Read API endpoint URL from the config
    const std::string apiUrl = config["discover_address"].as<std::string>();

    // Check if the API URL is valid
    if (apiUrl.empty()) {
        std::cerr << "Invalid API URL." << std::endl;
        return 1;
    }
     
    // Read requestData from the config
    json requestData;
    // Get client_id from the config
    requestData["client_id"] = config["name"].as<std::string>();

    // Check if the client_id is valid
    if (!requestData["client_id"].is_string()) {
        std::cerr << "Invalid client_id." << std::endl;
        return 1;
    }
    
    // Check if preferred_combiner is in the config, else use default empty string
    if (config["preferred_combiner"]) {
        requestData["preferred_combiner"] = config["preferred_combiner"].as<std::string>();
        // Check if the preferred_combiner is valid
        if (!requestData["preferred_combiner"].is_string()) {
            std::cerr << "Invalid preferred_combiner." << std::endl;
            return 1;
        }
    } else {
        requestData["preferred_combiner"] = "";
    }


    // Check if there is a "token" key in the config
    std::string token;
    if (config["token"]) {
        token = config["token"].as<std::string>();
        // Check if the token is valid
        if (!token.empty() && !requestData["token"].is_string()) {
            std::cerr << "Invalid token." << std::endl;
            return 1;
        }
    }

    // Create a Client instance with the API URL and token (if provided)
    HttpClient client(apiUrl, token);

    // Send the POST request and get the response JSON
    json responseJson = client.assign(requestData);

    // Check if the response JSON is empty
    if (responseJson.empty()) {
        std::cerr << "Error: empty response JSON." << std::endl;
        return 1;
    }

    


    // Print the response JSON
    std::cout << "Received combiner assignment:" << std::endl;
    std::cout << responseJson.dump(4) << std::endl;

    return 0;
}