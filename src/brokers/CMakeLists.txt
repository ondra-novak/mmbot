cmake_minimum_required(VERSION 3.1) 
add_library (brokers_common api.cpp orderdatadb.cpp httpjson.cpp)
# target_include_directories (brokers_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

