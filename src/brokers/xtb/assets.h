#pragma once
#ifndef SRC_BROKERS_XTB_ASSETS_H_
#define SRC_BROKERS_XTB_ASSETS_H_


#include "../../main/istockapi.h"
#include "client.h"
#include "ratio.h"

#include <optional>
class XTBAssets {
public:

    struct MarketInfo : IStockApi::MarketInfo {
        std::string category;
        std::string group;
        std::string name;
        bool currency_pair;
        double contract_size;
        double size_precision;
    };

    std::optional<MarketInfo> get(const std::string &symbol) const;
    std::optional<std::string> find_combination(const std::string_view &asset, const std::string_view currency) const;
    void update(XTBClient &client);
    MarketInfo update_symbol(XTBClient &client, const std::string &symbol);

    template<typename Fn>
    bool with_symbol(const std::string &symbol, Fn fn) const {
        std::lock_guard _(_mx);
        auto iter = _symbols.find(symbol);
        if (iter == _symbols.end()) return false;
        fn(iter->second);
        return true;
    }


    std::shared_ptr<IFXRate> get_ratio(const std::string &source_currency, const std::string &target_currency, XTBClient &client) const;
    std::shared_ptr<IFXRate> get_ratio(const std::string &source_currency, const std::string &target_currency, XTBClient &client) ;

    bool empty() const;

    std::vector<std::pair<std::string, MarketInfo> > get_all_symbols() const;


protected:
    mutable std::mutex _mx;
    std::unordered_map<std::string, MarketInfo> _symbols;
    static MarketInfo parse_symbol(json::Value v);

    struct RatioOperation {
        std::string symbol;
        bool divide;
    };

    std::vector<RatioOperation> calc_conversion_path(const std::string &source_currency, const std::string &target_currency) const;
    class FXRateOp;

};



#endif /* SRC_BROKERS_XTB_ASSETS_H_ */
