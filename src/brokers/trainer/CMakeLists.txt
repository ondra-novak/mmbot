cmake_minimum_required(VERSION 2.8) 
add_compile_options(-std=c++17)


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/brokers/)

add_custom_command(OUTPUT ${CMAKE_CURRENT_LIST_DIR}/generated/index.html.cpp COMMAND ${CMAKE_CURRENT_LIST_DIR}/txt2cpp.sh index.html MAIN_DEPENDENCY ${CMAKE_CURRENT_LIST_DIR}/index.html)

add_executable (trainer cryptowatch.cpp main.cpp ../bitfinex/structs.cpp ${CMAKE_CURRENT_LIST_DIR}/generated/index.html.cpp)
target_link_libraries (trainer LINK_PUBLIC brokers_common imtjson simpleServer)
