#include "client.h"

#include "streaming.h"
#include <memory>

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
    return {
        {"userId", userId},
        {"password",password},
        {"appName", appName}
     };
}

void XTBClient::login(Credentials c) {
    json::Value arg = c.toJson();
    this->operator ()("login", arg) >> [this, c = std::move(c)](const Result &res) mutable {
      if (isResult(res)) {
          std::lock_guard _(_mx);
          _credents = std::move(c);
          on_login(getResult(res));
          _credents->loginCB(res);
      } else {
          c.loginCB(res);
      }
    };
}

bool XTBClient::data_input(WsInstance::EventType event, json::Value data) {
    switch (event) {
        case WsInstance::EventType::connect:
            if (_credents.has_value()) {
                this->operator ()("login", _credents->toJson()) >> [this](const Result &res) {
                    auto cb = std::move(_credents->loginCB);
                    if (isError(res)) {
                        std::lock_guard _(_mx);
                        _credents.reset();
                    } else {
                        on_login(getResult(res));
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
    }
    bool st = res["status"].getBool();
    if (st) {
        cb(res["returnData"]);
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

bool XTBClient::isError(const Result &res) {
    return std::holds_alternative<Error>(res);
}

bool XTBClient::isResult(const Result &res) {
    return std::holds_alternative<json::Value>(res);
}

const json::Value& XTBClient::getResult(const Result &res) {
    return std::get<json::Value>(res);
}

const XTBClient::Error XTBClient::getError(const Result &res) {
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
    _wscntr.send({
        {"command", command},
        {"arguments", args},
        {"customTag", tag}
    });
}

void XTBClient::on_login(const json::Value &res) {
    std::lock_guard _(_mx);
    _streaming->set_session_id(res["streamSessionId"].getString());
}
