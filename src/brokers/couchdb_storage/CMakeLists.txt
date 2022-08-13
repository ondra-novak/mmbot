cmake_minimum_required(VERSION 3.1) 

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/brokers/)

add_executable (couchdb_storage main.cpp)
target_link_libraries (couchdb_storage LINK_PUBLIC brokers_common simpleServer imtjson)
