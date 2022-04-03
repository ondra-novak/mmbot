cmake_minimum_required(VERSION 2.8) 
add_compile_options(-std=c++17)


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/brokers/)

add_executable (ftx main.cpp
					interface.cpp					
)
target_link_libraries (ftx LINK_PUBLIC brokers_common userver imtjson )
