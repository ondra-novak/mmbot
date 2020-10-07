/*
 * cryptowatch.h
 *
 *  Created on: 6. 10. 2020
 *      Author: ondra
 */

#ifndef SRC_BROKERS_TRAINER_CRYPTOWATCH_H_
#define SRC_BROKERS_TRAINER_CRYPTOWATCH_H_

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "../httpjson.h"
class CryptowatchPairs {
public:

	CryptowatchPairs(HTTPJson &httpc);
	~CryptowatchPairs();

	double getPrice(std::string_view asset, std::string_view currency) const;
	void reset();
	std::vector<std::string> getAllPairs() const;

protected:


	using SymbolMap = std::map<std::string, double, std::less<> >;

	void download() const;
	bool needDownload() const;

	HTTPJson &httpc;
	mutable SymbolMap symbolMap;
	mutable bool reseted = true;

	mutable int lkfile = -1;

private:
	json::Value do_download() const;
};





#endif /* SRC_BROKERS_TRAINER_CRYPTOWATCH_H_ */
