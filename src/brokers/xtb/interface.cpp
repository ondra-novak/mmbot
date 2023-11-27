#include <imtjson/object.h>
#include <shared/logOutput.h>

#include "interface.h"

using ondra_shared::logDebug;

static XTBInterface::BrokerInfo broker_info = {
        false,
        "XTB/XOpenHub",
        "XTB",
        "https://www.xtb.com/cz/realny-ucet",
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
true,true,false,false};





XTBInterface::XTBInterface(const std::string &secure_storage_path)
    :AbstractBrokerAPI(secure_storage_path,
            {
                        json::Object({
                            {"name","userid"},
                            {"label","Account number"},
                            {"type","string"},
                            {"attrs",json::Object{
                                {"style","text-align:center"}
                            }}}),
                        json::Object({
                            {"name","password"},
                            {"label","Password"},
                            {"type","string"},
                            {"attrs",json::Object{
                                {"type","password"},
                                {"style","text-align:center"}
                            }}}),
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
    bool demo;
};

static ServerInfo server_ports[] = {
        {"0","wss://ws.xtb.com/real","wss://ws.xtb.com/realStream",false},
        {"1","wss://ws.xtb.com/demo","wss://ws.xtb.com/demoStream",true},
        {"2","wss://ws.xapi.pro/real","wss://ws.xapi.pro/realStream",false},
        {"3","wss://ws.xapi.pro/demo","wss://ws.xapi.pro/demoStream",true},
};

void XTBInterface::stop_client() {
    _position_control.reset();
    _assets.reset();
    _rates.reset();
    _orderbook.reset();
    _client.reset();
}

bool XTBInterface::on_login() {
    if (_userid.empty() || _password.empty() || _sname.empty()) return false;
    auto iter = std::find_if(std::begin(server_ports), std::end(server_ports),[&](const ServerInfo &server){
        return server.name == _sname;
    });
    if (iter == std::end(server_ports)) return false;

    auto client = std::make_unique<XTBClient>(_httpc, iter->control_url, iter->stream_url);
    if (!client->login(XTBClient::Credentials{
        _userid, _password, std::string("mmbot"), [](const auto &...){}
    }, true)) {
        throw std::runtime_error("Invalid credentials (login failed)");
    }

    _client = std::move(client);

    if (this->debug_mode) {
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
    }


    _is_demo = iter->demo;
    _assets = std::make_unique<XTBAssets>();
    _position_control = PositionControl::subscribe(*_client, [this](auto &&...){});
    _rates = std::make_unique<RatioTable>();
    _orderbook = std::make_unique<XTBOrderbookEmulator>(*_client, _position_control);
    _position_control->set_close_ordering(_current_close_ordering);
    update_equity();
    return true;
}

void XTBInterface::onLoadApiKey(json::Value keyData) {
    _userid = keyData["userid"].getString();
    _password = keyData["password"].getString();
    _sname = keyData["server"].getString();
    stop_client();
}


bool XTBInterface::reset() {
    try {
        ensure_logged_in();
        update_equity();
        _position_control->refresh(*_client);
        return true;
    } catch (std::exception &e) {
        ondra_shared::logError("RESET: $1", e.what());
        return true;
    }
}

void XTBInterface::ensure_logged_in(bool skip_update_assets) {
    if (!_client) {
        if (!on_login()) throw std::runtime_error("Operation need valid API key");
    }
    if (!skip_update_assets && _assets->empty()) {
        _assets->update(*_client, _is_demo);
    }
}

std::vector<std::string> XTBInterface::getAllPairs() {
    ensure_logged_in();
    _assets->update(*_client,_is_demo);
    auto symbols =_assets->get_all_symbols();
    std::vector<std::string> res;
    std::transform(symbols.begin(), symbols.end(),std::back_inserter(res),
            [&](const auto &p) {
        return p.first;
    });
    return res;
}


IStockApi::MarketInfo XTBInterface::getMarketInfo(const std::string_view &pair) {
    ensure_logged_in();
    return _assets->update_symbol(*_client, std::string(pair), _is_demo);
}


AbstractBrokerAPI* XTBInterface::createSubaccount(const std::string &secure_storage_path) {
    return new XTBInterface(secure_storage_path);
}

XTBInterface::BrokerInfo XTBInterface::getBrokerInfo() {
    broker_info.trading_enabled = !_userid.empty() && !_password.empty() && !_sname.empty();
    return broker_info;
}

double XTBInterface::getBalance(const std::string_view &symb, const std::string_view &pair) {
    ensure_logged_in();
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
    ensure_logged_in();
    TradeHistory thist;
    std::string symbol(pair);
    auto sinfo = _assets->get(symbol);
    if (!sinfo.has_value()) return {};

    while (_position_control->any_trade()) {
        _trades.push_back(_position_control->pop_trade());
    }
    std::swap(_trades, _trades_tmp);
    _trades.clear();
    for (auto &t: _trades_tmp) {
        if (symbol == t.symbol) {
            if (t.size) {
                double size = t.size*sinfo->contract_size;
                thist.push_back({
                    t.id,t.time.get_millis(),size,t.price,size,t.price-t.commision/size
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
    ensure_logged_in();
    std::string symbol(par);
    auto sinfo = _assets->get(symbol);
    if (!sinfo.has_value()) return {};
    auto ordlst = _orderbook->get_orders(symbol);
    Orders out;
    std::transform(ordlst.begin(), ordlst.end(), std::back_inserter(out), [&](const Order &x){
        return IStockApi::Order{x.id,x.client_id,x.size*sinfo->contract_size,x.price};
    });
    return out;
}

json::Value XTBInterface::placeOrder(const std::string_view &pair, double size,
        double price, json::Value clientId, json::Value replaceId,
        double replaceSize) {
    ensure_logged_in();
    std::string symbol(pair);
    auto info = _assets->get(symbol);
    if (!info.has_value()) throw std::runtime_error("Unknown symbol");
    return _orderbook->placeOrder(symbol, size/info->contract_size, price, clientId, replaceId);

}

double XTBInterface::getFees(const std::string_view &) {
    return 0.0;
}

IStockApi::Ticker XTBInterface::getTicker(const std::string_view &piar) {
    ensure_logged_in();
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
    const_cast<XTBInterface *>(this)->ensure_logged_in(true);
    std::map<std::string, std::map<std::string, std::map<std::string, std::map<std::string, std::string> > > >tree;
    _assets->update(*_client, _is_demo);
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
    ensure_logged_in();
    return {
        {"equity",{
                {_base_currency, _equity}}
        }
    };
}

bool XTBInterface::areMinuteDataAvailable(const std::string_view &asset,const std::string_view &currency) {
    ensure_logged_in();
    auto f = _assets->find_combination(asset, currency);
    return f.has_value();
}

uint64_t XTBInterface::downloadMinuteData(const std::string_view &asset,
        const std::string_view &currency, const std::string_view &hint_pair,
        uint64_t time_from, uint64_t time_to,
        IHistoryDataSource::HistData &data) {
    ensure_logged_in();
    std::string symbol(hint_pair);
    auto sinfo = _assets->get(symbol);
    if (!sinfo.has_value()) {
        auto s  = _assets->find_combination(asset, currency);
        if (!s.has_value()) throw std::runtime_error("Data are unavailable");
        symbol = *s;
    }

    static constexpr unsigned int intervals[] = {5,15,30,60,240,1440,10080,43200};

    IHistoryDataSource::MinuteData out;

    std::uint64_t start = std::max(time_from, time_to-2592000000);
    std::uint64_t to = time_to -1;
    std::uint64_t fetch_start = 0;
    auto interval_iter = std::begin(intervals);
    auto interval_end =  std::end(intervals);
    while (out.empty() && interval_iter != interval_end) {
        XTBClient::Result res = (*_client)("getChartRangeRequest", json::Object{
            {"info",json::Object{
                {"start", start},
                {"end", to},
                {"period", *interval_iter},
                {"symbol", symbol}
            }}
        });
        if (XTBClient::is_error(res)) throw XTBClient::get_error(res);
        unsigned int rep = *interval_iter / 5;
        json::Value result = XTBClient::get_result(res);
        json::Value values = result["rateInfos"];
        double digits = result["digits"].getNumber();
        double power = std::pow(10,-digits);
        fetch_start = values[0]["ctm"].getUIntLong();
        for (json::Value item:values) {
            double open = item["open"].getNumber();
            double close = open+item["close"].getNumber();
            double high = open+item["high"].getNumber();
            double low = open+item["low"].getNumber();
            double mid = std::sqrt(high*low);
            open *= power;
            close *= power;
            high *= power;
            low *= power;
            mid *= power;

            for (unsigned int i = 0; i < rep; i++) out.push_back(open);
            for (unsigned int i = 0; i < rep; i++) out.push_back(high);
            for (unsigned int i = 0; i < rep; i++) out.push_back(mid);
            for (unsigned int i = 0; i < rep; i++) out.push_back(low);
            for (unsigned int i = 0; i < rep; i++) out.push_back(close);
        }
       ++interval_iter;
    }

    data = std::move(out);

    return fetch_start;
}

void XTBInterface::probeKeys() {
    ensure_logged_in(true);
}

void XTBInterface::update_equity() {
    XTBClient::Result res =(*_client)("getMarginLevel", {});
    if (XTBClient::is_error(res)) throw XTBClient::get_error(res);
    json::Value v = XTBClient::get_result(res);
    _base_currency = v["currency"].getString();
    _equity = v["equity"].getNumber();
}

json::Value XTBInterface::setSettings(json::Value v) {
    restoreSettings(v);
    return v;
}

void XTBInterface::restoreSettings(json::Value v) {
    _current_close_ordering = static_cast<CloseOrdering>(v["close_ordering"].getUInt());
    if (_position_control) _position_control->set_close_ordering(_current_close_ordering);
}

json::Value XTBInterface::getSettings(const std::string_view &pairHint) const {
    std::string v = std::to_string(static_cast<int>(_current_close_ordering));
    return {
        json::Object({
            {"name","close_ordering"},
            {"label","Order of closing trades"},
            {"type","enum"},
            {"default", v},
            {"options",json::Object{
                {"0","Fifo"},
                {"1","Small trades first"},
                {"2","Profit trades first"},
                {"3","Losing trades first"},
                {"4","Lowest swap first"},
            }}
        }),
    };
}
