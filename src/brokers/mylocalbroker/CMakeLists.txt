cmake_minimum_required(VERSION 3.0) 

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/brokers/)

add_executable (mylocalbroker main.cpp mylocalbroker.cpp)
target_link_libraries (mylocalbroker LINK_PUBLIC brokers_common simpleServer imtjson)
