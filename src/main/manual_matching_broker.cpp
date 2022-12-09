#include "manual_matching_broker.h"
#include <imtjson/binary.h>
#include <imtjson/object.h>
#include <imtjson/array.h>


IBrokerControl::BrokerInfo ManualMatchingBroker::getBrokerInfo() {
    
    static json::Value bin = json::base64->decodeBinaryValue(
            "iVBORw0KGgoAAAANSUhEUgAAAQAAAAEACAMAAABrrFhUAAAAM1BMVEUAAAAA"
            "AAAZGxgnKSY2ODVERkNVV1VjZWJzdnOAgn+SlJGkpqOztbLGyMXU1tPo6uf/"
            "///hO28dAAAAAXRSTlMAQObYZgAACp9JREFUeNrtnemCojAMgK1ylF7h/Z92"
            "2RGhJxSkxULya3dGHfOZtmkuHw8UFBQUFBQUFBQUFBQUFBQUlF8QEpJ7a38P"
            "BiRCbq7+hRGQDXJ19eu2pYyLUVhH27p+XZqArlvHmJTQmwJSMsYuS0BTTCil"
            "bO0n4by5JAFN/V71i6JEcz0Ck/q8XxelutfFCExrH/o4Ed2lCIzKvLo+Xq5E"
            "YFSl2aL/sBAuQ+CjP+83ykUIfJZ/8PMHuDaBUX/782fyJjbgtX8A1sYQgPIJ"
            "jBqAo38tYraB9iIAGlN/+ucQ9ncg4NEfeDX8qIp0ieASAHRleTfeCCKFFk1g"
            "NADl6M9gozdQNABNfxj1F/HuEC0eQKUbwGb9e1ayCbxvwDMAeA7/f3LZ3wTA"
            "GAIAw5wbsfFKUBcPYNb/RWqxVf+SN4F3+HcyAEUH9TfrfyEAIKm0I6Dy0pvA"
            "G4B2CNoR4Oez7u4EwNafLf56ig8+SwXguwjp+reOn+QHUJUNIBQJY+0nVkxX"
            "7kNVoWtg2QC6as6V0OWjgZcJYEoEeT/VzsiDvujS8VioCbzfdOVd1dxJhg8e"
            "AlzLBEb93XMeBK+19OeMgPNgfLjEoMC4AKQdAuea+kRbKn/7ZYgBlArANABQ"
            "Zv6fGJvFW9rBOZQOBdWVR+B98Ze66XedvwLE3RAo5Vx58mQF6d/bAES3UADj"
            "qxFiTAdQFwpg0kDS5RIoH4IG7DVQHACp/PovOQ56LQ2UeydeALDuPHlMoHgA"
            "GwqfNAuQlwEAbAeBp1ZJ15W+CcLsAUTq/6z1SsJCLYBph7nicd7MR3/DKZSl"
            "OkLKyIpGEZj01x2hUgG8lJkXjgZg6Q9NqZch5hYGrOjxuR0XHxcdIx3SA4BE"
            "ADDvQq9yAwJmJVAsANMAFCs4ImQSWF8DnhXw0b/UoKBBYPWzdFeAKrdolrja"
            "xAHQDUCVXDQcJLBmAPIqRdNuk8CKK+AQ44WXShIrQRwFQDcAWnqlpF0nt+wN"
            "Ot5TWv3JwbKgEsRFuPMCICQfgThv0KXVFKW/953aKXCANQCQJyFCyFkEmtBD"
            "x9SI9lCRDgAhuQjEm8D4SMhRJjslH46TZQJaKRDUS6kBvbBYpQbQHyiMLX+w"
            "/aoJOAUzKvUO0B8qrF4EoJuAtwvCLZcpDEBfLW+DK61AxHmUqlKfgXkB6AWB"
            "wiEw/kDmyghnBeDZcWqLgOfeqKqrAaCejsD3mzD+k6ckIC8Ad4OfOoMNUfma"
            "Jk8B0PTLBFTGSHhmAB8v1yDwNNV/GvqLxHGAcwCYOsrWqAUw68ma3AD2e/9R"
            "AHwmMBBon28zeDaZ9T8LgEWgV0rJ/6KsinFBLgfgQfy18aB8M3XSR0LzA/D4"
            "umeODTgBQDyBHJHwMwDEEsiSCTgFgH+awjljM84BMA1UWe0OSZ4JOQnARCDU"
            "GZAtE5bbE3SCsQMCFwLPN0TtDACW5XRCiA8DkLKziulznwKpAfiXT1VVzSB1"
            "ZQ2TJPk9wQwBkePSbYUBSJJyLAaAq07LBwnp+v93bQ4EmQDYvUB/s1Thf9u8"
            "V/2/6QrwN1y1TYwgCwC7B2jQ3m6ctjpExHxPthrMjkaQAEBXL/SCdfOR5x2R"
            "9Q6LCLPFUIguGYLjc4OdWQpurmzX6RnTY8b7cXqoAbquSYNgmnLoE73XLV5C"
            "zZDDpufxe8384Jg48D1OGv1WWeoD2jmD0dFdR5b+YkpFjIlbmiMA0mg1z0LA"
            "rlHarX8DwRHSRiukXUxnrwQdQQYC+wGYL1zJ8LjQXnH7GXX4wQCqyUhgNwDj"
            "ZV8UIHIuBFmZtvIeQlllI7AXgPGidG1CkA3gtTZiSbFcl6T2i2a1j/XTiIPT"
            "BEDXZ0yxOjmBbwF8bMijjPADIKFDEFRw50ydNN0PwJ//+dMGqD0kxAZgP4N5"
            "xk4qluOavBtAaHCMUoq+XvZAoal/xm6U/lSIVMwTNlNpCXwFIBD3B6Va3yk3"
            "VQ3brfLapJ3Og6BNSeAbAEH930d4C4HZMKF5Q934NAhUJJIfAxDSvwluDKJZ"
            "BPCJGdCcBPYD8OsvZx/WzYYI7cZPwzkCYyhnagK7Afj11wenuABgGYCca2gq"
            "7hnE91MAvPpL3cSfPDgqMQSg0uNJphHwVIOFdgLw6Q/CqIKqPABg2gbnI2IO"
            "m0FlPB+y5FD3AfBlfYUV0pvHK8/b+gxgPgS18UFtcKZMOgK7ABBPq7yY3709"
            "WVMLkVD3DNAAMO3p7kBqmS5Yug+AoT/r7IFx8x6ojQ6cKuXmH1Hq1Ar6h0ul"
            "cYj2APCtUao5rHb3KJ//KYUNTzWNp1hyIsAXIstnAfB8hxbv3Hl58+9b5tTD"
            "Mf0GYM9N0KMMRiv655z8CQCVz/vRY/7zTJxWa411ALTPebd0w4bWcdj+AgDi"
            "TEux9H9YszSg0udqMPOaMFybKmZ1V1mWBkkLSvcC4Cpc5mIRGo73MIBhV3yy"
            "QJOpp6lEng9g9GPUwvFkTxh/6R4DM//gsKyfVAU6hz0V14f3Fu8EoEX/4eVt"
            "B6JaTuipnRj/M0TaNw7Uxncy2UO1PdfKswG4BuA66fYhAcDnMXEgdQdH1rXS"
            "HukHYHTg0RSR8s0ANANw67x757oPoHm1Ut/ZgRuZJHj6e8tUuvbqXQBqZb9l"
            "/Sn98vcKybpiwTj4y19ykNAENgJwfCBPp8sqgIWUWACAPpECfgsACQIIJooW"
            "AIgm0GCqxReBng6AL5/LbwBV0AS85QOj/q9Q2VHsUJLEAGwD8PolIwAaUlOG"
            "bEPwJjiCmEXPpfkVAAMBKuK/YOz/J8srEu4yZ2b+4EwA1A5yBpPQVEQjAEpD"
            "2XAHgDwZgFht9tQKAds2AoHifGkMsZV+hUMvBJsAkEgARiFCy9jit6qAUjxm"
            "KEmb5kZ0AID1UgymggjsurjwOILZlKA+FcB8sZHxc8MpY/59L6pnwAbQngqg"
            "gUhTtMuGg6WDqwVhToztXADWgbwePRxjSGxF/3j/81wAuyKoboTXyJNFvUrn"
            "KTY6F0CzKY1kzxzWAUS9RBex+eYA0FmpvPhMagBA7At08WsvKQBmxcLi/4yZ"
            "6doyMsLyQM8FwCcHZqsG1d0BGENVxsl5jz0AxG8AkNsBQL8vrvGTANQml9wB"
            "0JHSAfTbAWgbOWwH0P4IAGEC6DcBEKGOqxj7+U0Am/4Q4eOzaU3uCeB/pyij"
            "g5BvAMjfALDxc/imSdgqOjoXgNxriF90CF8DwGN/h/RFADx29wZbRTnqJAC9"
            "mandsRfv7Yr9KQAyxWFUlAUoBHBbAD0CwCWAAOYCqQIBHNI8bVlAhq+sOAzA"
            "Me3z9WgCkAYASQfgoAEK1bvGKZX+4fzwtwAOG6FRv4WQfATeyUWrWPJA/anW"
            "v/IDAEgKAJ88vVfmhIWg50qAwGEA+h8XStMC6PtCCNwXQCDljgD+OkcQAAJA"
            "APcG0N4cAGnbL9qGrgBgIPAn5PoAliY27pyrVC6AAIHHfQB4CTzuBMBD4HFd"
            "AFEEHtcFwBpDjhsbUgoAu8eQ3RzAh8B9AfQIAAEgAASAABDA8QDET2st0QIQ"
            "AAJAAAgAASAABIAAEAACQAAI4NYAFD8SQHkElDo0L/C/1r0wOW6gPClZjuu6"
            "uLH+BRM4tvPmxvoXSuCBgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKC4pd/gBgx"
            "oo0lqgwAAAAASUVORK5CYII=");

    std::string name = "manual";
    if (!sub.empty()) name.append("~").append(sub);

    return {
        _trading_enabled,
        name,
        "Manual trading/OTC",
        "",
        "1.0.0",
        "",
        json::map_bin2str(bin.getBinary(json::base64)),
        true,
        true,
        false,
        false,
    };
    

}

void ManualMatchingBroker::setApiKey(json::Value keyData) {
    if (keyData.type() == json::object) {
        if (keyData["agreement"].getString() == "yes") {
            _trading_enabled = true;
        } else {
            throw std::runtime_error("Invalid api key");
        }
    } else {
        _trading_enabled = false;
    }
}

json::Value ManualMatchingBroker::getApiKeyFields() const {
    using namespace json;
    return {
        Object {
            {"label","Your API key is a confirmation that you understand how this broker works"},
            {"type","label"}
        },
        Object {
            {"label","This broker doesn't execute orders. "                    
                    "When the price reaches an order, "
                    "that order is marked with an exclamation mark. "
                    "You can read the price and volume of the order. "                    
                    "These values can be used as an advice to execute "
                    "the trade manually. Visit your favorite DEX or OTC "
                    "exchange and trade as advised above. Once the trade "
                    "is completed, open \"Broker options\" and record "
                    "the details of the trade you \executed. After that "
                    "the strategy recalculates and generates a new pair of orders"},
            {"type","label"}
        },
        Object {
            {"label","This broker uses other brokers as data source"},
            {"type","label"}
        },
        Object{
            {"label",""},
            {"type","enum"},
            {"name","agreement"},
            {"options",Object{
                {"no","-------- click here --------"},
                {"yes","I understand how this broker works"}
            }}
        }
    };
}

json::Value ManualMatchingBroker::setSettings(json::Value v) {
    return Simulator::setSettings(v);
}

void ManualMatchingBroker::simulate(AbstractPaperTrading::TradeState &st) {
    st.ticker = st.source->getTicker(st.src_pair);

}

json::Value ManualMatchingBroker::placeOrder(const std::string_view &pair,
        double size, double price, json::Value clientId, json::Value replaceId,
        double replaceSize) {
    std::lock_guard _(lock);

    AbstractPaperTrading::TradeState &st = getState(pair);
    bool marked = false;
    
    if (replaceId.hasValue()) {
        auto iter = std::find_if(st.openOrders.begin(), st.openOrders.end(), [&](const Order &ord){
            return (ord.id == replaceId);
        });
        marked = iter != st.openOrders.end();
    }
    json::Value out = Simulator::placeOrder(pair, size, price, clientId, replaceId, replaceSize);
    if (marked) {
        _ready_orders[std::string(pair)] = Order{
            out, clientId, size,price
        };
        throw std::runtime_error("Order HIT (manual trade)");
    } 
    _ready_orders.erase(std::string(pair));
    return out;
}

IStockApi::MarketInfo ManualMatchingBroker::fetchMarketInfo(const AbstractPaperTrading::TradeState &st) {
    auto r = AbstractPaperTrading::fetchMarketInfo(st);
    r.simulator = false;
    r.leverage = 0;
    r.feeScheme = FeeScheme::currency;
    return r;
}

json::Value ManualMatchingBroker::getSettings(const std::string_view &pairHint) const {
    std::lock_guard _(lock);
    const AbstractPaperTrading::TradeState &st =  const_cast<ManualMatchingBroker *>(this)->getState(pairHint);
    using namespace json;
    double asset = 0;
    double currency = 0;
    auto iter = wallet[wallet_spot].find(st.minfo.asset_symbol);
    if (iter != wallet[wallet_spot].end()) asset = iter->second.first;
    iter = wallet[wallet_spot].find(st.minfo.currency_symbol);
    if (iter != wallet[wallet_spot].end()) currency = iter->second.first;
    Value is_trade = Object {{"report_en","yes"}};
    double advised_price = st.ticker.last;
    double advised_size = 0;
    auto iter2 = _ready_orders.find(pairHint);
    Value rep_trade = "no";
    if (iter2 != _ready_orders.end()) {
        advised_price = iter2->second.price;
        advised_size = iter2->second.size;
        rep_trade = "yes";
    }
    return {
        Object{
            {"label","hidden"},{"name","pair"},{"type","string"},{"default",pairHint},{"showif",object}
        },
        Object {
            {"label","Balances"},{"type","header"}            
        },
        Object {
            {"label",st.minfo.asset_symbol},
            {"type", "number"},
            {"name","asset"},
            {"default", asset}
        },
        Object {
            {"label",st.minfo.currency_symbol},
            {"type", "number"},
            {"name","currency"},
            {"default", currency}
        },
        Object {
            {"label","Trade"},{"type","header"}            
        },
        Object {
            {"label",""},{"type","enum"},{"default",rep_trade},{"name","report_en"},{
                    "options",Object {
                        {"no","---- no trade ----"},
                        {"yes","Report executed trade"},
                    }
            }
        },
        Object {
            {"label","Execution price (you can modify)"},
            {"type","number"},
            {"default",advised_price},
            {"name","trade_price"},
            {"showif",is_trade},
        },
        Object {
            {"label","Advised size"},{"type","number"},{"default",advised_size?Value(advised_size):Value()},{"name","trade_size"},
            {"attrs",Object{
                {"readonly","readonly"}
            }},
            {"showif",is_trade}
        },
        Object {
            {"label","Final balance " + st.minfo.asset_symbol},
            {"type","number"},
            {"name","final_asset"},
            {"showif",is_trade}
        },
        Object {
            {"label","Final balance " + st.minfo.currency_symbol},
            {"type","number"},
            {"name","final_currency"},
            {"showif",is_trade}
        },
        Object{
            {"label","Fees"},
            {"type","header"}
        },
        Object{
            {"label","Expected fees [%]"},
            {"type","number"},
            {"name","_customfee"},
            {"default",st.fee_override.has_value()?Value(*st.fee_override):Value()}        
        },
        Object{
            {"type","rotext"},
            {"label",""},
            {"default","Specify expected fees for further trades"}
        }
    };
    
    
}
