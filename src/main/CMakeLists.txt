cmake_minimum_required(VERSION 2.8) 
add_compile_options(-std=c++17)

add_executable (mmbot  
	abstractExtern.cpp
	authmapper.cpp
	ext_stockapi.cpp
	mtrader.cpp
	istockapi.cpp
	storage.cpp	
	main.cpp
	report.cpp
	webcfg.cpp	
	traders.cpp
	strategy.cpp
	strategy_halfhalf.cpp
	strategy_constantstep.cpp
	strategy_keepvalue.cpp
	strategy_keepvalue2.cpp
	strategy_hypersquare.cpp
	strategy_error_fn.cpp
	strategy_exponencial.cpp
	strategy_sinh.cpp
	strategy_sinh_gen.cpp
	strategy_sinh_val.cpp
	strategy_stairs.cpp
	strategy_hodl_short.cpp
	strategy_keepbalance.cpp
	strategy_gamma.cpp
	strategy_hedge.cpp
	strategy_pile.cpp
	simulator.cpp
	localdailyperfmod.cpp
	extdailyperfmod.cpp
	ext_storage.cpp
	backtest.cpp
	swap_broker.cpp
	emulatedLeverageBroker.cpp
	walletDB.cpp
	random_chart.cpp
	dynmult.cpp
	spread.cpp
	series.cpp
	btstore.cpp
	papertrading.cpp
	../brokers/httpjson.cpp
	)
target_link_libraries (mmbot LINK_PUBLIC simpleServer imtjson )
