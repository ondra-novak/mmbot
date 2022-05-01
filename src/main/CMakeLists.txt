cmake_minimum_required(VERSION 2.8) 
add_compile_options(-std=c++17)

add_executable (mmbot
		../brokers/httpjson.cpp
		start.cpp  
		strategy3.cpp		
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
		auth.cpp
		stock_selector.cpp
		simulator.cpp
		ext_stockapi.cpp
		ext_storage.cpp
		abstractExtern.cpp
		traders.cpp
		utilization.cpp
		errhandler.cpp
		core.cpp
		storage.cpp
		extdailyperfmod.cpp
		httpapi.cpp
		httpapi_def.cpp
		ssestream.cpp
		licence.cpp
		server.cpp
		btstore.cpp
		market_info.cpp
	)
target_link_libraries (mmbot LINK_PUBLIC userver imtjson )
