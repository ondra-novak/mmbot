cmake_minimum_required(VERSION 2.8)
project (markettrader)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/)

include_directories(BEFORE src/imtjson/src src/server/src src )
add_compile_options(-std=c++17)
add_compile_options(-Wall -Werror -Wno-noexcept-type)

add_subdirectory (src/imtjson/src/imtjson EXCLUDE_FROM_ALL)
add_subdirectory (src/server/src/simpleServer EXCLUDE_FROM_ALL)
add_subdirectory (src/main)
add_subdirectory (src/coinmate)
add_subdirectory (src/poloniex)

install(DIRECTORY conf DESTINATION ".") 
install(DIRECTORY www DESTINATION ".") 
install(DIRECTORY DESTINATION "data")
install(DIRECTORY DESTINATION "log")
