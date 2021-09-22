#include "kucoin.h"

#include <imtjson/object.h>

using namespace json;

static Value apiKeyFmt ({
		Object{{"name","key"},{"label","API Key"},{"type","string"}},
		Object{{"name","secret"},{"label","Private Key"},{"type","string"}}
});

KucoinIFC::KucoinIFC(const std::string &cfg_file)
	:AbstractBrokerAPI(cfg_file, apiKeyFmt)

{
}

std::vector<std::string> KucoinIFC::getAllPairs() {
}

bool KucoinIFC::areMinuteDataAvailable(const std::string_view &asset,
		const std::string_view &currency) {
}

IStockApi::MarketInfo KucoinIFC::getMarketInfo(const std::string_view &pair) {
}

AbstractBrokerAPI* KucoinIFC::createSubaccount(
		const std::string &secure_storage_path) {
}

void KucoinIFC::onLoadApiKey(json::Value keyData) {
}

IStockApi::BrokerInfo KucoinIFC::getBrokerInfo() {
}

uint64_t KucoinIFC::downloadMinuteData(const std::string_view &asset, const std::string_view &currency,
		const std::string_view &hint_pair, uint64_t time_from, uint64_t time_to,
		std::vector<IHistoryDataSource::OHLC> &data) {
}

void KucoinIFC::testBroker() {
}

json::Value KucoinIFC::getMarkets() const {
}

double KucoinIFC::getBalance(const std::string_view &symb, const std::string_view &pair) {
}

void KucoinIFC::onInit() {
}

IStockApi::TradesSync KucoinIFC::syncTrades(json::Value lastId,
		const std::string_view &pair) {
}

bool KucoinIFC::reset() {
}

IStockApi::Orders KucoinIFC::getOpenOrders(const std::string_view &par) {
}

json::Value KucoinIFC::placeOrder(const std::string_view &pair, double size, double price,
		json::Value clientId, json::Value replaceId, double replaceSize) {
}

double KucoinIFC::getFees(const std::string_view &pair) {
}

IBrokerControl::AllWallets KucoinIFC::getWallet() {
}

IStockApi::Ticker KucoinIFC::getTicker(const std::string_view &piar) {
}
