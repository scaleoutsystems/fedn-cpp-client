#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <map>
#include <yaml-cpp/yaml.h>

#include "nlohmann/json.hpp"

using json = nlohmann::json;

size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output);
void SaveModelToFile(const std::string& modelData, const std::string& modelPath);
void SaveMetricsToFile(const json& metrics, const std::string& metricPath);
std::string LoadModelFromFile(const std::string& modelPath);
json LoadMetricsFromFile(const std::string& metricPath);
void DeleteFileFromDisk(const std::string& path);
std::string generateRandomUUID();
std::map<std::string, std::string> readCombinerConfig(YAML::Node configFile);
std::map<std::string, std::string> readControllerConfig(YAML::Node config);

#endif // UTILS_H