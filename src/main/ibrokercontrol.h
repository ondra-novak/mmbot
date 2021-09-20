/*
 * ibrokercontrol.h
 *
 *  Created on: 20. 10. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_IBROKERCONTROL_H_
#define SRC_MAIN_IBROKERCONTROL_H_
#include <imtjson/value.h>
#include <imtjson/string.h>

class IBrokerControl {
public:

	virtual json::Value getSettings(const std::string_view &pairHint) const = 0;
	///Sets settings to the broker
	/** @param v settings created complette by the template returned by getSettings
	 * @return Can return "undefinded" or it can also return an object. This object is
	 * remembered and next time when broker restarts it is used after initialization
	 * to restore configuration. The broker's configuration is stored in bot's configuration file.
	 */
	virtual json::Value setSettings(json::Value v) = 0;

	///Restores settings previously stored after setSettings called
	virtual void restoreSettings(json::Value v) = 0;

	struct PageData{
		unsigned int code = 0;
		std::vector<std::pair<std::string, std::string> > headers;
		std::string body;
	};

	///Allows to broker act as webserver
	/**
	 * @param method method - POST, GET, PUT, DELETE, ...
	 * @param vpath virtual path - relative path. The path always starts with /. Only '/' is a homepage
	 * @param pageData contains headers and body if given.
	 * @return response from the page. The field code must be filled. If contains zero, rest of the response is ignored and result is always 404
	 */
	virtual PageData fetchPage(const std::string_view &method, const std::string_view &vpath, const PageData &pageData) = 0;

	///Replacement of getAllPairs() - returns structured object
	virtual json::Value getMarkets() const = 0;

	struct WalletItem {
		json::String symbol;
		double balance;
	};

	struct Wallet {
		json::String walletId;
		std::vector<WalletItem> wallet;

	};
	using AllWallets = std::vector<Wallet>;

	virtual AllWallets getWallet()  = 0;

	virtual ~IBrokerControl() {}
};

class IBrokerIcon {
public:
	//saves image to disk to specified path
	virtual void saveIconToDisk(const std::string &path) const = 0;
	//retrieves name of saved image
	virtual std::string getIconName() const = 0;
};

class IStockApi;

class IBrokerSubaccounts {
public:
	virtual IStockApi *createSubaccount(const std::string &subaccount) const= 0;
	virtual bool isSubaccount() const = 0;
	virtual ~IBrokerSubaccounts() {}
};

class IHistoryDataSource {
public:
	struct OHLC {
		double open;
		double high;
		double low;
		double close;
	};

	///Asks to broker, whether there are available historical minute data
	/**
	 * @param asset asset
	 * @param currency currency
	 * @retval true data are available
	 * @retval false data are not available
	 */
	virtual bool areMinuteDataAvailable(const std::string_view &asset, const std::string_view &currency) = 0;
	///Downloads data
	/**
	 *  Downloads data as much as possible into history. Function allows download data in block which
	 *  is necesery when a large portion of the data must be downloaded, the process can take more time that timeout. So
	 *  broker can return less then requested data, but the returned block must be recent.
	 *
	 *  For example, the user want to download data from january to jul. However, the broker can download only one month
	 *  at once, so the broker returns data for jul only. Then additional requests are generated to retrieve rest of the data
	 *
	 *  @param asset asset
	 *  @param currency currency
	 *  @param hint_symbol hints which pair to use - for example if there are multiple pairs for different kinds of markets. The
	 *  broker can ignore the hint.
	 *  @param time_from starting time - linux timestamp (can't be zero)
	 *  @param time_to ending time - linux timestamp (excluded)
	 *  @param data the vector is cleared and filled with result
	 *
	 *  @return function returns 0, if no more data are available. Function returns starting_time, when all of the requested
	 *  data has been downloaded. Function returns number greater than starting_time if less then requested data were downloaded.
	 *  The returned value contains time of the first record
	 */
	virtual std::uint64_t downloadMinuteData(const std::string_view &asset,
					  const std::string_view &currency,
					  const std::string_view &hint_pair,
					  std::uint64_t time_from,
					  std::uint64_t time_to,
					  std::vector<OHLC> &data
				) = 0;

};

#endif /* SRC_MAIN_IBROKERCONTROL_H_ */
