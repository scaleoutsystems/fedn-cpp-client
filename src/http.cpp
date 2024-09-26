#include <iostream>
#include <stdlib.h>

#include "../include/fednlib/http.h"
#include "../include/fednlib/utils.h"

HttpClient::HttpClient(const std::string& apiUrl, const std::string& token = "") : apiUrl(apiUrl), token(token) {
    curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Error initializing libcurl." << std::endl;
        std::exit(1);
    }
}

HttpClient::~HttpClient() {
    if (curl) {
        curl_easy_cleanup(curl);
    }
}

json HttpClient::assign(std::map<std::string, std::string> controllerConfig) {
    // Get request body as JSON
    json requestData = {
        {"client_id", controllerConfig["client_id"]},
        {"name", controllerConfig["name"]},
        {"package", controllerConfig["package"]},
        {"preferred_combiner", controllerConfig["preferred_combiner"]}
    };
    
    // Convert the JSON data to a string
    std::string jsonData = requestData.dump();

    // add endpoint /add_client to the apiUrl
    const std::string addClientApiUrl = apiUrl + "/add_client";

    // Set libcurl options for the POST request
    curl_easy_setopt(curl, CURLOPT_URL, addClientApiUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonData.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    // allow all redirects
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    // Set the Content-Type header
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    // Get Environment variable for token scheme
    char* token_scheme;
    token_scheme = std::getenv("FEDN_AUTH_SCHEME");
    if (token_scheme == NULL) {
        token_scheme = (char*) "Bearer";
    }
    std::string fillString = token_scheme + (std::string) " ";

    // Set the token as a header if it's provided
    if (!token.empty()) {
        headers = curl_slist_append(headers, ("Authorization: " + fillString + token).c_str());
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
    if (statusCode < 200 || statusCode >= 300){
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
        if (!responseData.empty() && responseData[0] == '{') {
            json responseJson = json::parse(responseData);
            return responseJson;
        } else {
            std::cerr << "Invalid or empty response data." << std::endl;
            // Print the response data
            std::cout << responseData << std::endl;
            return json(); // Return an empty JSON object in case of invalid or empty response data
        }
    } catch (const std::exception& e) {
        std::cerr << "Error parsing response JSON: " << e.what() << std::endl;
        return json(); // Return an empty JSON object in case of parsing error
    }
}

std::string HttpClient::getToken() {
    return token;
}