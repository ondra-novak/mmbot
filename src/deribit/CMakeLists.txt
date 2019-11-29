cmake_minimum_required(VERSION 2.8) 
add_compile_options(-std=c++17)


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/brokers/)

add_executable (deribit main.cpp proxy.cpp )
target_link_libraries (deribit LINK_PUBLIC brokers_common simpleServer imtjson  ssl crypto  stdc++fs pthread)
install(TARGETS deribit DESTINATION "bin/brokers") 
