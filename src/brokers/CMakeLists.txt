cmake_minimum_required(VERSION 2.8) 
add_library (brokers_common api.cpp orderdatadb.cpp)
# target_include_directories (brokers_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

