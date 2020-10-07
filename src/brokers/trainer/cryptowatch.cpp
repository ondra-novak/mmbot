#include "cryptowatch.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fstream>
#include <algorithm>

#include <imtjson/binjson.tcc>
#include "../../shared/logOutput.h"

using ondra_shared::logDebug;
CryptowatchPairs::CryptowatchPairs(HTTPJson &httpc):httpc(httpc) {
}

CryptowatchPairs::~CryptowatchPairs() {
	reset();
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
	if (lkfile != -1) {
		::flock(lkfile, LOCK_UN);
		::close(lkfile);
		lkfile = -1;
	}
}

std::vector<std::string> CryptowatchPairs::getAllPairs() const {

	if (reseted) download();

	std::vector<std::string> buff;
	std::transform(symbolMap.begin(), symbolMap.end(),std::back_inserter(buff), [](const auto &x){
		return x.first;
	});

	return buff;
}

json::Value CryptowatchPairs::do_download() const {
	return httpc.GET(
			"https://billboard.service.cryptowat.ch/assets?quote=usd&limit=9999");
}

static std::string shared_file = "/tmp/a678w20s58w_mmbot_trainer";

void CryptowatchPairs::download() const {
	symbolMap.clear();


	json::Value dwn_res;

	lkfile = ::open((shared_file+".lock").c_str(), O_RDWR|O_CREAT|O_CLOEXEC, 0666);
	int r = flock(lkfile,  LOCK_EX|LOCK_NB);
	if (r == -1) {
		::close(lkfile);
		lkfile = -1;
		std::fstream inf(shared_file+".data", std::ios::in);
		if (!inf) {
			dwn_res = do_download();
		} else {
			dwn_res = json::Value::parseBinary([&](){return inf.get();}, json::base64);
			logDebug("(cryptowatch) Using cached result");
		}
	} else {
		dwn_res = do_download();
		std::fstream onf(shared_file+".part", std::ios::out|std::ios::trunc);
		if (!(!onf)) {
			dwn_res.serializeBinary([&](char c){onf.put(c);}, json::compressKeys);
		}
		::rename((shared_file+".part").c_str(), (shared_file+".data").c_str());
	}
	json::Value result = dwn_res["result"];
	json::Value rows = result["rows"];
	for(json::Value row: rows) {
		double price = row["price"].getNumber();
		if (price == 0) continue;
		std::string symbol = row["symbol"].getString();
		std::transform(symbol.begin(), symbol.end(), symbol.begin(), [](char c){return std::toupper(c);});
		symbolMap.emplace(symbol, price);
	}
	symbolMap.emplace("USD",1.0);
	reseted = false;
}

bool CryptowatchPairs::needDownload() const {
	return reseted;
}

