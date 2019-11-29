cmake_minimum_required(VERSION 2.8) 
add_compile_options(-std=c++17)
file(GLOB main_SRC "*.cpp" )
file(GLOB main_HDR "*.h" "*.tcc")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/brokers/)

add_executable (coinmate main.cpp proxy.cpp )
target_link_libraries (coinmate LINK_PUBLIC brokers_common simpleServer imtjson ssl crypto stdc++fs pthread)
install(TARGETS coinmate DESTINATION "bin/brokers") 
