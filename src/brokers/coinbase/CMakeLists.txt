cmake_minimum_required(VERSION 3.1) 


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/brokers/)

add_executable (coinbase coinbase_advanced.cpp orderbook.cpp orderlist.cpp)
target_link_libraries (coinbase LINK_PUBLIC brokers_common simpleServer imtjson)
