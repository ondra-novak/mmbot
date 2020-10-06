#include "cryptowatch.h"

#include <algorithm>

CryptowatchPairs::CryptowatchPairs(HTTPJson &httpc):httpc(httpc) {
}


double CryptowatchPairs::getPrice(std::string_view asset, std::string_view currency) const {

	if (reseted) download();

	auto iter1 = symbolMap.find(asset);
	auto iter2 = symbolMap.find(currency);
	if (iter1 == symbolMap.end() || iter2 == symbolMap.end()) return 0;

	return iter1->second/iter2->second;
}

void CryptowatchPairs::reset() {
	reseted = true;
}

std::vector<std::string> CryptowatchPairs::getAllPairs() const {

	if (reseted) download();

	std::vector<std::string> buff;
	std::transform(symbolMap.begin(), symbolMap.end(),std::back_inserter(buff), [](const auto &x){
		return x.first;
	});

	return buff;
}

void CryptowatchPairs::download() const {
	symbolMap.clear();

	json::Value dwn_res = httpc.GET("https://billboard.service.cryptowat.ch/assets?quote=usd&limit=9999");
	json::Value result = dwn_res["result"];
	json::Value rows = result["rows"];
	for(json::Value row: rows) {
		std::string symbol = row["symbol"].getString();
		std::transform(symbol.begin(), symbol.end(), symbol.begin(), [](char c){return std::toupper(c);});
		double price = row["price"].getNumber();
		symbolMap.emplace(symbol, price);
	}
	symbolMap.emplace("USD",1.0);
	reseted = false;
}

bool CryptowatchPairs::needDownload() const {
	return reseted;
}

