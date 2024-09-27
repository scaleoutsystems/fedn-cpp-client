#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <map>
#include <yaml-cpp/yaml.h>

#include "nlohmann/json.hpp"

using json = nlohmann::json;

size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* output);
void saveModelToFile(const std::string& modelData, const std::string& modelPath);
void saveMetricsToFile(const json& metrics, const std::string& metricPath);
std::string loadModelFromFile(const std::string& modelPath);
json loadMetricsFromFile(const std::string& metricPath);
void deleteFileFromDisk(const std::string& path);
std::string generateRandomUUID();
std::map<std::string, std::string> readCombinerConfig(YAML::Node configFile);
std::map<std::string, std::string> readControllerConfig(YAML::Node config);

#endif // UTILS_H