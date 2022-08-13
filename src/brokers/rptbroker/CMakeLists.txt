cmake_minimum_required(VERSION 3.1) 

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/brokers/)

add_executable (rptbroker main.cpp database.cpp trade_report.cpp)
target_link_libraries (rptbroker LINK_PUBLIC brokers_common simpleServer imtjson)
