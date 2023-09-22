#include "client.h"

#include "streaming.h"

#include <future>
#include <memory>
#include <imtjson/object.h>
#include <imtjson/array.h>

const XTBClient::Error XTBClient::error_disconnect = {
        "CDIS","Disconnect"
};
const XTBClient::Error XTBClient::error_exception = {
        "CEXCEPT",""

};


XTBClient::XTBClient(simpleServer::HttpClient &httpc, std::string control_url, std::string streaming_url)
    :_wscntr(httpc, control_url)
    ,_streaming(std::make_shared<XTBStreaming>(httpc, streaming_url))
{


    _wscntr.regHandler([this](WsInstance::EventType event, json::Value data){
        return data_input(event, data);
    });

}

json::Value XTBClient::Credentials::toJson() const {
    return json::Object{
        {"userId", userId},
        {"password",password},
        {"appName", appName}
     };
}

bool XTBClient::login(Credentials c, bool sync) {
    std::promise<bool> p;
    json::Value arg = c.toJson();
    this->operator ()("login", arg) >> [this, sync, &p, c = std::move(c)](const Result &res) mutable {
      if (is_result(res)) {
          std::lock_guard _(_mx);
          _credents = std::move(c);
          on_login(get_result(res));
          _credents->loginCB(res);
          if (sync) p.set_value(true);
      } else {
          c.loginCB(res);
          if (sync) p.set_value(false);
      }
    };
    if (sync) {
        return p.get_future().get();
    } else {
        return true;
    }
}

bool XTBClient::data_input(WsInstance::EventType event, json::Value data) {
    switch (event) {
        case WsInstance::EventType::connect:
            if (_credents.has_value()) {
                this->operator ()("login", _credents->toJson()) >> [this](const Result &res) {
                    auto cb = std::move(_credents->loginCB);
                    std::lock_guard _(_mx);
                    if (is_error(res)) {
                        _credents.reset();
                    } else {
                        on_login(get_result(res));
                    }
                    cb(res);
                };
            }
            break;
        case WsInstance::EventType::disconnect:
            reject_all(error_disconnect);
            break;
        case WsInstance::EventType::exception:
            try {
                throw;
            } catch (const std::exception &e) {
                reject_all(Error{error_exception.code, e.what()});
            }
            break;
        case WsInstance::EventType::data:
            route_result(data);
            break;
    }
    return true;
}

void XTBClient::reject_all(const Error &err) {
    RequestMap tmp;
    {
        std::lock_guard _(_mx);
        std::swap(tmp, _requests);
    }
    for (const auto &[tag, cb]: tmp) {
        cb(err);
    }
}

XTBClient::Request XTBClient::operator ()(std::string command, const json::Value &args) {
    return Request(*this, std::move(command), args);
}

void XTBClient::route_result(const json::Value res) {
    std::string tag = res["customTag"].getString();
    ResultCallback cb;
    {
        std::lock_guard _(_mx);
        auto iter = _requests.find(tag);
        if (iter == _requests.end()) return;
        cb = std::move(iter->second);
        _requests.erase(iter);
    }
    bool st = res["status"].getBool();
    if (st) {
        json::Value data = res["returnData"];
        if (!data.defined()) data = res;
        cb(data);
    } else {
        cb(Error{
            res["errorCode"].getString(),
            res["errorDescr"].getString()
        });
    }
}

XTBClient::Request::Request(XTBClient &owner, std::string command, json::Value arguments)
:owner(owner)
,command(std::move(command))
,arguments(std::move(arguments))
{

}

void XTBClient::Request::operator >>(ResultCallback cb) {
    owner.send_command(command, arguments, std::move(cb));
}

bool XTBClient::is_error(const Result &res) {
    return std::holds_alternative<Error>(res);
}

bool XTBClient::is_result(const Result &res) {
    return std::holds_alternative<json::Value>(res);
}

const json::Value& XTBClient::get_result(const Result &res) {
    return std::get<json::Value>(res);
}

const XTBClient::Error XTBClient::get_error(const Result &res) {
    return std::get<Error>(res);

}

void XTBClient::send_command(const std::string &command, const json::Value &args, ResultCallback &&cb) {
    std::string tag;
    {
        std::lock_guard _(_mx);
        tag = std::to_string(_counter);
        ++_counter;
        _requests.emplace(tag, std::move(cb));
    }
    _wscntr.send(json::Object{
        {"command", command},
        {"arguments", args},
        {"customTag", tag}
    });
}

void XTBClient::set_logger(Logger logger) {
    if (logger) {
        _wscntr.set_logger([logger](bool out, WsInstance::EventType ev, const json::Value &data){
            logger(out?LogEventType::command:LogEventType::result, ev, data);
        });
        _streaming->set_logger([logger](bool out, WsInstance::EventType ev, const json::Value &data){
            logger(out?LogEventType::stream_request:LogEventType::stream_data, ev, data);
        });
    } else {
        _wscntr.set_logger(nullptr);
        _streaming->set_logger(nullptr);
    }
}

void XTBClient::on_login(const json::Value &res) {
    _streaming->set_session_id(res["streamSessionId"].getString());
}

XTBClient::Request::operator Result() const {
    std::promise<Result> res;
    owner.send_command(command, arguments, [&](Result x){
       res.set_value(std::move(x));
    });
    return res.get_future().get();
}

XTBClient::QuoteSubscription XTBClient::subscribe_quotes(std::string symbol,XTBStreaming::StreamCallback<Quote> cb) {
    auto s = _streaming->subscribe_quotes(symbol, std::move(cb));
    (*this)("getTickPrices", json::Object{
        {"level", 0},
        {"symbols",json::Array{symbol}},
        {"timestamp",0}
    }) >> [s](const Result &res) {
        if (is_result(res)) {
            json::Value v = get_result(res);
            s->post_data(v["quotations"][0], true);
        }
    };
    return s;
}


XTBClient::TradeSubscription XTBClient::subscribe_trades(XTBStreaming::StreamCallback<Position> cb) {
    auto s = _streaming->subscribe_trades(std::move(cb));
    (*this)("getTrades", json::Object{
        {"openedOnly", true}
    }) >> [s](const Result &res) {
        if (is_result(res)) {
            json::Value v = get_result(res);
            for (json::Value x: v) {
                s->post_data(x, true);
            }
        }
    };
    return s;
}
