cmake_minimum_required(VERSION 2.8) 
add_compile_options(-std=c++17)
file(GLOB main_SRC "*.cpp" )
file(GLOB main_HDR "*.h" "*.tcc")

add_executable (markettrader ${main_SRC} )
target_link_libraries (markettrader LINK_PUBLIC simpleServer imtjson curlpp ssl crypto curl stdc++fs pthread)
install(TARGETS markettrader DESTINATION "bin") 
