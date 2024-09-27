#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <random>

#include "../include/fednlib/utils.h"

/**
 * @brief Callback function for writing received data to a string.
 *
 * This function is used as a callback for handling data received from a HTTP request.
 * It appends the received data to the provided output string.
 *
 * @param contents Pointer to the data received.
 * @param size Size of each data element.
 * @param nmemb Number of data elements.
 * @param output Pointer to the string where the received data will be appended.
 * @return The total size of the data processed (size * nmemb).
 */
size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t totalSize = size * nmemb;
    output->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

/**
 * @brief Saves the given model data to a file at the specified path.
 *
 * This function writes the binary string representation of the model data
 * to a file. It opens the file in binary mode and writes the data to it.
 * If the file cannot be opened, an error message is printed to the standard error.
 * Upon successful completion, a success message is printed to the standard output.
 *
 * @param modelData The binary string representation of the model data to be saved.
 * @param modelPath The file path where the model data should be saved.
 */
void saveModelToFile(const std::string& modelData, const std::string& modelPath) {
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
 * @brief Saves the provided JSON metrics to a file.
 *
 * This function takes a JSON object containing metrics and writes it to a specified file path.
 *
 * @param metrics The JSON object containing the metrics to be saved.
 * @param metricPath The file path where the metrics should be saved.
 * 
 * @note The JSON object must be an object and not an array.
 */
void saveMetricsToFile(const json& metrics, const std::string& metricPath) {
    // Create an ofstream object and open the file in binary mode
    // Ensure that metrics is a json object and not an array
    if (!metrics.is_object()) {
        std::cerr << "WARNING: Metrics must be a JSON object, arrays are not allowed." << std::endl;
    }


    std::ofstream outFile(metricPath);
    // Check if the file was opened successfully
    if (!outFile) {
        std::cerr << "Error opening file for writing" << std::endl;
    }
    // Write the json to the file
    outFile << metrics.dump();
    // Close the file
    outFile.close();
    std::cout << "Metrics saved to file " << metricPath << " successfully" << std::endl;
}

/**
 * @brief Loads the content of a model file into a string.
 *
 * This function reads the entire content of a file specified by the given 
 * file path and returns it as a string. The file is read in binary mode.
 *
 * @param modelPath The path to the model file to be loaded.
 * @return A string containing the content of the model file.
 */
std::string loadModelFromFile(const std::string& modelPath) {
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
 * @brief Loads metrics from a JSON file.
 *
 * This function reads a JSON file from the specified path and parses its contents
 * into a json object. If the file cannot be opened or the JSON parsing fails, 
 * appropriate error messages are printed to the standard error output.
 *
 * @param metricPath The path to the JSON file containing the metrics.
 * @return A json object containing the parsed metrics data.
 */
json loadMetricsFromFile(const std::string& metricPath) {
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
 * @brief Deletes a file from the disk at the specified path.
 * 
 * This function attempts to delete the file located at the given path.
 * If the file is successfully deleted, a success message is printed to the console.
 * If the file cannot be deleted, an error message is printed to the console.
 * 
 * @param path The path to the file that needs to be deleted.
 */
void deleteFileFromDisk(const std::string& path) {
    // Delete the file
    if (remove((path).c_str()) != 0) {
        std::cerr << "Error deleting file" << std::endl;
    }
    else {
        std::cout << "File deleted successfully: " << path << std::endl;
    }
}

/**
 * @brief Generates a random UUID (Universally Unique Identifier).
 *
 * This function creates a random UUID in the format xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx,
 * where each 'x' is a hexadecimal digit (0-9, a-f). The UUID is generated using a 
 * random number generator.
 *
 * @return A string representing the generated UUID.
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

/**
 * @brief Reads the combiner configuration from a YAML configuration file.
 *
 * This function extracts various configuration parameters related to the combiner
 * from the provided YAML node and stores them in a map. The expected parameters
 * include "combiner", "proxy_server", "insecure", "token", and "auth_scheme".
 * Default values are provided for "insecure" (false) and "auth_scheme" (Bearer)
 * if they are not specified in the configuration file.
 *
 * @param configFile A YAML::Node object representing the configuration file.
 * @return A map containing the combiner configuration parameters as key-value pairs.
 */
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

/**
 * @brief Reads the controller configuration from a YAML node.
 *
 * This function extracts various configuration parameters from the provided
 * YAML node and stores them in a map. The configuration parameters include
 * API URL, token, client ID, name, package, and preferred combiner.
 *
 * @param config The YAML node containing the configuration data.
 * @return A map containing the configuration parameters as key-value pairs.
 * @throws std::runtime_error if the "discover_host" or "token" values are invalid.
 */
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