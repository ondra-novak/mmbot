cmake_minimum_required(VERSION 2.8) 
add_compile_options(-std=c++17)


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/brokers/)

add_executable (bitmex main.cpp proxy.cpp )
target_link_libraries (bitmex LINK_PUBLIC brokers_common imtjson curlpp ssl crypto curl stdc++fs pthread)
install(TARGETS bitmex DESTINATION "bin/brokers") 
