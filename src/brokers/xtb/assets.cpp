#include "assets.h"
#include <imtjson/object.h>

#include <unordered_set>
std::optional<XTBAssets::MarketInfo> XTBAssets::get(const std::string &symbol) const {
    std::lock_guard _(_mx);
    auto iter = _symbols.find(symbol);
    if (iter == _symbols.end()) return {};
    return iter->second;
}

XTBAssets::MarketInfo XTBAssets::update_symbol(XTBClient &client, const std::string &symbol, bool demo_account) {
    if (_symbols.empty()) {
        update(client, demo_account);
        auto r = get(symbol);
        if (r.has_value()) return *r;
        else throw std::runtime_error("Symbol not found");
    }
    XTBClient::Result resp = client("getSymbol",json::Object{
        {"symbol", symbol}
    });
    if (client.is_error(resp)) throw client.get_error(resp);
    json::Value s = client.get_result(resp);
    std::lock_guard _(_mx);
    return _symbols[symbol] = parse_symbol(s, demo_account);
}

std::optional<std::string> XTBAssets::find_combination(
        const std::string_view &asset, const std::string_view currency) const {
    auto iter = std::find_if(_symbols.begin(), _symbols.end(), [&](const auto &item){
        const MarketInfo &m = item.second;
        return m.asset_symbol == asset && m.currency_symbol == currency;
    });
    if (iter != _symbols.end()) return iter->first;
    else return {};
}

std::vector<XTBAssets::RatioOperation> XTBAssets::calc_conversion_path(const std::string &source_currency, const std::string &target_currency) const {
    std::vector<RatioOperation> path;
    std::vector<std::tuple<std::string, RatioOperation, int> > walk;
    std::unordered_set<std::string> locked;
    walk.push_back({target_currency, RatioOperation{}, -1});
    std::size_t pos = 0;
    while (pos < walk.size()) {
        std::lock_guard _(_mx);
        auto target = std::get<0>(walk[pos]);
        for (const auto &[symbol, minfo]: _symbols) {
            if (minfo.currency_pair) {
                if (minfo.asset_symbol == target) {
                    if (locked.find(minfo.currency_symbol) == locked.end()) {
                        locked.insert(minfo.currency_symbol);
                        walk.push_back({minfo.currency_symbol, {symbol, true}, static_cast<int>(pos)});
                    } else {
                        continue;
                    }
                } else if (minfo.currency_symbol == target) {
                    if (locked.find(minfo.asset_symbol) == locked.end()) {
                        locked.insert(minfo.asset_symbol);
                        walk.push_back({minfo.asset_symbol, {symbol, false}, static_cast<int>(pos)});
                    } else {
                        continue;
                    }
                }
                if (std::get<0>(walk.back()) == source_currency) {
                    int z = static_cast<int>(walk.size()-1);
                    while (z > 0) {
                        path.push_back(std::get<1>(walk[z]));
                        z = std::get<2>(walk[z]);
                    }
                    return path;
                }
            }
        }
        ++pos;
    }
    return {};
}


struct GlobalEvent {
    std::mutex mx;
    std::condition_variable cond;

    template<typename X>
    void wait(const std::atomic<X> &test, const X &val) {
        if (test == val) {
            std::unique_lock lk(mx);
            cond.wait(lk, [&]{return test != val;});
        }
    }
    void notify() {
        cond.notify_all();
    }
};

class XTBAssets::FXRateOp: public IFXRate {
public:

    FXRateOp(bool divide, std::shared_ptr<IFXRate> prev, GlobalEvent &ev)
        :_prev_rate(prev), _divide(divide),_ev(ev) {}

    virtual double get_rate() const {
        double r= _rate;
        if (_divide) r = 1.0/r;
        if (_prev_rate) r = r * _prev_rate->get_rate();
        return r;
    }
    void set_rate(double rate) {
        this->_rate = rate;
        _ev.notify();
    }
    void set_sub(XTBClient::QuoteSubscription sub) {_sub = sub;}

    virtual bool is_ready() const {
        return _rate != 0;
    }
    virtual void wait() const {
        if (_prev_rate) _prev_rate->wait();
        _ev.wait(_rate, 0.0);
    }
    ~FXRateOp() {
        if (!_rate) {
            _rate = std::numeric_limits<double>::quiet_NaN();
        }
        _ev.notify();
    }


    std::shared_ptr<IFXRate> _prev_rate;
    bool _divide;
    XTBClient::QuoteSubscription _sub;
    GlobalEvent &_ev;

    std::atomic<double> _rate = {0};
};

bool XTBAssets::empty() const {
    std::lock_guard _(_mx);
    return _symbols.empty();

}

std::shared_ptr<IFXRate> XTBAssets::get_ratio(const std::string &source_currency, const std::string &target_currency, XTBClient &client) {
    return const_cast<const XTBAssets *>(this)->get_ratio(source_currency, target_currency, client);
}

std::shared_ptr<IFXRate> XTBAssets::get_ratio(
        const std::string &source_currency, const std::string &target_currency,
        XTBClient &client) const {
    static GlobalEvent gev;


    std::shared_ptr<IFXRate> cur;
    auto path = calc_conversion_path(source_currency, target_currency);
    for (const auto &p: path) {
        std::shared_ptr<FXRateOp> x = std::make_shared<FXRateOp>(p.divide, cur, gev);
        auto sub = client.subscribe_quotes(p.symbol, [wk = std::weak_ptr<FXRateOp>(x)]
                                                      (const std::vector<Quote> & q){
            if (!q.empty()) {
                auto x = wk.lock();
                if (x) {
                    x->set_rate((q.back().ask+q.back().bid)*0.5);
                }
            }
        });
        x->set_sub(sub);
        cur = x;
    }
    return cur;
}

XTBAssets::MarketInfo XTBAssets::parse_symbol(json::Value v, bool demo_account) {
    MarketInfo out = {};
    out.contract_size = v["contractSize"].getNumber();
    out.group = v["groupName"].getString();
    out.category = v["categoryName"].getString();
    out.name = v["description"].getString();
    out.size_precision = v["lotStep"].getNumber();
    out.asset_step = out.contract_size * out.size_precision;
    out.currency_pair = v["currencyPair"].getBool();
    out.asset_symbol = (out.currency_pair?v["currency"]:v["symbol"]).getString();
    out.currency_step = std::pow(10,-v["precision"].getNumber());
    out.currency_symbol = v["currencyProfit"].getString();
    out.feeScheme = IStockApi::currency;
    out.leverage = 100.0/v["leverage"].getNumber();
    out.fees = 0;
    out.invert_price = false;
    out.min_size = v["lotMin"].getNumber()*out.contract_size;
    out.min_volume = 0;
    out.private_chart = false;
    out.simulator = demo_account;
    out.wallet_id = "";
    return out;
}

void XTBAssets::update(XTBClient &client, bool demo_account) {
    XTBClient::Result resp = client("getAllSymbols",{});
    if (client.is_error(resp)) throw client.get_error(resp);
    json::Value symbols = client.get_result(resp);
    std::lock_guard _(_mx);
    _symbols.clear();
    for (json::Value s: symbols) {
        std::string symbol = s["symbol"].getString();
        auto minfo = parse_symbol(s, demo_account);
        _symbols[symbol] = minfo;
    }
}

std::vector<std::pair<std::string, XTBAssets::MarketInfo> > XTBAssets::get_all_symbols() const {
    std::lock_guard _(_mx);
    return {_symbols.begin(), _symbols.end()};
}
