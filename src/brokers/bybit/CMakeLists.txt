cmake_minimum_required(VERSION 3.0) 


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/brokers/)

add_executable (bybit main.cpp bybitbroker.cpp )
target_link_libraries (bybit LINK_PUBLIC brokers_common userver imtjson )
