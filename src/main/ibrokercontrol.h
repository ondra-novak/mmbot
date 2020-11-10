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

#endif /* SRC_MAIN_IBROKERCONTROL_H_ */
