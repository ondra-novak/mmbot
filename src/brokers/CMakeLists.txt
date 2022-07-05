cmake_minimum_required(VERSION 2.8) 

# brokers_common is for brokers compatible with version 1 of the api

# brokers_v2_common is for brokers which doesn't support version 1 of the api

add_library (brokers_common api.cpp api_v1.cpp orderdatadb.cpp httpjson.cpp)
add_library (brokers_v2_common api.cpp api_v2.cpp orderdatadb.cpp httpjson.cpp)



