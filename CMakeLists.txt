cmake_minimum_required(VERSION 2.8)
project (mmbot)

EXEC_PROGRAM(uname OUTPUT_VARIABLE SYSTEM_NAME)
SET(SYSTEM_NAME "${SYSTEM_NAME}" CACHE INTERNAL "")

IF(SYSTEM_NAME STREQUAL "Linux")
   MESSAGE("-- Detected Linux system")
   LINK_LIBRARIES( stdc++fs)
ENDIF(SYSTEM_NAME STREQUAL "Linux")

IF(SYSTEM_NAME STREQUAL "FreeBSD")
   MESSAGE("-- Detected FreeBSD system")
   LINK_LIBRARIES(execinfo)   
ENDIF(SYSTEM_NAME STREQUAL "FreeBSD")
LINK_LIBRARIES(ssl crypto pthread)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/)

include_directories(BEFORE src/imtjson/src src/server/src src )
add_compile_options(-std=c++17)
add_compile_options(-Wall -Wno-noexcept-type)

add_subdirectory (src/imtjson/src/imtjson EXCLUDE_FROM_ALL)
add_subdirectory (src/server/src/simpleServer EXCLUDE_FROM_ALL)
add_subdirectory (src/brokers EXCLUDE_FROM_ALL)
add_subdirectory (src/main)
add_subdirectory (src/binance)
add_subdirectory (src/bitfinex)
add_subdirectory (src/bitmex)
add_subdirectory (src/coinbase)
add_subdirectory (src/coingi)
add_subdirectory (src/coinmate)
add_subdirectory (src/deribit)
add_subdirectory (src/poloniex)
add_subdirectory (src/simplefx)
add_subdirectory (src/trainer)


install(DIRECTORY conf DESTINATION ".") 
install(DIRECTORY www DESTINATION ".") 
install(DIRECTORY DESTINATION "data")
install(DIRECTORY DESTINATION "log")
