/*
 * manual_matching_broker.h
 *
 *  Created on: 5. 12. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_MANUAL_MATCHING_BROKER_H_
#define SRC_MAIN_MANUAL_MATCHING_BROKER_H_

#include "simulator.h"

#include "apikeys.h"

class ManualMatchingBroker: public Simulator, public IApiKey {
public:
    
    
    using Simulator::Simulator;
    
    
    virtual IBrokerControl::BrokerInfo getBrokerInfo() override;
    virtual void setApiKey(json::Value keyData) override;
    virtual json::Value getApiKeyFields() const override;
    virtual json::Value placeOrder(const std::string_view &pair, double size,
            double price, json::Value clientId, json::Value replaceId,
            double replaceSize) override;

    virtual json::Value setSettings(json::Value v) override;
    virtual json::Value getSettings(const std::string_view &pairHint) const override;

protected:
    bool _trading_enabled;
    
    std::map<std::string, Order, std::less<> > _ready_orders;

    virtual IStockApi::MarketInfo fetchMarketInfo(
            const AbstractPaperTrading::TradeState &st) override;
    virtual void simulate(AbstractPaperTrading::TradeState &state) override;
};




#endif /* SRC_MAIN_MANUAL_MATCHING_BROKER_H_ */
