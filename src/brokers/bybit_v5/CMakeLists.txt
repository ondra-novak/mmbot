cmake_minimum_required(VERSION 3.0) 


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/brokers/)

add_executable (bybit_v5 
        main.cpp 
        rsa_tools.cpp
        ByBitBrokerV5.cpp
        )
target_link_libraries (bybit_v5 LINK_PUBLIC brokers_common simpleServer imtjson )
