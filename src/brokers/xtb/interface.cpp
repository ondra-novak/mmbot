#include <imtjson/object.h>
#include <shared/logOutput.h>

#include "interface.h"

using ondra_shared::logDebug;

static XTBInterface::BrokerInfo broker_info = {
        false,
        "XTB/XOpenHub",
        "XTB",
        "https://www.xtb.com",
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
"iVBORw0KGgoAAAANSUhEUgAAAIAAAACABAMAAAAxEHz4AAAAMFBMVEXrEh7sFCXrJSztKjTtPUPu"
"UVbwZWryfoLzl5n2qaz4vb772Nn86Or+8fH/+/n9//xOxLpQAAADQUlEQVRo3u2YvVMTQRTA35Jo"
"vYEQLEXRGTpgBBwrKDwcK2wIdmBj0EbGhkQKmElDGBq0Ig6VViGpIhaEWKpFYucoBbSQ4N0fAN56"
"4SC53eze3eYs95WZfb+8t/c+F0ZuBZLbcBMCCQIUCSYQUQAFUAAFUAAFUID/AoBAAOiaWfuIOgVg"
"pC3lqvpKZxZgGH2e3SeE1DpxAUM0ninphqVPitIADN1aKmdrE3I+LQ0YWModXmpb8hPJAm5UdNIS"
"c1E6DraJU45lAwk/MJz65qYsIFygDKj1SwLwS9qAoo9cwGikNYP2/aAMqE96ATD0TGX2Xl8RujYo"
"fVJE7gCIDCQbEVNPYxv32KANmHZP557ZraqtUU9f/BArU/rmJyQG4Ii2Vjpsnj191zjLOHA2LSoo"
"Vq4kshXK3NM0Yh0glAEOAIRmrEwjjJymGQfoG2gCMDxM5KqEI/U8cbmBJiCc2tcJXxgHnDHgdGEq"
"e0j8iPkB8QG4e3ZH9wGoTQrLOkSTZcPTgE23vgBa1suIY/fGgkML7kacL3p1JhjfcSPsIs/WhsNv"
"joT6TAwJkgktVEU3+NZfc0VTZT7gl9/ujO/meRdxNu+7vePe97wYGvYPiBV4V7CL/LoQ47pA/q76"
"A+DxvCgRJvwAQPQR+E60AdDTcpBQxqGUa104GXIH4N51Q66e0AD0KO9VEepzLoBQqupdkr6KK9JY"
"1k9N+7MqqInhhbKvqkpO+jkAjOI5gffmnsHeYzsAaetVkf7naME1Hi8AL4SV0Pw2FOljfDOLbd35"
"TtI5AjrP/p5oHw8484E1kGgZTm8zD540/gwtE3F/bn4FdM/qrsxfHczbJ68VxBMCOJcALbmntxhm"
"7VI/gu8fCbMSmGncmuX1K/vnW8foMa81anP6Aowl7UHhYM7hKetE6xZ4Fakrnqno3+kmMmgIboFf"
"VFE0wbbxZcGcAn63udgX2oQJ2XGfCSdTfuUJbXPbjMTSNchdGWS2tg2DM6zIAK5nSo6I1F/J7430"
"KGwvbpKbKzhS3w4m6d25sX9mSg2G/SU72d4BDcS3rJw7CfB+AGg0kausBHrBsMrHs4BPIBjUM5AC"
"KIACKIACKEA7AAcFDAfT7/4H+hGWPy5EUJYAAAAASUVORK5CYII=",
false,true,false,false};





XTBInterface::XTBInterface(const std::string &secure_storage_path)
    :AbstractBrokerAPI(secure_storage_path,
            {
                        json::Object({
                            {"name","userid"},
                            {"label","User ID"},
                            {"type","string"}}),
                        json::Object({
                            {"name","password"},
                            {"label","Password"},
                            {"type","string"}}),
                        json::Object({
                            {"name","server"},
                            {"label","Server"},
                            {"type","enum"},
                            {"options",json::Object{
                                {"0","XTB real"},
                                {"1","XTB demo"},
                                {"2","XOPENHUB real"},
                                {"3","XOPENHUB demo"},
                            }}
                        }),
            })
    ,_httpc("MMBot 2.0 XTB API client", simpleServer::newHttpsProvider(), nullptr, nullptr)
    ,_equity(0)
{
}

struct ServerInfo {
    std::string name;
    std::string control_url;
    std::string stream_url;
};

static ServerInfo server_ports[] = {
        {"0","wss://ws.xtb.com/real","wss://ws.xtb.com/realStream"},
        {"1","wss://ws.xtb.com/demo","wss://ws.xtb.com/demoStream"},
        {"2","wss://ws.xapi.pro/real","wss://ws.xapi.pro/realStream"},
        {"3","wss://ws.xapi.pro/demo","wss://ws.xapi.pro/demoStream"},
};

void XTBInterface::stop_client() {
    _position_control.reset();
    _assets.reset();
    _rates.reset();
    _orderbook.reset();
    _client.reset();
}

void XTBInterface::onLoadApiKey(json::Value keyData) {
    std::string userid = keyData["userid"].getString();
    std::string password = keyData["password"].getString();
    std::string sname = keyData["server"].getString();
    auto iter = std::find_if(std::begin(server_ports), std::end(server_ports),[&](const ServerInfo &server){
        return server.name == sname;
    });
    if (iter == std::end(server_ports)) return;
    _client = std::make_unique<XTBClient>(_httpc, iter->control_url, iter->stream_url);
    if (!_client->login(XTBClient::Credentials{
        userid, password, std::string("mmbot"), [](const auto &...){}
    }, true)) {
        _client.reset();
        throw std::runtime_error("Invalid credentials (login failed)");
    }

    _client->set_logger([&](XTBClient::LogEventType log_ev, WsInstance::EventType ev, const json::Value &v){
        std::string_view evtype;
        std::string_view action;
        switch (log_ev) {
            case XTBClient::LogEventType::command: evtype = "command";break;
            case XTBClient::LogEventType::result: evtype = "response";break;
            case XTBClient::LogEventType::stream_request: evtype = "stream_request";break;
            case XTBClient::LogEventType::stream_data: evtype = "stream_data";break;
            default: break;
        }
        switch(ev) {
            case WsInstance::EventType::connect: action = "connect";break;
            case WsInstance::EventType::disconnect: action = "disconnect";break;
            case WsInstance::EventType::data: action = "data";break;
            case WsInstance::EventType::exception: action = "exception";break;
            default: break;
        }
        logDebug("$1/$2: $3", evtype, action, v.toString().substr(0, 1000));
    });


    _assets = std::make_unique<XTBAssets>();
    _position_control = PositionControl::subscribe(*_client, [this](auto &&...){});
    _rates = std::make_unique<RatioTable>();
    _orderbook = std::make_unique<XTBOrderbookEmulator>(*_client, _position_control);
    update_equity();
}

bool XTBInterface::logged_in() const {
    return _client != nullptr;
}

bool XTBInterface::reset() {
    update_equity();
    return true;
}

void XTBInterface::test_login() const {
    if (!_client) throw std::runtime_error("Not logged (API key is not set)");
}

std::vector<std::string> XTBInterface::getAllPairs() {
    test_login();
    _assets->update(*_client);
    auto symbols =_assets->get_all_symbols();
    std::vector<std::string> res;
    std::transform(symbols.begin(), symbols.end(),std::back_inserter(res),
            [&](const auto &p) {
        return p.first;
    });
    return res;
}


IStockApi::MarketInfo XTBInterface::getMarketInfo(const std::string_view &pair) {
    test_login();
    return _assets->update_symbol(*_client, std::string(pair));
}


AbstractBrokerAPI* XTBInterface::createSubaccount(const std::string &secure_storage_path) {
    return new XTBInterface(secure_storage_path);
}

XTBInterface::BrokerInfo XTBInterface::getBrokerInfo() {
    broker_info.trading_enabled = logged_in();
    return broker_info;
}

double XTBInterface::getBalance(const std::string_view &symb, const std::string_view &pair) {
    test_login();
    std::string symbol(pair);
    auto sinfo = _assets->get(symbol);
    if (!sinfo.has_value()) return 0.0;
    if (symb == sinfo->asset_symbol) {
        auto pos = _position_control->getPosition(symbol);
        return pos.getPos() * sinfo->contract_size;
    } else {
        double rate = _rates->get_ratio({_base_currency, std::string(symb) }, *_assets, *_client);
        return rate * _equity;
    }
}

IStockApi::TradesSync XTBInterface::syncTrades(json::Value lastId, const std::string_view &pair) {
    test_login();
    TradeHistory thist;
    while (_position_control->any_trade()) {
        _trades.push_back(_position_control->pop_trade());
    }
    std::swap(_trades, _trades_tmp);
    _trades.clear();
    for (auto &t: _trades_tmp) {
        if (pair == t.symbol) {
            if (t.size) {
                thist.push_back({
                    t.id,t.time.get_millis(),t.size,t.price,t.size,t.price-t.commision/t.size
                });
            }
        } else {
            _trades.push_back(std::move(t));
        }
    }
    return {thist,nullptr};
}

void XTBInterface::onInit() {}

IStockApi::Orders XTBInterface::getOpenOrders(const std::string_view &par) {
    test_login();
    return _orderbook->get_orders(std::string(par));
}

json::Value XTBInterface::placeOrder(const std::string_view &pair, double size,
        double price, json::Value clientId, json::Value replaceId,
        double replaceSize) {
    test_login();
    std::string symbol(pair);
    auto info = _assets->get(symbol);
    if (!info.has_value()) throw std::runtime_error("Unknown symbol");
    return _orderbook->placeOrder(symbol, size/info->contract_size, price, clientId, replaceId);

}

double XTBInterface::getFees(const std::string_view &) {
    return 0.0;
}

IStockApi::Ticker XTBInterface::getTicker(const std::string_view &piar) {
    test_login();
    return _orderbook->get_ticker(std::string(piar));
}

template<typename Y>
static json::Value treeToObject(const std::map<std::string, Y> &tree) {
    if (tree.size() == 1) {
        if constexpr(std::is_same_v<Y, std::string>) {
            return json::Object{{tree.begin()->first,tree.begin()->second}};
        } else {
            return treeToObject(tree.begin()->second);
        }
    } else {
        return json::Value(json::object, tree.begin(), tree.end(), [&](const auto &iter){
            if constexpr(std::is_same_v<Y, std::string>) {
                return json::Value(iter.first, iter.second);
            } else {
                return json::Value(iter.first, treeToObject(iter.second));
            }
        });
    }

}

json::Value XTBInterface::getMarkets() const {
    test_login();
    std::map<std::string, std::map<std::string, std::map<std::string, std::map<std::string, std::string> > > >tree;
    _assets->update(*_client);
    auto symbs = _assets->get_all_symbols();
    for (const auto &[symbol, info]: symbs) {
        auto &l1 = tree[info.category];
        auto &l2 = l1[info.group];
        if (info.currency_pair) {
            auto &l3 = l2[info.asset_symbol];
            l3[info.currency_symbol] = symbol;
        } else {
            auto &l3 = l2[""];
            auto np = symbol.rfind('_');
            if (np != symbol.npos && np+3 >= symbol.size()) {
                l3[symbol.substr(0,np)] = symbol;
            } else {
                l3[symbol] = symbol;
            }

        }

    }
    return treeToObject(tree);
}

XTBInterface::AllWallets XTBInterface::getWallet() {
    test_login();
    return {
        {"equity",{
                {_base_currency, _equity}}
        }
    };
}

bool XTBInterface::areMinuteDataAvailable(const std::string_view &asset,const std::string_view &currency) {
    return false;
}

uint64_t XTBInterface::downloadMinuteData(const std::string_view &asset,
        const std::string_view &currency, const std::string_view &hint_pair,
        uint64_t time_from, uint64_t time_to,
        IHistoryDataSource::HistData &data) {
    return 0;
}

void XTBInterface::update_equity() {
    XTBClient::Result res =(*_client)("getMarginLevel", {});
    if (XTBClient::is_error(res)) throw XTBClient::get_error(res);
    json::Value v = XTBClient::get_result(res);
    _base_currency = v["currency"].getString();
    _equity = v["equity"].getNumber();
}
