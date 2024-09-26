#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <random>

#include "../include/fednlib/utils.h"

/**
 * Callback for handling data received from a network request.
 * It appends the received HTTP response data to the provided output string.
 * 
 * @param contents Pointer to the data received.
 * @param size Size of each data element.
 * @param nmemb Number of data elements.
 * @param output Pointer to the string where the received data will be appended.
 * @return The total size of the data processed (size * nmemb).
 */
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t totalSize = size * nmemb;
    output->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

/**
 * Save model to file.
 *
 * @param modelData The model data to save.
 * @param modelID The model ID to save. Will be used as filename.
 * @param path The path to save the model to.
 */
void SaveModelToFile(const std::string& modelData, const std::string& modelPath) {
    // Write the binary string to a file
    // Create an ofstream object and open the file in binary mode
    std::ofstream outFile(modelPath, std::ios::binary);

    // Check if the file was opened successfully
    if (!outFile) {
        std::cerr << "Error opening file for writing" << std::endl;
    }

    // Write the binary string to the file
    outFile.write(modelData.c_str(), modelData.size());

    // Close the file
    outFile.close();

    std::cout << "modelData saved to file " << modelPath << " successfully" << std::endl;
}

/**
 * Save metrics to file.
 * 
 * @param metrics The metrics to save, as a JSON object.
 * @param metricPath The path to save the metrics to.
 */
void SaveMetricsToFile(const json& metrics, const std::string& metricPath) {
    // Create an ofstream object and open the file in binary mode
    std::ofstream outFile(metricPath);
    // Check if the file was opened successfully
    if (!outFile) {
        std::cerr << "Error opening file for writing" << std::endl;
    }
    // Write the json to the file
    outFile << metrics.dump(4);
    // Close the file
    outFile.close();
    std::cout << "Metrics saved to file " << metricPath << " successfully" << std::endl;
}

/**
 * Load model from file.
 *
 * @param modelID The model ID to load. Will be used as filename.
 * @param path The path to load the model from.
 * @return String containing binary data of the model.
 */
std::string LoadModelFromFile(const std::string& modelPath) {
    // Create an ifstream object and open the file in binary mode
    std::ifstream inFile(modelPath, std::ios::binary);
    // Check if the file was opened successfully
    if (!inFile) {
        std::cerr << "Error opening file " << modelPath << " for reading" << std::endl;
    }
    // Get the length of the file
    inFile.seekg(0, inFile.end);
    int length = inFile.tellg();
    inFile.seekg(0, inFile.beg);
    // Create a string object to hold the data
    std::string data(length, '\0');
    // Read the file
    inFile.read(&data[0], length);
    // Close the file
    inFile.close();
    // Return the data
    return data;
}

/**
 * Load metrics from file.
 * 
 * @param metricPath The path to load the metrics from.
 * @return JSON object containing the metrics.
 */
json LoadMetricsFromFile(const std::string& metricPath) {
    // Create an ifstream object and open the file as json
    std::ifstream inFile(metricPath);
    // Check if the file was opened successfully
    if (!inFile) {
        std::cerr << "Error opening file " << metricPath << " for reading" << std::endl;
    }
    // Create a json object to hold the data
    json metrics;
    try {
        metrics = json::parse(inFile);
    } catch (const nlohmann::detail::type_error e) {
        inFile.close();
        std::cerr << "Error parsing metrics JSON: " << e.what() << std::endl;
    }
    // Close the file
    inFile.close();
    // Return the data
    std::cout << "Metrics loaded from file " << metricPath << " successfully" << std::endl;
    return metrics;
}

/**
 * Delete file from disk.
 *
 * @param path The path to delete the model from.
 */
void DeleteFileFromDisk(const std::string& path) {
    // Delete the file
    if (remove((path).c_str()) != 0) {
        std::cerr << "Error deleting file" << std::endl;
    }
    else {
        std::cout << "File deleted successfully: " << path << std::endl;
    }
}

/**
 * Generate a random UUID version 4 string.
 *
 * @return A random UUID version 4 string.
 */
std::string generateRandomUUID() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);

    std::stringstream ss;
    for (int i = 0; i < 32; ++i) {
        if (i == 8 || i == 12 || i == 16 || i == 20) {
            ss << '-';
        }
        int randomValue = dis(gen);
        ss << std::hex << randomValue;
    }

    return ss.str();
}

std::map<std::string, std::string> readCombinerConfig(YAML::Node configFile) {
    // Read HTTP configuration from the "client.yaml" file
    std::map<std::string, std::string> combinerConfig;

    if (configFile["combiner"]) {
        combinerConfig["host"] = configFile["combiner"].as<std::string>();
    }
    if (configFile["proxy_server"]) {
        combinerConfig["proxy_host"] = configFile["proxy_server"].as<std::string>();
    }
    if (configFile["insecure"]) {
        combinerConfig["insecure"] = configFile["insecure"].as<std::string>();
    }
    else {
        combinerConfig["insecure"] = "false";
    }
    if (configFile["token"]) {
        combinerConfig["token"] = configFile["token"].as<std::string>();
    }
    if (configFile["auth_scheme"]) {
        combinerConfig["auth_scheme"] = configFile["auth_scheme"].as<std::string>();
    }
    else {
        combinerConfig["auth_scheme"] = "Bearer";
    }

    return combinerConfig;
}

std::map<std::string, std::string> readControllerConfig(YAML::Node config) {
    // Read requestData from the config
    std::cout << "Reading HTTP request data from config file" << std::endl;
    std::map<std::string, std::string> controllerConfig;

    // Check if there is a valid "discover_host" key in the config
    std::string apiUrl = config["discover_host"].as<std::string>();
    if (apiUrl.empty()) {
        throw std::runtime_error("Invalid discover_host.");
    }
    controllerConfig["api_url"] = "https://" + apiUrl;

    // Check if there is a "token" key in the config file
    std::string token = "";
    if (config["token"]) {
        token = config["token"].as<std::string>();
        // Check if the "token" value is valid
        if (token.empty()) {
            throw std::runtime_error("Invalid token.");
        }
    }
    controllerConfig["token"] = token;

    controllerConfig["client_id"] = config["client_id"].as<std::string>();
    controllerConfig["name"] = config["name"].as<std::string>();
    if (config["package"]) {
        controllerConfig["package"] = config["package"].as<std::string>();
    } else {
        controllerConfig["package"] = "remote";
    }
    
    // Check if preferred_combiner is in the config, else use default empty string
    if (config["preferred_combiner"]) {
        controllerConfig["preferred_combiner"] = config["preferred_combiner"].as<std::string>();
    } else {
        controllerConfig["preferred_combiner"] = "";
    }

    return controllerConfig;
}