cmake_minimum_required(VERSION 2.8) 
add_compile_options(-std=c++17)
file(GLOB main_SRC "*.cpp" )
file(GLOB main_HDR "*.h" "*.tcc")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/brokers/)

add_executable (poloniex ${main_SRC} )
target_link_libraries (poloniex LINK_PUBLIC imtjson curlpp ssl crypto curl stdc++fs pthread)
install(TARGETS poloniex DESTINATION "bin/brokers") 
