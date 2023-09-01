cmake_minimum_required(VERSION 3.10)
project(fedn-cpp VERSION 1.0 DESCRIPTION "FEDn client in C++")

# Find required dependencies
find_package(CURL REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(nlohmann_json REQUIRED)

include_directories(${YAML_CPP_INCLUDE_DIRS})

# Define the source files for your project
set(SOURCES
    client.cpp
)

# Create an executable target
add_executable(fedn-cpp ${SOURCES})

# Link dependencies to the target
target_link_libraries(fedn-cpp PRIVATE CURL::libcurl yaml-cpp ${YAML_CPP_LIBRARIES} nlohmann_json::nlohmann_json)