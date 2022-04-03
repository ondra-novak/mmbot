cmake_minimum_required(VERSION 3.0) 

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/brokers/)

add_executable (kucoin main.cpp kucoin.cpp)
target_link_libraries (kucoin LINK_PUBLIC brokers_common userver imtjson)
