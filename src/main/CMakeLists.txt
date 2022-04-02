cmake_minimum_required(VERSION 2.8) 
add_compile_options(-std=c++17)

add_executable (mmbot
		start.cpp  
		strategy3.cpp		
		spreadgenerator.cpp
		default_spread_generator.cpp
		registrations.cpp
		dynmult.cpp
		trader_factory.cpp
		strategy_pile.cpp
		istockapi.cpp
		backtest2.cpp
		walletDB.cpp
		trader.cpp
		swap_broker.cpp
		report.cpp
		papertrading.cpp
	)
target_link_libraries (mmbot LINK_PUBLIC simpleServer imtjson )
