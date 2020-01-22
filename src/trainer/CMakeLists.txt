cmake_minimum_required(VERSION 2.8) 
add_compile_options(-std=c++17)


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/brokers/)

add_custom_command(OUTPUT generated/index.html.cpp COMMAND ./txt2cpp.sh index.html MAIN_DEPENDENCY index.html)

add_executable (trainer main.cpp generated/index.html.cpp)
target_link_libraries (trainer LINK_PUBLIC brokers_common imtjson simpleServer)
install(TARGETS trainer DESTINATION "bin/trainer") 
