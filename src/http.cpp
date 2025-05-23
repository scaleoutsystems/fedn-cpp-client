#include <iostream>
#include <stdlib.h>

#include "../include/fednlib/http.h"
#include "../include/fednlib/utils.h"

/**
 * @brief Constructs a new HttpClient object.
 * 
 * Initializes the HttpClient with the specified API URL and optional token.
 * Also initializes the libcurl library for HTTP requests.
 * 
 * @param apiUrl The base URL of the API to interact with.
 * @param token (Optional) The authentication token for the API.
 * 
 * @throws std::runtime_error If libcurl initialization fails.
 */
HttpClient::HttpClient(const std::string& apiUrl, const std::string& token = "") : apiUrl(apiUrl), token(token) {
    curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Error initializing libcurl." << std::endl;
        std::exit(1);
    }
}

/**
 * @brief Destructor for the HttpClient class.
 *
 * This destructor cleans up the CURL handle if it has been initialized.
 * It ensures that any resources allocated by CURL are properly released.
 */
HttpClient::~HttpClient() {
    if (curl) {
        curl_easy_cleanup(curl);
    }
}

/**
 * @brief Assigns a client to the controller using the provided configuration.
 *
 * This function sends a POST request to the /add_client endpoint of the API,
 * with the client configuration provided in the controllerConfig map. The
 * request body is formatted as JSON and includes the client_id, name, package,
 * and preferred_combiner fields.
 *
 * @param controllerConfig A map containing the client configuration.
 *
 * @return A JSON object containing the response from the server. If an error occurs,
 * an empty JSON object is returned.
 */
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

    // Select HTTP protocol based on insecure flag
    std::string httpProtocol = "https://";
    if (controllerConfig["insecure"] == "true") {
        httpProtocol = "http://";
    }

    // add endpoint api/v1/clients/add to the apiUrl
    const std::string addClientApiUrl = httpProtocol + apiUrl + "/api/v1/clients/add";

    // Set libcurl options for the POST request
    curl_easy_setopt(curl, CURLOPT_URL, addClientApiUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonData.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeHttpResponseToString);
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

    std::cout << "Adding client with REST call to: " + addClientApiUrl << std::endl;
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

/**
 * @brief Retrieves the authentication token.
 * 
 * This method returns the current authentication token stored in the HttpClient instance.
 * 
 * @return std::string The authentication token.
 */
std::string HttpClient::getToken() {
    return token;
}