#pragma once
#include <string>
#include <cstdint>

namespace fedn {

// ClientOptions struct to hold client configuration options to be overwritten by command line arguments
struct ClientOptions {
    std::string discover_host = "grpc://localhost:50051";
    std::string token         = "";
    std::string name          = "unnamed";
    std::uint64_t client_id   = 0;
    bool        insecure      = false;
    std::string package       = "local";
    std::string helper_type   = "";  
};

} 