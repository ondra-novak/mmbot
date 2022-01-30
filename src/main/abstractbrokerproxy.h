/*
 * abstractbrokerproxy.h
 *
 *  Created on: 29. 1. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_ABSTRACTBROKERPROXY_H_
#define SRC_MAIN_ABSTRACTBROKERPROXY_H_
#include "ibrokercontrol.h"

#include "istockapi.h"

class AbstractBrokerProxy: public IStockApi, public IBrokerControl {
public:
	AbstractBrokerProxy(PStockApi trg):target(trg) {}

	virtual std::vector<std::string> getAllPairs() override {
		return forward(&IBrokerControl::getAllPairs,{});
	}
	virtual IBrokerControl::PageData fetchPage(const std::string_view &method,
			const std::string_view&vpath, const IBrokerControl::PageData &pageData) override {
		return forward(&IBrokerControl::fetchPage,{},method, vpath, pageData);
	}
	virtual json::Value getSettings(const std::string_view &pairHint) const override {
		return forward(&IBrokerControl::getSettings, json::Value(),pairHint);
	}
	virtual json::Value setSettings(json::Value v) override{
		return forward(&IBrokerControl::setSettings,json::Value(),std::move(v));
	}
	virtual void restoreSettings(json::Value v) override {
		return forward(&IBrokerControl::restoreSettings,std::move(v));
	}

	virtual json::Value getMarkets() const override {
		return forward(&IBrokerControl::getMarkets,json::Value());
	}
	virtual AllWallets getWallet() override {
		return forward(&IBrokerControl::getWallet,{});
	}

	virtual IBrokerControl::BrokerInfo getBrokerInfo() override {
		return forward(&IBrokerControl::getBrokerInfo,{});
	}

protected:
	PStockApi target;

	template<typename R, typename X, typename ... Args>
	R forward(R (X::*fn)(Args ...), const R &fallback, Args &&... args) {
		X *z = dynamic_cast<X *>(target.get());
		if (z) return (z->*fn)(std::forward<Args>(args)...);
		else return fallback;
	}

	template<typename R, typename X, typename ... Args>
	R forward(R (X::*fn)(Args ...) const, const R &fallback, Args &&... args) const {
		const X *z = dynamic_cast<const X *>(target.get());
		if (z) return (z->*fn)(std::forward<Args>(args)...);
		else return fallback;
	}
	template<typename X, typename ... Args>
	void forward(void (X::*fn)(Args ...), Args &&... args) {
		X *z = dynamic_cast<X *>(target.get());
		if (z) (z->*fn)(std::forward<Args>(args)...);
	}

	template<typename X, typename ... Args>
	void forward(void (X::*fn)(Args ...) const, Args &&... args) const {
		const X *z = dynamic_cast<const X *>(target.get());
		if (z) (z->*fn)(std::forward<Args>(args)...);
	}

};



#endif /* SRC_MAIN_ABSTRACTBROKERPROXY_H_ */
