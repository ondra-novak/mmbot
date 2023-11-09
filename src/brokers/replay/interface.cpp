#include <imtjson/object.h>
#include <shared/logOutput.h>
#include <sstream>

#include "interface.h"

using ondra_shared::logDebug;

class ReplayInterface::Source: public IStockApi {
public:
    Source(ReplayInterface &owner):_owner(owner) {}
    virtual IStockApi::TradesSync syncTrades(json::Value lastId, const std::string_view &pair) override {return {};}
    virtual json::Value placeOrder(const std::string_view &pair, double size,
            double price, json::Value clientId, json::Value replaceId,
            double replaceSize) override {return {};}
    virtual IStockApi::Ticker getTicker(const std::string_view &) override {return _owner.create_ticker();}
    virtual IStockApi::Orders getOpenOrders(const std::string_view &par) override {return {};}
    virtual IStockApi::MarketInfo getMarketInfo(const std::string_view &) override {return _owner.get_market();}
    virtual double getBalance(const std::string_view &symb, const std::string_view &pair) override {return _owner.getInitialBalance(symb);}
    virtual void reset(const std::chrono::_V2::system_clock::time_point &tp) override {}
protected:
    ReplayInterface &_owner;
};


static ReplayInterface::BrokerInfo broker_info = {
        false,
        "Replay",
        "Replay",
        "",
        "1.0.0",
        "Copyright (c) 2023 Ondřej Novák\n\n"

"Permission is hereby granted, free of charge, to any person "
"obtaining a copy of this software and associated documentation "
"files (the \"Software\"), to deal in the Software without "
"restriction, including without limitation the rights to use, "
"copy, modify, merge, publish, distribute, sublicense, and/or sell "
"copies of the Software, and to permit persons to whom the "
"Software is furnished to do so, subject to the following "
"conditions: "
"\n\n"
"The above copyright notice and this permission notice shall be "
"included in all copies or substantial portions of the Software. "
"\n\n"
"THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, "
"EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES "
"OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND "
"NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT "
"HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, "
"WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING "
"FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR "
"OTHER DEALINGS IN THE SOFTWARE.",
"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",
                true, true, false, false };

ReplayInterface::ReplayInterface(const std::string &secure_storage_path)
    :AbstractBrokerAPI(secure_storage_path,
            {
                          json::Object({
                              {"name","replay"},
                              {"label","Replay file"},
                              {"type","file"},
                          }),
                          json::Object({
                              {"name", "offset"},
                              {"label","Skip (days)"},
                              {"type","number"},
                              {"default","0"},
                          }),
                          json::Object({
                              {"name", "init_price"},
                              {"label","Initial price"},
                              {"type","number"}
                          }),
                          json::Object({
                              {"name","asset"},
                              {"label","Asset symbol *"},
                              {"type","string"},
                              {"default","BTC"},
                              {"attrs",json::Object{
                                  {"style","text-align:center"}
                              }}}),
                          json::Object({
                              {"name","currency"},
                              {"label","Currency symbol *"},
                              {"type","string"},
                              {"default","USD"},
                              {"attrs",json::Object{
                                  {"style","text-align:center"}
                              }}}),
                          json::Object({
                              {"name","leverage"},
                              {"label","Leverage *"},
                              {"type","enum"},
                              {"options",json::Object{
                                  {"0","Disabled - Spot market"},
                                  {"2","2x"},
                                  {"3","3x"},
                                  {"4","4x"},
                                  {"5","5x"},
                                  {"10","10x"},
                                  {"20","20x"},
                                  {"30","30x"},
                                  {"50","50x"},
                                  {"100","100x"},
                                  {"125","125x"},
                                  {"300","300x"},
                                  {"500","500x"},
                              }}
                          }),
                          json::Object({
                              {"name", "ticksize"},
                              {"label","Tick step"},
                              {"type","number"},
                          }),
                          json::Object({
                              {"name", "lotsize"},
                              {"label","Amount step"},
                              {"type","number"},
                          }),
                          json::Object({
                              {"name", "minsize"},
                              {"label","Min amount"},
                              {"type","number"},
                          }),
                          json::Object({
                              {"name", "minvolume"},
                              {"label","Min volume"},
                              {"type","number"},
                          }),
                          json::Object({
                              {"name", "fees"},
                              {"label","Fees [%] *"},
                              {"type","number"},
                              {"default","0"},
                          }),

              })
,_paper(std::make_unique<Source>(*this))
{
}

ReplayInterface::BrokerInfo ReplayInterface::getBrokerInfo() {
    auto ret =broker_info;
    ret.trading_enabled = _market.has_value();
    return ret;
}

void ReplayInterface::onLoadApiKey(json::Value keyData) {
    _market.reset();
    if (!keyData.hasValue()) return;
    std::string_view data = keyData["replay_content"].getString();
    MarketInfoEx minfo;
    std::istringstream input((std::string(data)));
    while (!(!input)) {
        double v = 0;
        input >> v;
        if (v>0) minfo.data.push_back(v);
    }
    double offset = keyData["offset"].getNumber();
    double init_price = keyData["init_price"].getNumber();
    minfo.asset_symbol = keyData["asset"].getString();
    minfo.currency_symbol = keyData["currency"].getString();
    minfo.leverage = keyData["leverage"].getNumber();
    minfo.asset_step= keyData["ticksize"].getNumber();
    minfo.currency_step= keyData["lotsize"].getNumber();
    minfo.min_size= keyData["minsize"].getNumber();
    minfo.min_volume= keyData["minvolume"].getNumber();
    minfo.fees= keyData["fees"].getNumber()*0.01;
    minfo.feeScheme = minfo.leverage?IStockApi::currency:IStockApi::income;
    minfo.simulator = true;
    minfo.private_chart = true;
    minfo.price_mult = init_price>0?init_price/minfo.data[0]:1.0;

    if (minfo.asset_symbol.empty()) throw std::runtime_error("Asset symbol can't be empty");
    if (minfo.currency_symbol.empty()) throw std::runtime_error("Currency symbol can't be empty");

    if (minfo.data.empty()) return;

    auto max = std::max_element(minfo.data.begin(), minfo.data.end());
    if (*max <= 1e-16) return;
    double maxval = *max * minfo.price_mult;

    if (minfo.currency_step <= 1e-16) {
        double tick_size = std::pow(10, std::floor(std::log10(maxval)-6));
        minfo.currency_step = tick_size;
    }
    if (minfo.asset_step <= 1e-16) {
        minfo.asset_step = std::pow(10, std::floor(std::log10(1.0/ maxval)));
    }
    minfo.min_size = std::max(minfo.asset_step, minfo.min_size);
    minfo.start_time = std::chrono::system_clock::from_time_t(keyData["timestamp"].getIntLong())
                + std::chrono::seconds(static_cast<unsigned int>(offset * 24*60*60));
    minfo.init_equity = (minfo.asset_step *10000) * minfo.data[0]*minfo.price_mult;
    _market = minfo;
}

void ReplayInterface::probeKeys() {
    if (!_market.has_value()) {
        throw std::runtime_error("Failed to read replay file (parse error)");
    }
}

IStockApi::MarketInfo ReplayInterface::getMarketInfo(const std::string_view &pair) {
    return _paper.getMarketInfo(pair);
}

void ReplayInterface::setApiKey(json::Value keyData) {
    if (keyData.hasValue() && !keyData["timestamp"].hasValue()) {
        auto now = std::chrono::system_clock::now();
        keyData.setItems({
           {"timestamp", std::chrono::system_clock::to_time_t(now)}
        });
    }
    AbstractBrokerAPI::setApiKey(keyData);
}

AbstractBrokerAPI* ReplayInterface::createSubaccount(const std::string &secure_storage_path) {
    return new ReplayInterface(secure_storage_path);
}

double ReplayInterface::getBalance(const std::string_view &symb, const std::string_view &pair) {
    return _paper.getBalance(symb, pair);
}

IStockApi::TradesSync ReplayInterface::syncTrades(json::Value lastId, const std::string_view &pair) {
    return _paper.syncTrades(lastId, pair);
}

bool ReplayInterface::reset() {
    _paper.reset(std::chrono::system_clock::now());
    return true;
}

IStockApi::Orders ReplayInterface::getOpenOrders(const std::string_view &par) {
    return _paper.getOpenOrders(par);
}

json::Value ReplayInterface::placeOrder(const std::string_view &pair,
        double size, double price, json::Value clientId, json::Value replaceId,
        double replaceSize) {
    return _paper.placeOrder(pair, size, price, clientId, replaceId, replaceSize);
}

std::vector<std::string> ReplayInterface::getAllPairs() {
    return {"replay"};
}

void ReplayInterface::onInit() {
}

double ReplayInterface::getFees(const std::string_view &pair) {
    return get_market().fees;
}

IStockApi::Ticker ReplayInterface::getTicker(const std::string_view &pair) {
    return _paper.getTicker(pair);
}

json::Value ReplayInterface::getMarkets() const {
    const auto &m = get_market();
    return json::Object {
        {m.asset_symbol,json::Object {
            {m.currency_symbol, "replay"}}}
        };
}

ReplayInterface::AllWallets ReplayInterface::getWallet() {
    return _paper.getWallet();
}

bool ReplayInterface::areMinuteDataAvailable(const std::string_view &asset,
        const std::string_view &currency) {
    return false;
}

uint64_t ReplayInterface::downloadMinuteData(const std::string_view &asset,
        const std::string_view &currency, const std::string_view &hint_pair,
        uint64_t time_from, uint64_t time_to,
        IHistoryDataSource::HistData &data) {
    return 0;
}

json::Value ReplayInterface::setSettings(json::Value v) {
    return _paper.setSettings(v);
}

void ReplayInterface::restoreSettings(json::Value v) {
    return _paper.restoreSettings(v);
}

json::Value ReplayInterface::getSettings(
        const std::string_view &pairHint) const {
    return _paper.getSettings(pairHint);
}

const ReplayInterface::MarketInfoEx& ReplayInterface::get_market() const {
    if (_market.has_value()) {
        return *_market;
    } else {
        throw std::runtime_error("Replay is not initialized");
    }

}

ReplayInterface::Ticker ReplayInterface::create_ticker() const {
    const auto &m = get_market();
    auto now = std::chrono::system_clock::now();
    auto dist = now - m.start_time;
    auto pos = std::chrono::duration_cast<std::chrono::minutes>(dist).count();
    auto idx = (pos % m.data.size());
    auto v1 = m.data[idx]*m.price_mult;
    return Ticker {
        v1 - m.currency_step,
        v1 + m.currency_step,
        v1,
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count())
    };
}

double ReplayInterface::getInitialBalance(std::string_view symb) const {
    const auto &m = get_market();
    if (symb == m.currency_symbol) return m.init_equity;
    else return 0.0;
}
