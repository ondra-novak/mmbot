/*
 * ByBitBroker.cpp
 *
 *  Created on: 21. 3. 2023
 *      Author: ondra
 */

#include <imtjson/object.h>
#include <imtjson/parser.h>
#include <imtjson/operations.h>
#include <imtjson/serializer.h>
#include <imtjson/binjson.tcc>
#include <simpleServer/urlencode.h>
#include <shared/logOutput.h>

#include "BybitBrokerV5.h"

#include "rsa_tools.h"

using ondra_shared::logDebug;

const std::string userAgent("+https://www.mmbot.trade");

const std::string licence(R"mit(Copyright (c) 2020 Ondřej Novák

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.)mit");

const std::string icon(
        "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"
        "2aoaAAAAAElFTkSuQmCC"
);


 ByBitBrokerV5::ByBitBrokerV5(std::string secure_path)
     :AbstractBrokerAPI(secure_path, {})
{

}

AbstractBrokerAPI* ByBitBrokerV5::createSubaccount(
        const std::string &secure_storage_path) {
    return new ByBitBrokerV5(secure_storage_path);
}

ByBitBrokerV5::BrokerInfo ByBitBrokerV5::getBrokerInfo() {
    return IBrokerControl::BrokerInfo{
        has_keys(),
        "bybitv5",
        "ByBitV5",
        "https://www.bybit.com/en-US/invite?ref=Y6ERW0",
        "1.0",
        licence,
        icon,
        false,
        true,
    };
}



json::Value ByBitBrokerV5::getApiKeyFields() const {
    auto pair = generateKeysWithBits(2048);
    return {
        json::Object {
            {"type","enum"},
            {"label","API server"},
            {"options",json::Object{
                {"live","Live trading"},
                {"testnet","Testnet (paper trading)"}
            }},
            {"default","live"},
            {"name","server"}
        },
        json::Object {
            {"type","label"},
            {"label","Use 'Self-generated API Keys'"}
        },
        json::Object {
            {"type","label"},
            {"label","Click on Public Key below to copy it into the clipboard and paste it to the form 'Create New Key' on the ByBit platform."}
        },
        json::Object {
            {"type","textarea"},
            {"name","public_key"},
            {"label","Public Key"},
            {"default",pair.public_key},
            {"attrs",json::Object{
                {"onclick","this.select(); "
                        "document.execCommand('copy');"
                        "var el = document.createElement('p');"
                        "el.setAttribute('class','save hide ok');"
                        "el.appendChild(document.createTextNode('✓'));"
                        "var er = this.parentElement.querySelector('p');"
                        "if (er) er.parentNode.removeChild(er);"
                        "this.parentNode.insertBefore(el,this);"
                },
                {"readonly","readonly"},
                {"style","font-size: 0.7em;font-family:monospace;text-align:center;cursor:pointer"},
                {"rows","10"},
            }}
        },
        json::Object {
            {"type","link"},
            {"default","Create api key on Bybit"},
            {"href","https://testnet.bybit.com/app/user/add-secret-key?type=auto"},
            {"showif",json::Object{{"server",json::Value(json::array,{"testnet"})}}},
            {"name","lnk1"}
        },
        json::Object {
            {"type","link"},
            {"default","Create api key on Bybit"},
            {"href","https://www.bybit.com/app/user/add-secret-key?type=auto"},
            {"showif",json::Object{{"server",json::Value(json::array,{"live"})}}},
            {"name","lnk2"}
        },
        json::Object {
            {"type","label"},
            {"label","When creation process is done, paste resulting API Key here"}
        },
        json::Object {
            {"type","textarea"},
            {"name","private_key"},
            {"label","priv_key"},
            {"default",key2String(pair.private_key)},
            {"showif",json::Object{{"none","none"}}},
        },
        json::Object {
            {"type","string"},
            {"name","api_key"},
            {"label","API Key"},
            {"default",""},
        }
    };
}


void ByBitBrokerV5::onLoadApiKey(json::Value keyData) {
    json::Value api_key_json = keyData["api_key"];
    json::Value priv_key_json = keyData["private_key"];
    if (api_key_json.hasValue() && priv_key_json.hasValue()) {
        cur_api_key = api_key_json.getString();
        cur_priv_key = string2key(priv_key_json.getString());
    } else {
        cur_api_key.clear();
        cur_priv_key.reset();
    }
    std::string url;
    if (keyData["server"].getString() == "testnet") {
        url = "https://api-testnet.bybit.com";
        is_paper = true;
    } else {
        url = "https://api.bybit.com";
        is_paper = false;
    }
    httpc = std::make_unique<HTTPJson>(simpleServer::HttpClient(userAgent, simpleServer::newHttpsProvider(), nullptr, simpleServer::newCachedDNSProvider(15)),url);
}

void ByBitBrokerV5::onInit() {
    if (!httpc) {
        httpc = std::make_unique<HTTPJson>(simpleServer::HttpClient(userAgent, simpleServer::newHttpsProvider(), nullptr, simpleServer::newCachedDNSProvider(15)),"https://api.bybit.com");
    }
}

json::Value ByBitBrokerV5::getMarkets() const {
    std::map<std::string_view, std::map<std::string_view, std::map<std::string_view, std::string_view> > > maps;
    std::map<std::string_view, std::string> future_map;
    static const std::string_view spot_name = "Spot";
    static const std::string_view future_name = "Futures ";
    static const std::string_view inversed = "Inversed";
    static const std::string_view perpetual = "Perpetual";

    const SymbolMap &s = const_cast<ByBitBrokerV5 *>(this)->getSymbols();
    for (const auto &[id, nfo] : s) {
        switch (nfo.cat) {
            case Category::spot:
                maps[spot_name][nfo.asset_symbol][nfo.currency_symbol] = id;
                break;
            case Category::linear: {
                std::string &coin = future_map[nfo.currency_symbol];
                if (coin.empty()) {
                    coin.append(future_name);
                    coin.append(nfo.currency_symbol);
                }
                maps[coin][nfo.asset_symbol][nfo.future_id.empty()?perpetual:nfo.future_id] = id;
            }
            break;
            case Category::inverse: {
                maps[inversed][nfo.currency_symbol][nfo.future_id.empty()?perpetual:nfo.future_id] = id;
            }
            break;
        }
    }
    json::Object res;
    for (const auto &[k,v]: maps) {
        json::Object l1;
        for (const auto &[k,v1]: v) {
            json::Object l2;
            for (const auto &[k,v2]: v1) {
                l2.set(k, v2);
            }
            l1.set(k, l2);
        }
        res.set(k, l1);
    }
    return res;

}

ByBitBrokerV5::AllWallets ByBitBrokerV5::getWallet() {
    return {};
}

json::Value ByBitBrokerV5::testCall(const std::string_view &method,json::Value args) {
    return {};
}

bool ByBitBrokerV5::areMinuteDataAvailable(const std::string_view &asset,
        const std::string_view &currency) {
    const auto &s = getSymbols();
    auto iter = std::find_if(s.begin(), s.end(),[&](const auto &x){
            return x.second.currency_symbol == currency && x.second.asset_symbol == asset;
    });
    return (iter != s.end());
}

std::uint64_t ByBitBrokerV5::downloadMinuteData(const std::string_view &asset,
        const std::string_view &currency, const std::string_view &hint_pair,
        std::uint64_t time_from, std::uint64_t time_to, HistData &xdata) {
    const auto &s = getSymbols();
    auto iter = s.find(hint_pair);
    if (iter == s.end()) {
        iter = std::find_if(s.begin(), s.end(),[&](const auto &x){
                return x.second.currency_symbol == currency && x.second.asset_symbol == asset;
        });
        if (iter == s.end()) return 0;
    }

    MinuteData data;

    auto start = std::max(time_from, time_to - 200*300000);
    json::Value hdata = publicGET("/v5/market/kline", json::Object{
        {"category", cat2Val(iter->second.cat)},
        {"symbol", iter->second.api_symbol},
        {"interval", "5"},
        {"start", start},
        {"end", time_to},
        {"limit", 200}
    });
    json::ULongInt end = time_to;
    for (json::Value x: hdata["list"]) {
        auto tm = x[0].getUIntLong();
        if (tm >= time_from && tm < time_to) {
            double o = x[1].getNumber();
            double h = x[2].getNumber();
            double l = x[3].getNumber();
            double c = x[4].getNumber();
            double m = std::sqrt(h*l);
            data.push_back(o);
            data.push_back(l);
            data.push_back(m);
            data.push_back(h);
            data.push_back(c);
            end = std::min(tm, end);
        }
    }
    if (data.size()){
        std::reverse(data.begin(), data.end());
        xdata = std::move(data);
        return end;
    } else {
        return 0;
    }

}

void ByBitBrokerV5::probeKeys() {
    json::Value v = privateGET("/v5/user/query-api",{});
    if (v["readOnly"].getUInt() != 0) {
        throw std::runtime_error("The key is in 'read only' mode.");
    }
    struct Finder {
        json::Value x;
        bool operator()(json::Value v) const {return x == v;}
    };

    json::Value perm = v["permissions"];

    if (!perm["ContractTrade"].find(Finder{"Order"}).defined()
     && !perm["ContractTrade"].find(Finder{"Position"}).defined()
     && !perm["Derivatives"].find(Finder{"DerivativesTrade"}).defined()
     && !perm["Options"].find(Finder{"OptionsTrade"}).defined()
     && !perm["Spot"].find(Finder{"SpotTrade"}).defined()) {
        throw std::runtime_error("No trading permissions allowed on the key.");
    }
}

bool ByBitBrokerV5::reset() {
    return true;
}

IStockApi::TradesSync ByBitBrokerV5::syncTrades(json::Value lastId,
        const std::string_view &pair) {
    const auto &s = getSymbol(pair);
    auto now = httpc->now();
    auto endTime = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()-5000;
    auto startTime = lastId.getIntLong();
    if (lastId.hasValue()) {
        json::Value v = privateGET("/v5/execution/list", json::Object{
            {"category", cat2Val(s.cat)},
            {"symbol", s.api_symbol},
            {"startTime", startTime},
            {"endTime", endTime},
            {"limit", 100}
        });
        auto st = startTime;
        json::Value lst = v["list"].filter([&](json::Value x){
            auto execType = x["execType"].getString();
            auto execTime = x["execTime"].getIntLong();
            if (st < execTime && st >= startTime) st = execTime;
            return s.cat == Category::spot || execType == "Trade" || execType == "AdlTrade" || execType == "BustTrade" || execType == "Delivery" ;
        });
        TradeHistory hist = mapJSON(lst, [&](json::Value v){
            double eff_size;
            double eff_price;
            double fee = v["execFee"].getNumber();
            double dir = v["side"].getString() == "Buy"?1.0:-1.0;
            double size = v["execQty"].getNumber()*dir;
            double price = v["execPrice"].getNumber();
            switch (s.cat) {
                default:
                case Category::spot:
                    if (size > 0) {
                        eff_size = size - fee;
                        eff_price = size * price / eff_size;
                    } else {
                        eff_price = price + fee / size;
                        eff_size = size;
                    }
                    break;
                case Category::linear:
                    eff_size = size;
                    eff_price = price + fee/size;
                    break;
                case Category::inverse:
                    size = -size;
                    price = 1.0/price;
                    eff_size = size;
                    eff_price = price + fee/size;
                    break;
            }

            return Trade {
                v["execId"],
                v["execTime"].getUIntLong(),
                size,
                price,
                eff_size,
                eff_price
            };

        }, TradeHistory());
        std::reverse(hist.begin(), hist.end());
        return TradesSync{hist, st+1};

    } else{
        return IStockApi::TradesSync{{},endTime};
    }


    return {};
}

json::Value ByBitBrokerV5::placeOrder(const std::string_view &pair, double size,
        double price, json::Value clientId, json::Value replaceId,
        double replaceSize) {
    const auto &s = getSymbol(pair);

    if (s.cat == Category::inverse) {
        size = -size;
        price = 1.0/price;
    }

    if (replaceId.hasValue()) {
        if (size == 0) {
            privatePOST("/v5/order/cancel", json::Object{
               {"category", cat2Val(s.cat)},
               {"symbol", s.api_symbol},
               {"orderId", replaceId},
            });
            return nullptr;
        }
        if (s.cat != Category::spot) {
            json::Value v = privatePOST("/v5/order/amend", json::Object {
                {"category", cat2Val(s.cat)},
                {"symbol", s.api_symbol},
                {"orderId", replaceId},
                {"qty",s.sizeToString(std::abs(size))},
                {"price", s.priceToString(price)},
            });
            return v["orderId"];
        } else {
            privatePOST("/v5/order/cancel", json::Object{
               {"category", "spot"},
               {"symbol", s.api_symbol},
               {"orderId", replaceId},
            });
        }
    }

    if (size) {
        json::Value v = privatePOST("/v5/order/create", json::Object {
            {"category", cat2Val(s.cat)},
            {"symbol", s.api_symbol},
            {"side", size < 0?"Sell":"Buy"},
            {"orderType","Limit"},
            {"qty", s.sizeToString(std::abs(size))},
            {"timeInForce","PostOnly"},
            {"price", s.priceToString(price)},
            {"orderLinkId", createLinkId(clientId)}
        });
        return v["orderId"];
    }

    return nullptr;


    return nullptr;
}

std::vector<std::string> ByBitBrokerV5::getAllPairs() {
    std::vector<std::string> out;
    const auto &s = getSymbols();
    for (const auto &[key, value]: s) {
        out.push_back(key);
    }
    return out;
}

IStockApi::Ticker ByBitBrokerV5::getTicker(const std::string_view &pair) {
    const auto &s = getSymbol(pair);
    json::Value v = publicGET("/v5/market/tickers", json::Object{
        {"category", category_to_value(s.cat)},
        {"symbol", s.api_symbol},
    });
    auto t = v["list"][0];
    Ticker ticker{
        t["bid1Price"].getNumber(),
        t["ask1Price"].getNumber(),
        t["lastPrice"].getNumber(),
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(httpc->now().time_since_epoch()).count())
    };
    if (s.invert_price) {
        double z = ticker.ask;
        ticker.ask = 1.0/ticker.bid;
        ticker.bid = 1.0/z;
        ticker.last = 1.0/ticker.last;
    }
    return ticker;

}

IStockApi::Orders ByBitBrokerV5::getOpenOrders(const std::string_view &pair) {
    const auto &s = getSymbol(pair);
    json::Value v = privateGET("/v5/order/realtime", json::Object {
        {"category",cat2Val(s.cat)},
        {"symbol", s.api_symbol},
        {"openOnly", 0},
        {"limit",50},
    });
    json::Value list = v["list"];
    return mapJSON(v["list"], [&](json::Value v){
        double qty = v["qty"].getNumber();
        Order ord {
            v["orderId"],
            parseLinkId(v["orderLinkId"].getString()),
            (v["side"].getString() =="Buy"?1.0:-1.0)*qty,
            v["price"].getNumber()
        };
        if (s.cat == Category::inverse) {
            ord.size = -ord.size;
            ord.price = 1.0/ord.price;
        }
        return ord;
    }, Orders());

}

IStockApi::MarketInfo ByBitBrokerV5::getMarketInfo(const std::string_view &pair) {
    return getSymbol(pair);
}

double ByBitBrokerV5::getBalance(const std::string_view &symb, const std::string_view &pair) {
    const auto &s = getSymbol(pair);
    switch (s.cat) {
        case Category::spot: {
            json::Value v = privateGET("/v5/account/wallet-balance",json::Object{
                {"accountType",getAccountType() == AccountType::regular?"SPOT":"UNIFIED"},
                {"coin",symb}
            });
            json::Value resp = v["list"][0]["coin"][0];
            return resp["equity"].getNumber();
        }
        case Category::linear: {
            if (symb == s.asset_symbol) {
                json::Value v = privateGET("/v5/position/list",json::Object{
                                {"category","linear"},
                                {"symbol",s.api_symbol},
                            });
                json::Value list = v["list"];
                if (list.size() != 1) throw std::runtime_error("Unable to determine position");
                json::Value pos = list[0];
                return pos["size"].getNumber()*(pos["side"].getString() == "Buy"?1.0:-1.0);
            } else {
                if (getAccountType() == AccountType::regular) {
                    json::Value v = privateGET("/v5/account/wallet-balance",json::Object{
                        {"accountType","CONTRACT"},
                        {"coin",symb}
                    });
                    json::Value resp = v["list"][0]["coin"][0];
                    return resp["equity"].getNumber();
                } else {
                    json::Value v = privateGET("/v5/account/wallet-balance",json::Object{
                        {"accountType","UNIFIED"},
                        {"coin",symb}
                    });
                    json::Value resp = v["list"][0];
                    return resp["totalEquity"].getNumber();
                }
            }
        }
        break;
        case Category::inverse: {
            if (symb == s.asset_symbol) {
                json::Value v = privateGET("/v5/position/list",json::Object{
                                {"category","inverse"},
                                {"symbol",s.api_symbol},
                            });
                json::Value list = v["list"];
                if (list.size() != 1) throw std::runtime_error("Unable to determine position");
                json::Value pos = list[0];
                return pos["size"].getNumber()*(pos["side"].getString() == "Buy"?-1.0:1.0);
            } else {
                json::Value v = privateGET("/v5/account/wallet-balance",json::Object{
                    {"accountType","CONTRACT"},
                    {"coin",symb}
                });
                json::Value resp = v["list"][0]["coin"][0];
                return resp["equity"].getNumber();
            }
        }
        break;
        default:
            break;
    }


    return 0;
}

double ByBitBrokerV5::getFees(const std::string_view &char_traits) {
    return 0.001;
}

bool ByBitBrokerV5::has_keys() const {
    if (cur_priv_key != nullptr) return true;
    static bool preinit = false;
    if (!preinit) {
        //preinitalize openssl to avoid long delay when creating api key
        generateKeysWithBits(2048);
        preinit = true;
    }
    return false;
}

static void handleError(HTTPJson::UnknownStatusException &e) {
    json::Value v;
    try {
        auto s = e.response.getBody();
        v = json::Value::parse(s);
    } catch (...) {
    }
    std::string err = std::to_string(e.getStatusCode())
            .append(" ")
            .append(e.getStatusMessage())
            .append(" ")
            .append(v.toString());

    throw std::runtime_error(err);
}

json::Value handleResponse(json::Value resp) {
    int code = resp["retCode"].getUInt();
    if (code == 0) return resp["result"];
    throw std::runtime_error(std::to_string(code).append(" ").append(resp["retMsg"].getString()));
}

const ByBitBrokerV5::SymbolMap& ByBitBrokerV5::getSymbols() {
    auto now = std::chrono::system_clock::now();
    if (_symbol_map_expire < now) {
        json::Value args = json::Object{
            {"limit", 1000},
            {"category", "spot"}
        };

        json::Value spot = publicGET("/v5/market/instruments-info", json::Value(args));
        args.setItems({{"category","linear"}});
        json::Value linear = publicGET("/v5/market/instruments-info", json::Value(args));
        args.setItems({{"category","inverse"}});
        json::Value inverse = publicGET("/v5/market/instruments-info", json::Value(args));

        SymbolMap map;

        for(json::Value item: spot["list"]) {
            MarketInfoEx minfo;
            minfo.api_symbol = item["symbol"].getString();
            minfo.cat = Category::spot;
            std::string name("s");
            name.append(minfo.api_symbol);
            minfo.asset_step = item["lotSizeFilter"]["basePrecision"].getNumber();
            minfo.asset_symbol = item["baseCoin"].getString();
            minfo.currency_step = item["priceFilter"]["tickSize"].getNumber();
            minfo.currency_symbol = item["quoteCoin"].getString();
            minfo.feeScheme = income;
            minfo.fees = 0.001;
            minfo.leverage = 0;
            minfo.invert_price = false;
            minfo.min_size = item["lotSizeFilter"]["minOrderQty"].getNumber();
            minfo.min_volume = 0;
            minfo.wallet_id = "spot";
            minfo.simulator = is_paper;
            map.emplace(std::move(name), std::move(minfo));
        }
        for(json::Value item: linear["list"]) {
            MarketInfoEx minfo;
            minfo.api_symbol = item["symbol"].getString();
            minfo.cat = Category::linear;
            minfo.future_id = item["contractType"].getString()=="LinearFutures"?item["symbol"].getString():"";
            std::string name("l");
            name.append(minfo.api_symbol);
            minfo.asset_step = item["lotSizeFilter"]["qtyStep"].getNumber();
            minfo.asset_symbol = item["baseCoin"].getString();
            minfo.currency_step = item["priceFilter"]["tickSize"].getNumber();
            minfo.currency_symbol = item["settleCoin"].getString();
            minfo.feeScheme = currency;
            minfo.fees = 0.0001;
            minfo.invert_price = false;
            minfo.leverage = item["leverageFilter"]["maxLeverage"].getNumber();
            minfo.min_size = item["lotSizeFilter"]["minOrderQty"].getNumber();
            minfo.min_volume = 0;
            minfo.wallet_id = "futures";
            minfo.simulator = is_paper;
            map.emplace(std::move(name), std::move(minfo));
        }
        for(json::Value item: inverse["list"]) {
            MarketInfoEx minfo;
            minfo.api_symbol = item["symbol"].getString();
            minfo.cat = Category::inverse;
            minfo.future_id = item["contractType"].getString()=="InverseFutures"?item["symbol"].getString():"";
            std::string name("i");
            name.append(minfo.api_symbol);
            minfo.asset_step = item["lotSizeFilter"]["qtyStep"].getNumber();
            minfo.asset_symbol = item["quoteCoin"].getString();
            minfo.currency_step = item["priceFilter"]["tickSize"].getNumber();
            minfo.currency_symbol = item["settleCoin"].getString();
            minfo.feeScheme = currency;
            minfo.fees = 0.0001;
            minfo.invert_price = true;
            minfo.inverted_symbol = item["quoteCoin"].getString();
            minfo.leverage = item["leverageFilter"]["maxLeverage"].getNumber();
            minfo.min_size = item["lotSizeFilter"]["minOrderQty"].getNumber();
            minfo.min_volume = 0;
            minfo.wallet_id = "futures";
            minfo.simulator = is_paper;
            map.emplace(std::move(name), std::move(minfo));
        }
        _symbol_map = std::move(map);
        _symbol_map_expire = now + std::chrono::minutes(15);

    }
    return _symbol_map;
}

std::string_view create_query(std::string &path, json::Value query) {
    char c = '?';
    auto sz = path.length();
    for (json::Value v: query) {
        path.push_back(c);
        path.append(v.getKey());
        path.push_back('=');
        auto encode = simpleServer::urlEncoder([&](char c){path.push_back(c);});
        auto s = v.toString();
        for (auto d: std::string_view(s)) encode(d);
        c = '&';
    }
    std::string_view pp = path;
    pp = pp.substr(std::min(pp.size(),sz+1));
    return pp;
}

json::Value ByBitBrokerV5::publicGET(std::string path, json::Value query) {
    create_query(path, query);
    try {
        return handleResponse(httpc->GET(path,json::Value()));
    } catch (HTTPJson::UnknownStatusException &e) {
        handleError(e);
        throw;
    }
}

json::Value ByBitBrokerV5::privateGET(std::string path, json::Value query) {
    if (cur_priv_key == nullptr) throw std::runtime_error("This call needs valid API key");
    auto pp = create_query(path, query);
    auto hdrs = genSignature(httpc->now(), pp, cur_api_key, cur_priv_key);
    try {
        return handleResponse(httpc->GET(path,std::move(hdrs)));
    } catch (HTTPJson::UnknownStatusException &e) {
        handleError(e);
        throw;
    }
}

json::Value ByBitBrokerV5::privatePOST(std::string path, json::Value payload) {
    if (cur_priv_key == nullptr) throw std::runtime_error("This call needs valid API key");
    auto s = payload.stringify();
    auto hdrs = genSignature(httpc->now(), s.str() , cur_api_key, cur_priv_key);
    try {
        return handleResponse(httpc->POST(path,payload, std::move(hdrs)));
    } catch (HTTPJson::UnknownStatusException &e) {
        handleError(e);
        throw;
    }
}

const ByBitBrokerV5::MarketInfoEx &ByBitBrokerV5::getSymbol(std::string_view symbol) {
    const auto &s = getSymbols();
    auto iter = s.find(symbol);
    if (iter == s.end()) throw std::runtime_error("Unknown symbol");
    return iter->second;

}

json::Value ByBitBrokerV5::category_to_value(Category cat) {
    switch (cat) {
        case Category::spot:return "spot";
        case Category::linear: return "linear";
        case Category::inverse: return "inverse";
    }
    return "unknown";
}

json::Value ByBitBrokerV5::parseLinkId(json::Value linkId) {
    if (linkId.getString().empty()) return json::Value();
    auto vbin = linkId.getBinary(json::base64url);
    auto iter = vbin.begin();
    auto end = vbin.end();
    try {
    json::Value v = json::Value::parseBinary([&]()->int {
        if (iter == end) throw std::monostate();
        return *iter++;
    }, json::base64url);
    return v[0];
    } catch (...) {
        return json::Value();
    }

}

json::Value ByBitBrokerV5::createLinkId(json::Value tag) {
    static auto now_clk = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    auto id = ++now_clk;
    std::basic_string<unsigned char> binOut;
    json::Value data = {tag.stripKey(), id};
    data.serializeBinary([&](char c){
        binOut.push_back(c);
    });
    return json::Value(json::BinaryView(binOut),json::base64url);

}

ByBitBrokerV5::AccountType ByBitBrokerV5::getAccountType() {
    if (unified_mode != AccountType::unknown) return unified_mode;
    json::Value v = privateGET("/v5/account/info",{});
    unified_mode = static_cast<AccountType>(v["unifiedMarginStatus"].getUInt());
    return unified_mode;

}

json::Value ByBitBrokerV5::cat2Val(Category cat) {
    static json::Value spot_name("spot");
    static json::Value linear_name("linear");
    static json::Value inverse_name("inverse");
    switch (cat) {
        case Category::spot: return spot_name;
        case Category::linear: return linear_name;
        case Category::inverse: return inverse_name;
        default: throw std::runtime_error("Internal error: unknown category");
    }
}

std::string ByBitBrokerV5::MarketInfoEx::priceToString(double v) const {
    double adj = std::round(v / currency_step)*currency_step;
    char buff[100];
    int decs = std::ceil(-std::log10(currency_step));
    snprintf(buff,100,"%.*f", decs, adj);
    return buff;
}

std::string ByBitBrokerV5::MarketInfoEx::sizeToString(double v) const {
    double adj = std::round(v / asset_step)*asset_step;
    char buff[100];
    int decs = std::ceil(-std::log10(asset_step));
    snprintf(buff,100,"%.*f", decs, adj);
    return buff;

}
