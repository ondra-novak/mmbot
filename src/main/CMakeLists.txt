cmake_minimum_required(VERSION 2.8) 
add_compile_options(-std=c++17)

add_executable (mmbot  
	abstractExtern.cpp
	authmapper.cpp
	ext_stockapi.cpp
	mtrader.cpp
	spread_calc.cpp
	calculator.cpp
	istockapi.cpp
	ordergen.cpp
	storage.cpp
	emulator.cpp
	main.cpp
	report.cpp
	webcfg.cpp
	spawn.cpp
	backtest_broker.cpp
	backtest.cpp
	traders.cpp
	)
target_link_libraries (mmbot LINK_PUBLIC simpleServer imtjson curlpp ssl crypto curl stdc++fs pthread)
install(TARGETS mmbot DESTINATION "bin") 
