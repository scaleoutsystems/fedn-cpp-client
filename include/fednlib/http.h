#ifndef HTTPCLIENT_H
#define HTTPCLIENT_H

#include <string>
#include <map>
#include <curl/curl.h>
#include "nlohmann/json.hpp"

using json = nlohmann::json;

class HttpClient {
public:
    HttpClient(const std::string& apiUrl, const std::string& token);
    ~HttpClient();
    json assign(std::map<std::string, std::string> controllerConfig);
    std::string getToken();

private:
    CURL* curl;
    std::string apiUrl;
    std::string token;
};

#endif // HTTPCLIENT_H