cmake_minimum_required(VERSION 3.1) 


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/brokers/)

add_executable (coinbase_adv coinbase_advanced.cpp orderbook.cpp orderlist.cpp)
target_link_libraries (coinbase_adv LINK_PUBLIC brokers_common simpleServer imtjson)
