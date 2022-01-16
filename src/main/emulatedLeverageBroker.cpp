/*
 * EmulatedLeverageBroker.cpp
 *
 *  Created on: 17. 12. 2020
 *      Author: ondra
 */

#include "emulatedLeverageBroker.h"

EmulatedLeverageBroker::EmulatedLeverageBroker(PStockApi target, double emulatedLeverage)
	:target(target),emulatedLeverage(emulatedLeverage)
{
}

std::vector<std::string> EmulatedLeverageBroker::getAllPairs() {
	auto sub = dynamic_cast<IBrokerControl *>(target.get());
	if (sub == nullptr) throw std::runtime_error("unsupported operation");
	return sub->getAllPairs();
}

IStockApi::MarketInfo EmulatedLeverageBroker::getMarketInfo(const std::string_view &pair) {
	minfo = target->getMarketInfo(pair);
	if (minfo.leverage) throw std::runtime_error("Can't emulate leverage on leveraged market");
	if (emulatedLeverage<=0) throw std::runtime_error("Invalid leverage to emulate");
	minfo.leverage = emulatedLeverage;
	minfo.wallet_id.append("#leveraged");
	return minfo;
}

IBrokerControl::PageData EmulatedLeverageBroker::fetchPage(const std::string_view &method,
		const std::string_view &vpath, const IBrokerControl::PageData &pageData) {
	auto sub = dynamic_cast<IBrokerControl *>(target.get());
	if (sub == nullptr) throw std::runtime_error("unsupported operation");
	return sub->fetchPage(method, vpath, pageData);
}

json::Value EmulatedLeverageBroker::getSettings(const std::string_view &pairHint) const {
	auto sub = dynamic_cast<const IBrokerControl *>(target.get());
	if (sub == nullptr) return json::Value();
	return sub->getSettings(pairHint);

}

EmulatedLeverageBroker::BrokerInfo EmulatedLeverageBroker::getBrokerInfo() {
	auto sub = dynamic_cast<IBrokerControl *>(target.get());
	if (sub == nullptr) throw std::runtime_error("unsupported operation");
	BrokerInfo binfo = sub->getBrokerInfo();
	return BrokerInfo {
		binfo.trading_enabled,
		binfo.name,
		binfo.exchangeName,
		binfo.exchangeUrl,
		binfo.version,
		binfo.licence,
		binfo.favicon,
		binfo.settings && dynamic_cast<const IBrokerControl *>(target.get()) != nullptr,
		binfo.subaccounts && dynamic_cast<const IBrokerSubaccounts *>(target.get()) != nullptr,
	};
}


double EmulatedLeverageBroker::getBalance(const std::string_view &symb, const std::string_view &pair) {
	double r = target->getBalance(symb, pair);;
	if (symb == minfo.currency_symbol) {
		double asset = target->getBalance(minfo.asset_symbol, pair);
		auto price = target->getTicker(pair);
		return (r+asset*price.last)/emulatedLeverage;
	} else {
		return r;
	}
}

json::Value EmulatedLeverageBroker::setSettings(json::Value v) {
	auto sub = dynamic_cast<IBrokerControl *>(target.get());
	if (sub == nullptr) return json::Value();
	return sub->setSettings(v);

}

void EmulatedLeverageBroker::restoreSettings(json::Value v) {
	auto sub = dynamic_cast<IBrokerControl *>(target.get());
	if (sub == nullptr) return ;
	return sub->restoreSettings(v);
}

IStockApi::TradesSync EmulatedLeverageBroker::syncTrades(json::Value lastId,
		const std::string_view &pair) {
	return target->syncTrades(lastId, pair);
}

bool EmulatedLeverageBroker::reset() {
	return target->reset();
}

IStockApi::Orders EmulatedLeverageBroker::getOpenOrders(const std::string_view &par) {
	return target->getOpenOrders(par);
}

json::Value EmulatedLeverageBroker::placeOrder(const std::string_view &pair,
	double size, double price, json::Value clientId, json::Value replaceId,
	double replaceSize) {
	return target->placeOrder(pair, size, price, clientId, replaceId, replaceSize);
}

double EmulatedLeverageBroker::getFees(const std::string_view &pair) {
	return target->getFees(pair);


}

IStockApi::Ticker EmulatedLeverageBroker::getTicker(const std::string_view &piar) {
	return target->getTicker(piar);
}

std::string EmulatedLeverageBroker::getIconName() const {
	auto sub = dynamic_cast<const IBrokerIcon *>(target.get());
	return sub?sub->getIconName():std::string("undefined.png");
}

void EmulatedLeverageBroker::saveIconToDisk(const std::string &path) const {
	auto sub = dynamic_cast<const IBrokerIcon *>(target.get());
	if (sub) sub->saveIconToDisk(path);
}

json::Value EmulatedLeverageBroker::getMarkets() const {
	auto sub = dynamic_cast<IBrokerControl *>(target.get());
	if (sub == nullptr) return json::object;
	return sub->getMarkets();
}

EmulatedLeverageBroker::AllWallets EmulatedLeverageBroker::getWallet()  {
	auto sub = dynamic_cast<IBrokerControl *>(target.get());
	if (sub == nullptr) return {};
	return sub->getWallet();
}
