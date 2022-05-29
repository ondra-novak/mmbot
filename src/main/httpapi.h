/*
 * httpapi.h
 *
 *  Created on: 9. 4. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_HTTPAPI_H_
#define SRC_MAIN_HTTPAPI_H_

#include <shared/refcnt.h>
#include <userver/openapi.h>
#include <userver/static_webserver.h>
#include <userver/callback.h>
#include <shared/linear_map.h>

#include "ssestream.h"

#include "core.h"
#include "longrun.h"


class HttpAPI;

using PHttpAPI = ondra_shared::RefCntPtr<HttpAPI>;

class HttpAPI: public ondra_shared::RefCntObj {
public:
	using Req = userver::PHttpServerRequest;
	using Args = userver::RequestParams;

	HttpAPI(std::unique_ptr<BotCore> &&core,std::size_t max_upload, const std::string_view &www_root)
		:core(std::move(core))
		,static_pages({www_root,"index.html",0})
		,max_upload(max_upload)
		,reg_op_map(PRegOpMap::make())
		,cancel_map(PCancelMap::make()) {}


	void init(userver::OpenAPIServer &server);

	template<typename ACL>
	bool check_acl(Req& req, const ACL &acl);

protected:

	static std::string ctx_json;

	std::unique_ptr<BotCore> core;
	userver::StaticWebserver static_pages;
	std::size_t max_upload;
	userver::OpenAPIServer *cur_server = nullptr;
	PRegOpMap reg_op_map;
	PCancelMap cancel_map;
	std::mutex cfg_lock;





	static void api_error(Req &req, int err, std::string_view desc = std::string_view());

	bool get_root(Req& req, const std::string_view &vpath);
	bool get_api__up(Req& req, const Args& args);
	bool get_api_data(Req& req, const Args& args);
	bool get_api_report_json(Req& req, const Args& args);
	bool get_api_login(Req& req, const Args& args);
	bool get_api_logout(Req& req, const Args& args);
	bool get_api_user(Req& req, const Args& args);
	bool post_api_user(Req& req, const Args& args);
	bool delete_api_user(Req& req, const Args& args);

	bool get_api_broker(Req &req, const Args &args);
	bool get_api_broker_broker(Req &req, const Args &args);
	bool delete_api_broker(Req &req, const Args &args);
	bool get_api_broker_icon_png(Req &req, const Args &args);
	bool get_api_broker_licence(Req &req, const Args &args);
	bool get_api_broker_apikey(Req &req, const Args &args);
	bool put_api_broker_apikey(Req &req, const Args &args);
	bool delete_api_broker_apikey(Req &req, const Args &args);
	bool get_api_broker_pairs(Req &req, const Args &args);
	bool get_api_broker_pairs_pair(Req &req, const Args &args);;
	bool get_api_broker_pairs_ticker(Req &req, const Args &args);
	bool get_api_broker_pairs_settings(Req &req, const Args &args);
	bool put_api_broker_pairs_settings(Req &req, const Args &args);
	bool get_api_broker_pairs_traderinfo(Req &req, const Args &args);


	static void send_json(Req &req, const json::Value &v);

	bool post_set_cookie(Req& req, const Args& args);

	bool get_api_config(Req &req, const Args &v);
	bool post_api_config(Req &req, const Args &v);

	bool get_api_form(Req &req, const Args &v);
	bool get_api_user_set_password(Req &req, const Args &v);
	bool get_api_backtest_data(Req &req, const Args &v);
	bool post_api_backtest_data(Req &req, const Args &v);
	bool post_api_backtest_historical_data(Req &req, const Args &v);
	bool get_api_backtest_historical_data(Req &req, const Args &v);
	bool get_api_backtest_historical_data_broker_pair(Req &req, const Args &v);
	bool post_api_backtest_historical_data_broker_pair(Req &req, const Args &v);
	bool get_api_run(Req &req, const Args &v);
	bool delete_api_run(Req &req, const Args &v);
	bool post_api_backtest(Req &req, const Args &v);
	bool post_api_editor(Req &req, const Args &v);
	bool get_api_wallet(Req &req, const Args &v);
	bool get_api_utilization(Req &req, const Args &v);
	bool get_api_broker_wallet(Req &req, const Args &v);
	bool get_api_trader(Req &req, const Args &v);
	bool get_api_trader_trader(Req &req, const Args &v);
	bool delete_api_trader_trader(Req &req, const Args &v);
	bool get_api_trader_trading(Req &req, const Args &v);
	bool post_api_trader_trading(Req &req, const Args &v);
	bool delete_api_trader_trading(Req &req, const Args &v);
	bool get_api_backtest_trader_data(Req &req, const Args &v);
	bool post_api_backtest_trader_data(Req &req, const Args &v);
	static void redir_to_run(int id, Req &req);

	bool get_api_config_history(Req &req, const Args &v);
	bool get_api_config_history_id(Req &req, const Args &v);

	class DataDownloaderTask;
	class BacktestState;


	void trader_info(Req &req, std::string_view trader_id, std::string_view broker_id, std::string_view pair_id, unsigned int swap_id, bool vis);
};



#endif /* SRC_MAIN_HTTPAPI_H_ */
