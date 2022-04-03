cmake_minimum_required(VERSION 3.0) 

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/brokers/)

add_executable (gokumarket main.cpp gokumarket.cpp)
target_link_libraries (gokumarket LINK_PUBLIC brokers_common userver imtjson)
