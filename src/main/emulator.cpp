/*
 * emulator.cpp
 *
 *  Created on: 2. 6. 2019
 *      Author: ondra
 */


#include "emulator.h"

#include <chrono>

#include "../shared/logOutput.h"
EmulatorAPI::EmulatorAPI(IStockApi &datasrc):datasrc(datasrc)
	,prevId(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()
				).count())
{

}

double EmulatorAPI::getBalance(const std::string_view & symb) {

	if (symb != balance_symb) {
		balance = datasrc.getBalance(symb);
		balance_symb = symb;
		return balance;
	} else {
		return balance;
	}
}

EmulatorAPI::TradeHistory EmulatorAPI::getTrades(json::Value lastId, std::uintptr_t ,
		const std::string_view &)  {

	return TradeHistory(std::move(trades));
}

EmulatorAPI::Orders EmulatorAPI::getOpenOrders(const std::string_view & par) {
	return Orders(orders.begin(), orders.end());
}

EmulatorAPI::Ticker EmulatorAPI::getTicker(const std::string_view & pair) {
	this->pair = pair;
	Ticker tk = datasrc.getTicker(pair);
	simulation(tk);
	return tk;
}

json::Value EmulatorAPI::placeOrder(const std::string_view & pair, const Order& order) {
	if (order.id.defined()) {
		auto iter = std::find_if(orders.begin(), orders.end(), [&](const Order &o){
			return o.id == order.id;
		});
		if (iter != orders.end()) {
			*iter = order;
			return iter->id = genID();
		}
	}
	orders.push_back(order);
	return orders.back().id = genID();
}

EmulatorAPI::MarketInfo EmulatorAPI::getMarketInfo(const std::string_view & pair) {
	minfo = datasrc.getMarketInfo(pair);
	return minfo;
}


double EmulatorAPI::getFees(const std::string_view &pair) {
	minfo.fees = datasrc.getFees(pair);
	return minfo.fees;
}

std::vector<std::string> EmulatorAPI::getAllPairs() {
	return datasrc.getAllPairs();
}

void EmulatorAPI::simulation(const Ticker &tk) {

	double cur = tk.last;
	Orders left_orders;
	for (auto &&o: orders) {

		double diffp = cur - o.price;
		double sm = diffp * o.size;
		if (sm > 0) {
			left_orders.push_back(std::move(o));
		} else {
			auto tm = std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch()
					).count();
			IStockApi::Trade tr {
				genID(),
				static_cast<std::size_t>(tm),
				o.size,
				o.price,
				o.size,
				o.price
			};
			minfo.removeFees(tr.eff_size, tr.eff_price);
			trades.push_back(tr);
			ondra_shared::logInfo("Emulator Trade: $1 on $2", o.size, o.price);
			balance +=o.size;
		}

	}

	std::swap(orders, left_orders);
}

bool EmulatorAPI::reset() {
	if (!datasrc.reset()) return false;
	if (!pair.empty()) getTicker(pair);
	return true;
}

bool EmulatorAPI::isTest() const {
	return true;
}

std::size_t EmulatorAPI::genID() {
	return ++prevId;
}
