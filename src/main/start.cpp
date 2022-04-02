
#include <iostream>
#include <string>
#include <fstream>

#include <imtjson/value.h>
#include <imtjson/parser.h>
#include "istockapi.h"

#include "backtest2.h"

#include "trader_factory.h"
#include "registrations.h"
///For test
int main(int argc, char **argv) {


	json::enableParsePreciseNumbers = true;
	init_registrations();

	if (argc < 3) {
		std::cerr << "Needs definition (json file) and data (json file)";
	}

	std::string definition_fname = argv[1];
	std::string data_fname = argv[2];

	std::ifstream def_file(definition_fname);
	std::ifstream data_file(data_fname);

	json::Value def = json::Value::fromStream(def_file);
	json::Value data = json::Value::fromStream(data_file);


	Trader_Config_Ex traderCfg;
	traderCfg.parse(def["trader"]);
	json::Value minfo_json = def["minfo"];
	json::Value init_json = def["init"];

	IStockApi::MarketInfo minfo;
	minfo = minfo.fromJSON(minfo_json);

	double assets = init_json["assets"].getNumber();
	double currency = init_json["currency"].getNumber();


	std::vector<double> ddata;
	for (json::Value d: data) ddata.push_back(d.getNumber());

	Backtest bt(traderCfg, minfo, assets, currency);

	bt.start(std::move(ddata), 0);

	std::size_t tradeCnt = 0;
	std::cout << "price,buy_price,buy_amount,buy_error,sell_price,sell_amount,sell_error,position,trade_price,trade_size,trade_dir,norm_profit,equilibrium,spread,dynmult_buy,dynmult_sell,budget_total,budget_extra,error" << std::endl;
	while (bt.next()) {
		const auto &tr = bt.get_trades();
		std::size_t e = tr.length;
		if (e == tradeCnt) e++;
		for (std::size_t tidx = tradeCnt; tidx < e; ++tidx) {
			const auto &bo = bt.get_buy_order();
			const auto &so = bt.get_sell_order();
			std::cout << bt.get_cur_price() << ",";
			if (bo.has_value()) std::cout << bo->size << "," << bo->price << ",";
			else std::cout << ",,";
			std::cout << bt.getBuyErr()<< ",";
			if (so.has_value()) std::cout << so->size << "," << so->price << ",";
			else std::cout << ",,";
			std::cout << bt.getSellErr() << ",";
			std::cout << bt.get_position() << ",";
			const auto &misc = bt.get_misc_data();
			if (tidx >= tr.length) {
				//,"trade_price","trade_size","trade_dir","norm_profit",
				std::cout << ",," << misc.trade_dir << ",,";
			} else {
				std::cout << tr[tidx].price << "," << tr[tidx].size << "," << misc.trade_dir << "," << tr[tidx].norm_profit << ",";
			}
			std::cout << misc.equilibrium << ",";
			std::cout << misc.spread<< ",";
			std::cout << misc.dynmult_buy<< ",";
			std::cout << misc.dynmult_sell<< ",";
			std::cout << misc.budget_total<< ",";
			std::cout << misc.budget_extra<< ",";
			std::cout << bt.getGenErr() << std::endl;
		}
		tradeCnt = tr.length;

	}







}
