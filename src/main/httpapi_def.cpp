/*
 * httpapi_def.cpp
 *
 *  Created on: 22. 4. 2022
 *      Author: ondra
 */

#include "httpapi.h"

using namespace userver;


static OpenAPIServer::SchemaItem brokerFormat ={
		"broker","object","broker description",{
				{"name","string","broker's id"},
				{"trading_enabled","boolean","Broker is configured, trading is enabled"},
				{"exchangeName","string","Name of exchange"},
				{"exchangeURL","string","Link to exchange (link to 'create account' page with referral"},
				{"version","string","version string"},
				{"subaccounts","boolean","true=broker supports subaccounts"},
				{"subaccount","string","Subaccount name (empty if not subaccount)"},
				{"nokeys","boolean","true=no keys are needed (probably simulator)"},
				{"settings","boolean","true=broker supports additional settings for each pair"},
		}
};

static OpenAPIServer::SchemaItem stdForm ={
		"field","object","field definition",{
				{"name","string","name of field"},
				{"label","string","Field label"},
				{"type","enum","Field type",{
						{"string"},{"number"},{"textarea"},{"enum"},{"label"},{"slider"},{"checkbox"}
				}},
				{"default","string","prefilled value (default)"},
				{"showif","object","defines conditions, when this field is shown",{
						{"string","assoc","name of field: list of values",{{"","array","list of values",{}}}}
				},true},
				{"hideif","object","defines conditions, when this field is hidden",{
						{"string","assoc","name of field: list of values",{{"","array","list of values",{}}}}
				},true},
				{"options","assoc","Options for enum <value>:<label>",{{"string"}}},
				{"min","number","minimal allowed value",{},true},
				{"max","number","maximal allowed value",{},true},
				{"step","number","value step",{},true},
				{"decimals","number","decimal numbers",{},true},
		}
};

static std::initializer_list<OpenAPIServer::SchemaItem> marketInfo={
		{"asset_step","number","amount granuality"},
		{"currency_step", "number", "tick size (price step)"},
		{"asset_symbol","string", "symbol for asset"},
		{"currency_symbol","string","symbol for currency"},
		{"min_size", "number", "minimal order amount"},
		{"min_volume", "number", "minimal order volume (amount x price)"},
		{"fees", "number", "fees as part of 1 (0.1%=0.001) "},
		{"feeScheme","number", "how fees are calculated (income, asset, currency)"},
		{"leverage", "number", "leverage amount, 0 = spot"},
		{"invert_price","boolean", "inverted market (price is inverted)" },
		{"inverted_symbol", "string","quoting symbol (inverted symbol)"},
		{"simulator", "boolean", "true if simulator"},
		{"private_chart", "boolean", "true if price is derived, and should not be presented as official asset price"},
		{"wallet_id", "string","id of wallet used for trading"}};

static std::initializer_list<OpenAPIServer::SchemaItem> tickerStruct={
		{"ask","number"},{"bid","number"},{"last","number"},{"time","number"}
};

static OpenAPIServer::ParameterObject brokerId = {"broker","path","string","Broker's ID"};
static OpenAPIServer::ParameterObject pairId = {"pair","path","string","Pair ID"};
static OpenAPIServer::ParameterObject traderId = {"trader","path","string","Trader's ID"};


std::string HttpAPI::ctx_json = "application/json";

void HttpAPI::init(std::shared_ptr<OpenAPIServer> server) {
	cur_server = server;
	PHttpAPI me = this;

	auto reg = [&](const std::string_view &p) {return server->addPath(p);};

	server->addPath("",[me](Req &req, const std::string_view &vpath){ return me->get_root(req, vpath);});

	reg("/set_cookie")
		.POST("General","Set cookie", "Content is application/x-www-form-urlencoded, and expects auth=<content>. It directly copies this to the cookie (as session) - this is intended to be used by platform to allow direct entering to the administration using JWT token",{},"",{
				{"application/x-www-form-urlencoded","","object","",{
						{"auth","string","content of cookie"},
						{"redir","string","http URL where to redirect after cookie is set. If not specified, request returns 202",{},true}}},
			} ).method(me,&HttpAPI::post_set_cookie);
	reg("/api/_up")
		.GET("Monitoring","Returns 200 if server is running").method(me,&HttpAPI::get_api__up);
	reg("/api/data")
		.GET("Statistics","Statistics datastream SSE","",{},{
			{200,"SSE Stream",{
					{"text/event-stream","Event stream","string","Each line starts with data: followed with JSON event (see detailed documentation)"}
			}}}).method(me,&HttpAPI::get_api_data);
	reg("/api/report.json")
		.GET("Statistics","Statistics report snapshot").method(me,&HttpAPI::get_api_report_json);
	reg("/api/login")
		.GET("General","Login using Basic Autentification","Requests for login dialog and after login is successful, redirects the user agent to the specified url",{
			{"redir","query","string","Absolute URL where to redirect the user agent",{},false}
			}).method(me, &HttpAPI::get_api_login);
	reg("/api/logout")
		.GET("General","Clears basic autentification state","It also deletes auth cookie to ensure that logout works",{
			{"redir","query","string","Absolute URL where to redirect the user agent",{},false}
			}).method(me, &HttpAPI::get_api_logout);
	reg("/api/user")
		.GET("General","Get information about current user","",{},{
					{200,"Information about user",{
						{ctx_json,"UserInfo","object","",{
								{"user","string","Name of the user (username)"},
								{"exists","boolean","true=User is valid, false=user is not valid"},
								{"jwt","boolean","true=logged in from external login server using JWT token"},
								{"viewer","boolean","true=current user can read statistics"},
								{"report","boolean","true=current user can read trading reports"},
								{"admin_view","boolean","true=current user can read settings, but cannot change them"},
								{"admin_edit","boolean","true=current user can read and edit settings"},
								{"backtest","boolean","true=current user can execute backtest"}
						}}
					}}
			}).method(me,&HttpAPI::get_api_user)
		.POST("General","Log-in a user","",{
					{"auth","cookie","string","JWT token",{}, false},
					{"Authorization","header","string","Bearer JWT token/Basic authorization",{}, false}
			},"",{
					{ctx_json,"","object","User credentials. The credentials can be also send as cookie 'auth' (JWT) or as Basic authentification (Authorization)",{
						{"user","string","Username to log-in",{},true},
						{"password","string","Password",{},true},
						{"token","string","A valid JWT token (or session)",{},true},
						{"exp","unsigned","Session expiration. If not specified, creates unspecified long session (permanent login)",{},true},
						{"cookie","enum","Specifies how to store result",{
								{"temporary","","Create temporary (session) cookie "},
								{"permanent","","Create permanent cookie"},
								{"return","","Don't set cookie, return result"},
						}},
						{"needauth","boolean","Require login using Basic auth, if no credentials are supplied (return 401)",{},true},
					}}
			},{
					{200,"Information about user",{
						{ctx_json,"UserInfo","object","",{
								{"user","string","Name of the user (username)"},
								{"exists","boolean","true=User is valid, false=user is not valid"},
								{"jwt","boolean","true=logged in from external login server using JWT token"},
								{"viewer","boolean","true=current user can read statistics"},
								{"report","boolean","true=current user can read trading reports"},
								{"admin_view","boolean","true=current user can read settings, but cannot change them"},
								{"admin_edit","boolean","true=current user can read and edit settings"},
								{"backtest","boolean","true=current user can execute backtest"},
								{"cookie","string","returned cookie",{},true}
						}}
					}}
			}).method(me,&HttpAPI::post_api_user)
		.DELETE("General","Log-out the current user")
				.method(me,&HttpAPI::delete_api_user);

	reg("/api/user/set_password")
		.POST("General","Change password of current user", "", {},"",{
				{ctx_json,"","object","",{
					{"old","string","old password"},
					{"new","string","new password"},
				}},
			},{
				{202,"Password set",{{ctx_json,"","boolean","true"}}},
				{400,"New password can't be empty / No change (same password)"},
				{409,"Current password doesn't match"},
			}).method(me, &HttpAPI::get_api_user_set_password);
	reg("/api/run/{id}")
		.GET("General","Retrieve SSE stream of long running operation - some operations need to open this stream to actually be executed","",{
			{"id","path","integer","Operation ID, it is returned as result of operation creation"}},{
			{200,"Progress state", {
					{"application/json-seq","","string","line separated JSONs"},
					{"text/event-stream","","string","JSON as event stream, prefixed by \"data:\" "}
			}},
				{404,"ID not found"}
			}).method(me, &HttpAPI::get_api_run)
		.DELETE("General","Cancel running operation graciously (you can cancel operation by closing SSE, but you will not able to receive final status)","",{
			{"id","path","integer","Operation ID, it is returned as result of operation creation"}},"",{},{
					{200,"Operation flagged to cancel"},
					{404,"Operation ID not found"}
			}).method(me,&HttpAPI::delete_api_run);

	reg("/api/broker")
		.GET("Brokers","List of configured brokers","",{},{
			{200,"",{{ctx_json,"","array","",{brokerFormat}}}}}).method(me, &HttpAPI::get_api_broker)
		.DELETE("Brokers","Reload brokers","Terminates all brokers and reloads each when it is accessed. This operation should not be considered as harmful and can solve any temporary issue with a broker",{},"",{},{
			{202,"Accepted",{}}}).method(me, &HttpAPI::delete_api_broker);
	reg("/api/broker/{broker}")
		.GET("Brokers","Get info of particular broker","",{brokerId},{
			{200,"",{{ctx_json,"","array","",{brokerFormat}}}}}).method(me, &HttpAPI::get_api_broker_broker);

	reg("/api/broker/{broker}/icon.png")
		.GET("Brokers","Broker's favicon","",{brokerId},
			{{200,"OK",{{"image/png","","","Broker's favicon"}}}}).method(me, &HttpAPI::get_api_broker_icon_png);

	reg("/api/broker/{broker}/licence")
		.GET("Brokers","Licence file","",{brokerId},
			{{200,"OK",{{ctx_json,"","string","Licence text"}}}}).method(me, &HttpAPI::get_api_broker_licence);


	reg("/api/broker/{broker}/apikey")
		.GET("Brokers","Retrieve APIKEY format","",{brokerId},
			{{200,"OK",{{ctx_json,"","array","List of field",{stdForm}}}}}).method(me, &HttpAPI::get_api_broker_apikey)
		.PUT("Brokers","Set APIKEY","",{brokerId},"",{
			{OpenAPIServer::MediaObject{"application/json","apikey","assoc","key:value of apikey",{
					{"","anyOf","key:value",{{"","string"},{"","number"},{"","boolean"}}}
			}}}},{
			{OpenAPIServer::ResponseObject{200,"OK",{{ctx_json,"","boolean","true"}}}},
			{OpenAPIServer::ResponseObject{409,"Conflict (invalid api key)",{{ctx_json,"","string","Error message returned by exchange API server"}}}}
			}).method(me, &HttpAPI::put_api_broker_apikey)
		.DELETE("Brokers","Delete APIKEY","",{brokerId},"").method(me, &HttpAPI::delete_api_broker_apikey);

	reg("/api/broker/{broker}/wallet")
		.GET("Brokers","Dump account wallet","",{brokerId},{
				{200,"OK"}
				}).method(me, &HttpAPI::get_api_broker_wallet);


	reg("/api/broker/{broker}/pairs")
		.GET("Brokers","List of available pairs","",{brokerId,{"flat","query","boolean","return flat structure (default false)",{},false}},
			{{200,"OK",{{ctx_json,"","oneOf","depend on 'flat' argument",{
					{"","assoc","Structure (flat=false)",{{"","string","pairid"}}},
					{"","array","Flat (flat=true",{{"","string","pairid"}}}
			}}}}}).method(me, &HttpAPI::get_api_broker_pairs);

	reg("/api/broker/{broker}/pairs/{pair}")
		.GET("Brokers","Market information","",{brokerId,pairId},{
				{OpenAPIServer::ResponseObject{200, "OK", {{ctx_json, "", "object", "", marketInfo}}}}
				}).method(me, &HttpAPI::get_api_broker_pairs_pair);

	reg("/api/broker/{broker}/pairs/{pair}/ticker")
		.GET("Brokers","Get ticker","",{brokerId,pairId},{
			{OpenAPIServer::ResponseObject{200, "OK", {{ctx_json, "", "object", "ticker information",tickerStruct}}}}
			}).method(me, &HttpAPI::get_api_broker_pairs_ticker);

	reg("/api/broker/{broker}/pairs/{pair}/trader_info")
		.GET("Brokers","Generate trader infomation of not-yet created trader","",{
				brokerId,pairId, {"swap_mode","query","int","Swap mode"}},{
			{OpenAPIServer::ResponseObject{200, "OK"}}
			}).method(me, &HttpAPI::get_api_broker_pairs_traderinfo);

	reg("/api/broker/{broker}/settings")
		.GET("Brokers","Get broker settings (form)","",{brokerId,{"pair","query","string","selected pair"}},{
				{OpenAPIServer::ResponseObject{200, "OK", {{ctx_json, "", "array", "Form fields",{stdForm}}}}}
			}).method(me, &HttpAPI::get_api_broker_pairs_settings)
		.PUT("Brokers","Store broker settings","",{brokerId},"",{
			{OpenAPIServer::MediaObject{"application/json","apikey","assoc","key:value",{
					{"","anyOf","key:value",{{"","string"},{"","number"},{"","boolean"}}}
			}}}},{
			{OpenAPIServer::ResponseObject{200,"OK",{{ctx_json,"","object","all settings"}}}},
			}).method(me, &HttpAPI::put_api_broker_pairs_settings);;


	reg("/api/config")
		.GET("Configuration","Get configuration file","",{},{
			{200,"OK",{{ctx_json, "", "object"}}}}).method(me, &HttpAPI::get_api_config)
		.POST("Configuration","Apply config diff, restart trading and return whole config","",{},"",{
			{ctx_json,"","object","Configuration diff (transfer only changed fields, use {} to delete fields)"}},
			{{202,"Accepted",{{ctx_json,"","object","whole merged configuration"}}}
			}).method(me, &HttpAPI::post_api_config);
	reg("/api/forms")
		.GET("Configuration","Retrieve form definition for specified object","",{},
			{{200,"OK",{{ctx_json,"","object"}}}}).method(me, &HttpAPI::get_api_form);

	reg("/api/backtest/data/{id}")
		.GET("Backtest","Retrieve minute price data (data must be uploaded, or imported from exchange)","",{
				{"id","path","identifier"}
			},{
					{200,"Stored data",{{ctx_json,"","array","",{{"number","number"}}}}},
					{404,"ID is not valud - need reupload"}
			}).method(me, &HttpAPI::get_api_backtest_data);
	reg("/api/backtest/data")
		.POST("Backtest","Upload minute price data - one item per minute","",{},"",{
				{ctx_json,"data","array","Minute data - numbers, one number per minute",{{"","number","",}}},
			},{
				{201,"Created",{{ctx_json,"","object","",{{"id","string"}}}},{{"Location","header","string","Resource location"}}}
			}).method(me, &HttpAPI::post_api_backtest_data);
	reg("/api/backtest/historical_data")
		.GET("Backtest","Retrieve available symbols for historical data","",{},{
				{200,"OK",{{ctx_json,"","array","",{{"string","string"}}}}}
			}).method(me, &HttpAPI::get_api_backtest_historical_data)
		.POST("Backtest","Upload historical data (from historical service)","",{},"",{
				{ctx_json,"","object","",{
						{"asset","string","asset"},
						{"currency","string","currency"},
						{"smooth","integer","smooth"}
				}}
			},{
				{201,"Created",{{ctx_json,"","object","",{{"id","string"}}}},{{"Location","header","string","Resource location"}}}
			}).method(me, &HttpAPI::post_api_backtest_historical_data);
	reg("/api/backtest/historical_data/{broker}/{pair}")
		.GET("Backtest","Probe, whether historical data are available for given pair","",{brokerId,pairId},{
			{200,"Data are available",{{ctx_json,"","boolean","true"}}},
			{404,"Data are not available"}
		}).method(me, &HttpAPI::get_api_backtest_historical_data_broker_pair)
		.POST("Backtest","Download historical data from exchange","",{brokerId,pairId},"",{},{
			{202,"Download in progres",{{ctx_json,"","object","Information about process",{
					{"progress","string", "ID of progress (/api/progress/ID). When URL becomes unavalable, the downloaded data are ready"},
					{"id","string", "ID under which data becomes available"}
			}}}},
			{404,"Data are not available, cannot be downloaded"}
		}).method(me, &HttpAPI::post_api_backtest_historical_data_broker_pair);
	reg("/api/backtest/trader_data/{trader}")
		.GET("Backtest","Retrieve trader's data collected during trading","",{
				traderId,{"withtm","query","boolean","Return prices with timestamps",{},false}
			},{{200,"OK",{
				{ctx_json,"data","array","Minute data",{
						{"","oneOf","",{
								{"price","number","price"},
								{"minute","array","minute",{
										{"tuple","number","tuple of two values, javascript timestamp and price"}

								}}
						}}
				}},
			}},
				{404,"Trader not found"}
			}).method(me, &HttpAPI::get_api_backtest_trader_data)
		.POST("Backtest","Store trader's data to the data storage, returns ID, these data can be used for backtesing","",{traderId},"",{},{
			{201, "Created", {{ctx_json,"id","object","",{
					{"id","string","id under data were stored"}
			}}}},
				{404,"Trader not found"}
			}).method(me, &HttpAPI::post_api_backtest_trader_data);

	reg("/api/backtest")
		.POST("Backtest","Create new backtest. ID of test returned and can be executed through /api/run/<id>","",{
			{"stream","query","boolean","Prepare operation to by streamed as executed (SSE) - default is false - result is returned directly to the response"},
			{"Accept","header","enum","Requested conten type",{{"text/event-stream"},{"application/json-seq"}}}
		},"",{
				{ctx_json, "config","object","backtest configuration",{
						{"makret","object","Market information",marketInfo},
						{"source","object","source definition",{
								{"id","string","Data source - ID of uploaded minute data"},
								{"offset","integer","(optional) skip specified count of minutes"},
								{"limit","integer","(optional) limit count of minutes"},
								{"start_time","int64","(optional) javascript timestamp (milliseconds) of starting date. If not set, current date is used"},
								{"init_price","number","(optional) initial price, if not set (or zero), it uses prices from supplied minute data"},
								{"backward","boolean","(optional) process data backward"},
								{"invert","boolean","(optional) invert data - swap top and bottom"},
								{"mirror","boolean","(optional) extends data with mirrored version of itself"}
						}},
						{"trader","object","Trader configuration (complete) - see other doc"},
						{"init","object","Initial conditions",{
								{"position","number","initial position. If not set, recommended position is used"},
								{"balance","number","initial currency balance. Mandatory"}
						}},
						{"output","object","Specify additional output",{
								{"freq","enum","how often is output generated (default is trade)",{{"trade"},{"minute"}}},
								{"strategy_state","boolean","include state of strategy (defaulf is false)"},
								{"log","boolean","include log messages (default is false)"},
								{"strategy_raw_state","boolean","include raw state of strategy (default is false)"},
								{"spread_raw_state","boolean","include raw state of spread (default is false)"},
								{"orders","boolean","include open orders (default false, for freq=minute)"},
								{"stats","boolean","include current position, balance, profit, norm-profit, etc (default false)"},
								{"debug","boolean","include more verbose (possible duplicated) informations (default is false)"},
								{"diff","boolean","send diff - each event is diff-object to previous event default is false)"},
						}}

				}}},{
						{200,"OK",{{"text/event-stream","","","Events"},{"application/json-seq","","","Events"}}},
						{201,"Created",{{ctx_json,"","object","",{{"id","string"}}}},{{"Location","header","string","Resource location"}}},
						{400,"Bad request - syntaxt errors"},
						{409,"Conflict - formal errors"},
						{416,"Invalid range: offset and limit out of range"},
				}).method(me, &HttpAPI::post_api_backtest);


	reg("/api/editor")
		.POST("Editor","Retrieve detailed info about trader (even if it is not yet saved)","",{},"",{
				{ctx_json,"","object","",{
					{"trader","string","Name of the trader. When not yet created, other fields are used"},
					{"broker","string","Broker - madatory only if pair is not yet created"},
					{"pair","string","Pair id - madatory only if pair is not yet created"},
					{"swap_mode","number","Swap mode - madatory only if pair is not yet created (0,1,2)"}
				}}
			},{
					{200,"OK-Data",{{ctx_json,"","object"}}}
				}).method(me, &HttpAPI::post_api_editor);

	reg("/api/wallet")
		.GET("Editor","Shows current wallet state (balance and allocation)","",{},{})
			.method(me, &HttpAPI::get_api_wallet);
	server->addSwagBrowser("/swagger");

	reg("/api/utilization")
		.GET("Editor","Shows instance utilization","",{
				{"tm","query","int64","Time of last check"}
		},{{200,"OK",{{ctx_json,"","object","",{
				{"overall","object","overall time of one cycle. Note it can be less than sum of each traders",{
						{"dur","number","duration in milliseconds"},
						{"updated","boolean","true=value has been updated after last check"},
				}},
				{"tm","number","timestamp of current check, pass this value as argument for next check"},
				{"traders","assoc","Associative array of traders", {
						{"","object","",{
								{"dur","number","duration in milliseconds"},
								{"updated","boolean","true=value has been updated after last check"}}
						}
				}}}}}}
		})
			.method(me, &HttpAPI::get_api_utilization);


	reg("/api/trader")
		.GET("Trader","List of active traders", "",{},{{200,"OK"}}).method(me, &HttpAPI::get_api_trader);
	reg("/api/trader/{trader}")
		.GET("Trader","Get trader state", "",{
				traderId,{"vis","query","boolean","Include strategy visualisation",{},false}},{{200,"OK"}})
					.method(me, &HttpAPI::get_api_trader_trader)
		.DELETE("Trader","Stop trader, cancel all orders","",{traderId},"",{},{{202,"Accepted"},{404,"Not found"}})
					.method(me, &HttpAPI::delete_api_trader_trader);
	reg("/api/trader/{trader}/trading")
		.GET("Trader","Retrieve live data from exchange to allow manual trading, Call this function in period min 20s","",{traderId},{
				{200,"OK"}
			}).method(me, &HttpAPI::get_api_trader_trading)
		.POST("Trader","Create an order","",{traderId},"",{
				{ctx_json,"","object","Order parameters",{
						{"price","oneOf","Order price",{
								{"","enum","Ask or bid",{{"ask"},{"bid"}}},
								{"","number","price"}
						}},
						{"size","number","Order size"},
						{"replace_id","","ID of order to replace",{},true},
					}}
			},{{201,"Order created",{{ctx_json,"","string","New order ID"}}}})
			.method(me,&HttpAPI::post_api_trader_trading)
		.DELETE("Trader","Cancel one or all orders","",{traderId},"",{
				{ctx_json, "", "object","",{{"id","string","orderid",{},true}}},
				{"<empty>","Cancel all orders","string","Empty body"}},
				{{OpenAPIServer::ResponseObject{202,"Orders are deleted",{{ctx_json,"","boolean","true"}}}}
				}).method(me, &HttpAPI::delete_api_trader_trading);



	server->addSwagBrowser("/swagger");

}

