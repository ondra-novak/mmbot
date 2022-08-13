cmake_minimum_required(VERSION 3.1) 
add_compile_options(-std=c++17)


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/brokers/)

add_executable (simplefx 
	main.cpp
	 tradingengine.cpp 
	 quotedist.cpp
	 quotestream.cpp 	 
	 market.cpp
	 
)
target_link_libraries (simplefx LINK_PUBLIC brokers_common simpleServer imtjson)
