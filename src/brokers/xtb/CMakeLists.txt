cmake_minimum_required(VERSION 3.1)
add_compile_options(-std=c++17)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/brokers/)

add_executable (xtb
    client.cpp
    streaming.cpp
    types.cpp
    position_control.cpp
    assets.cpp
    ratio.cpp
    orders.cpp
    main.cpp
    interface.cpp
)
target_link_libraries (xtb LINK_PUBLIC brokers_common simpleServer imtjson )
