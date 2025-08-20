cmake_minimum_required(VERSION 3.10)
project(my-example VERSION 1.0 DESCRIPTION "Example using fednlib")


# Add the path to the main project
set(FEDN_PROJECT_DIR ${CMAKE_SOURCE_DIR}/../..)


# Add the main project as a dependency
add_subdirectory(${FEDN_PROJECT_DIR} fednlib_build)

# Add the example executable
add_executable(attribute_client main.cpp)

# Link the example with the library
target_link_libraries(attribute_client fednlib)