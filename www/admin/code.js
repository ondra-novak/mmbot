"use strict";

window.addEventListener('DOMContentLoaded', (event) => {
   app_start();
});

function app_start() {
	TemplateJS.View.lightbox_class = "lightbox";
	window.app = new App();
	window.app.init();
	
}

function parse_fetch_error(e) {
	var txt;
	if (e.headers) {
		var ct = e.headers.get("Content-Type");
		if (ct == "text/html" || ct == "application/xhtml+xml") {
			txt = e.text().then(function(text) {
			 	var parser = new DOMParser();
				var htmlDocument = parser.parseFromString(text, ct);
				var el = htmlDocument.body.querySelector("p");
				if (!el) el = htmlDocument.body;
				return el.innerText;
			});
		} else {
			txt = e.text() || "";
		}
	} else {
		txt = Promise.resolve(e.toString());
	}	
	return txt.then(function(t) {
			var msg = (e.status || "")+" "+(e.statusText || "");
			if (t == msg) t = "";
			return {msg:msg, t:t};
	});
} 

function fetch_error(e) {
	if (!fetch_error.shown) {
		fetch_error.shown = true;
		parse_fetch_error(e).then(function(t) {
			if (e.status == 401) {
				var txts = document.getElementById("strtable");
				app.dlgbox({text: txts.dataset.please_login},"network_error").then(function() {
					fetch_error.shown = false;
				});
			} else {
				app.dlgbox({text:t.msg, desc:t.t},"network_error").then(function() {
					fetch_error.shown = false;
				});
			}
		});
	}	
	throw e;	
}

function fetch_with_error(url, opt) {
	return fetch_json(url, opt).catch(fetch_error);
}

function App() {	
	this.traders={};
	this.config={};
	this.backtest={};
	this.curForm = null;
	this.advanced = false;
	this.ext_assets=[];
	this.modified={};
}

TemplateJS.View.prototype.showWithAnim = function(item, state) {	
	var elem = this.findElements(item)[0];
	var h = elem.classList.contains("hidden") || elem.hidden;
	if (state) {
		if (h) {
			elem.hidden = false;
			TemplateJS.waitForDOMUpdate().then(function(x) {
				elem.classList.remove("hidden");
			});
		}
	} else {
		if (!h) {
			elem.classList.add("hidden");
			var anim = new TemplateJS.Animation(elem);
			anim.wait().then(function(x) {
				if (elem.classList.contains("hidden")) 
					elem.hidden = true;
			});
		}
	}
}


App.prototype.createTraderForm = function() {
	var form = TemplateJS.View.fromTemplate("trader_form");
	var norm=form.findElements("goal_norm")[0];
	var pl = form.findElements("goal_pl")[0];
	form.dlgRules = function() {
		var state = this.readData(["strategy","advanced","check_unsupp"]);
		form.showItem("strategy_halfhalf",state.strategy == "halfhalf" || state.strategy == "keepvalue" || state.strategy == "exponencial"||state.strategy == "hypersquare"||state.strategy == "conststep");
		form.showItem("strategy_pl",state.strategy == "plfrompos");
		form.showItem("strategy_pile",state.strategy == "pile");
		form.showItem("strategy_kv2",state.strategy == "keepvalue2");
		form.showItem("strategy_gauss",state.strategy == "errorfn");
		form.showItem("strategy_keepbalance",state.strategy == "keep_balance");
		form.showItem("strategy_martingale",state.strategy == "martingale");
		form.showItem("strategy_gamma",state.strategy == "gamma");
		form.showItem("strategy_hedge",state.strategy == "hedge");
		form.showItem("strategy_sinhgen",state.strategy == "sinh_gen");
		form.showItem("strategy_passive_income",state.strategy == "passive_income");
		form.showItem("strategy_incvalue",state.strategy == "inc_value");
		form.showItem("strategy_hodl_short",state.strategy == "hodlshort");
		form.showItem("strategy_hyperbolic",["hyperbolic","linear","sinh","sinh_val","sinh2"].indexOf(state.strategy) != -1);
		form.showItem("kv_valinc_h",state.strategy == "keepvalue");
		form.showItem("show_curvature",["sinh","sinh_val","sinh2"].indexOf(state.strategy) != -1);
		form.showItem("item_asym", ["sinh","sinh_val","sinh2"].indexOf(state.strategy) == -1);
		var selstrat = form.findElements("strategy")[0];
		var strathid = selstrat.querySelectorAll("[data-hidden]");
		Array.prototype.forEach.call(strathid,function(item) {
			if (item.value != state.strategy) {
				var par = item.parentNode;
				par.removeChild(item);
				if (par.firstElementChild == null) par.parentNode.removeChild(par);
			}
		});
		form.setData({"help_goal":{"class":state.strategy}});
		form.getRoot().classList.toggle("no_adv", !state["advanced"]);
		form.getRoot().classList.toggle("no_experimental", !state["check_unsupp"]);
		form.getRoot().classList.toggle("paper", !!this._paper);
		if (form.onChangeStrategy) {
			form.onChangeStrategy(state);
		}
		return state;
	};
	form.setItemEvent("strategy","change", form.dlgRules.bind(form));
	form.setItemEvent("advanced","change", form.dlgRules.bind(form));
	form.setItemEvent("check_unsupp","change", form.dlgRules.bind(form));
	return form;
}

App.prototype.createTraderList = function(form) {
	if (!form) form = TemplateJS.View.fromTemplate("trader_list");
	var items = Object.keys(this.traders).map(function(x) {
		return {
			image:this.brokerImgURL(this.traders[x].broker),
			caption: this.traders[x].title,
			broker: this.traders[x].broker,
			id: "trader/"+this.traders[x].id,
			"":{"classList":{"paper":this.traders[x].paper_trading,"modified":!!this.modified[x]}}			

		};
	},this);
	items.sort(function(a,b) {
		return a.broker.localeCompare(b.broker);
	});
	if (this.news) {
		items.unshift({
			"image":"../res/messages.png",
			"caption":this.strtable.messages,				
			"id":"internal/news"	
		});
	}
	items.unshift({
		"image":"../res/wallet.png",
		"caption":this.strtable.wallet,				
		"id":"internal/wallet"		
	});

	items.unshift({
		"image":"../res/options.png",
		"caption":this.strtable.options,				
		"id":"internal/options"
		
	});
	items.unshift({
		"image":"../res/security.png",
		"caption":this.strtable.access_control,				
		"id":"internal/security"
		
	});
	items.push({
		"image":"../res/add_icon.png",
		"caption":"New trader",				
		"id":"internal/add"
	});
	items.forEach(function(x) {
		x[""] = Object.assign(x[""] || {},{"!click":function(id) {
			form.select(id);
		}.bind(null,x.id)});
	},this);
	form.setData({"item": items});
	form.select = function(id) {
		var update = items.map(function(x) {
			return {"":{"classList":{"selected": x.id == id}}};
		});
		form.setData({"item": update});
		
		var nf;
		var path = id.split("/");
		location.replace("#"+id);
		if (path[0]=="internal") {
			var iid = path[1];		
			if (iid == "security") {
				if (this.curForm) {
					this.curForm.save();				
					this.curForm = null;
				}
				nf = this.securityForm();
				this.desktop.setItemValue("content", nf);
				nf.save = function() {};

			} else if (iid == 'options') {
				if (this.curForm) {
					this.curForm.save();				
					this.curForm = null;
				}
				nf = this.optionsForm();
				this.desktop.setItemValue("content", nf);
				this.curForm = nf;

			} else if (iid == 'wallet') {
				if (this.curForm) {
					this.curForm.save();				
					this.curForm = null;
				}
				nf = this.walletForm();
				this.desktop.setItemValue("content", nf);
				this.curForm = nf;
			} else if (iid == 'news') {
				if (this.curForm) {
					this.curForm.save();				
					this.curForm = null;
				}
				nf = this.messagesForm();
				this.desktop.setItemValue("content", nf);
				this.curForm = nf;
			} else if (iid == "add") {
				this.brokerSelect().then(this.pairSelect.bind(this)).then(function(res) {
					var broker = res[0];
					var pair = res[1];
					var name = res[2];				
					var swap_mode = parseInt(res[3]);
					if (!this.traders[name]) this.traders[name] = {};
					var t = this.traders[name];
					t.broker = broker;
					t.pair_symbol = pair;
					t.id = name;
					t.enabled = true;
					t.swap_symbols = swap_mode;
					if (!t.title) t.title = pair;
					this.updateTopMenu(name);
				}.bind(this))
			}
		} else if (path[0] == "trader") {
			var iid = path[1];
			if (this.curForm) {
				this.curForm.save();				
				this.curForm = null;
			}

			nf = this.openTraderForm(iid);			

			this.desktop.setItemValue("content", TemplateJS.View.fromTemplate("main_form_wait"));
			
			nf.then(function (nf) {
				this.desktop.setItemValue("content", nf);
				this.curForm = nf;
				this.curForm.save = function() {
					this.traders[iid] = this.saveForm(nf, this.traders[iid]);
					var orig = (this.config && this.config.traders && this.config.traders[iid]) || {}
					var c = !compareObjects(this.traders[iid], orig);
					if (c) console.log({"new":createDiff(orig,this.traders[iid]),"old":createDiff(this.traders[iid],orig)});
					this.modified[iid] = c;
					this.updateTopMenu();
				}.bind(this);
				this.curTrader = this.traders[iid];
			}.bind(this));
		}
	}.bind(this);
	
	return form;
}

App.prototype.processConfig = function(x) {	
	this.config = x;
	this.org_config = JSON.parse(JSON.stringify(x));
	this.users = x["users"] || [];
	this.traders = Object.assign({},x["traders"] || {});
	this.ext_assets=x["ext_assets"] || []
	for (var id in this.traders) this.traders[id].id = id;
	this.modified={};
	return x;	
}

App.prototype.loadConfig = function() {
	return fetch_with_error("../api/admin/config").then(this.processConfig.bind(this));
}

App.prototype.brokerURL = function(broker) {
	return "../api/admin/brokers/"+encodeURIComponent(broker);
}
App.prototype.pairURL = function(broker, pair) {
	return this.brokerURL(broker) + "/pairs/" + encodeURIComponent(pair);	
}
App.prototype.brokerImgURL = function(broker) {
	return this.brokerURL(broker) + "/icon.png";	
}
App.prototype.traderURL = function(trader) {
	return "../api/admin/traders/"+encodeURIComponent(trader);
}
App.prototype.traderPairURL = function(trader, pair) {
	return "../api/admin/traders/"+encodeURIComponent(trader)+"/broker/pairs/" + encodeURIComponent(pair);	
}
App.prototype.reload_trader_header=function() {}

function defval(v,w) {
	if (v === undefined) return w;
	else return v;
}

function filledval(v,w) {
	if (v === undefined) return {value:w};
	else if (v == w) return {value:v};
	else {
		return {
			"value":v,
			"classList":{
				"changed":true
			}
		}
	}
}


App.prototype.fillForm = function (src, trg) {
	var data = {};	
	var initial_pos;
	var recalc_strategy_fn= function(){};
	data.id = src.id;
	data.title = src.title;
	data.symbol = src.pair_symbol;	
	if (!this.fillFormCache) this.fillFormCache = {} 
	var recalcStrategyFn = new DelayUpdate(function() {
			var data = trg.readData()
			var strategy = getStrategyData(data,trg._pair.invert_price);
			var req = {
					strategy:strategy,
					price:invPrice(trg._price, trg._pair.invert_price),
					assets:invSize(trg._assets, trg._pair.invert_price),
					currency:trg._backtest_balance,
					leverage:trg._pair.leverage,
					inverted: trg._pair.invert_price,
					trader:src.id
			};
			fetch_json("../api/admin/strategy", {method:"POST",body:JSON.stringify(req)}).then(
				function(r){
					trg.setData({range_min_price:adjNum(r.min), range_max_price:adjNum(r.max), range_initial:adjNum(r.initial)});
					initial_pos = adjNumN(r.initial);
				},function(e){
					console.error(e);
			});
		}.bind(this)
    );
	
	
	var first_fetch = true;

	var updateHdr = function(){
		var state = fetch_json("../api/admin/editor",{
			method:"POST",
			body: JSON.stringify({
				broker: src.broker,
				trader: src.id,
				pair: src.pair_symbol,
				swap_mode: src.swap_symbols
			})
		});
		return state.then(function(st) {
			var data = {};
			fillHeader(st,data);
			trg.setData(data);
			trg.setData({
				err_failed_to_fetch: {
					classList:{
						mark:false
					},
					value:""
				}
			});
		},function(err) {
			var ep = parse_fetch_error(err);
			ep.then(function(x) {
				var data = {
					err_failed_to_fetch:{
						value:x.msg+" - "+x.t,
						classList:{
							mark:true
						}
					},
					broker:"error"
				};				
				trg.setData(data);
			});
		});			
	}.bind(this);
	
	var fillHeader = function(state, data) {
				
	
		var broker = state.broker;
		var avail = state.allocations;
		var pair = state.pair;
		var ext_ass = state.ext_ass;
		var orders = state.orders;
		this.fillFormCache[src.id] = state;

		data.broker = broker.exchangeName;
		data.broker_id = broker.name;
		data.broker_ver = broker.version;
		data.broker_subaccount = broker.subaccount?"("+broker.subaccount+")":"";
		data.asset = pair.asset_symbol;
		data.currency = pair.currency_symbol;
		data.quote = pair.quote_currency
		data.balance_asset= adjNum(invSize(pair.asset_balance,pair.invert_price));		
		data.balance_currency = adjNum(pair.currency_balance);
		data.price= adjNum(invPrice(pair.price,pair.invert_price));
		data.accumulation = adjNum(state.accumulation);
		data.show_accum = {".hidden": state.accumulation == 0};
		data.fees =adjNum(pair.fees*100,4);
		data.leverage=pair.leverage?pair.leverage+"x":"n/a";
		data.type_spot={".hidden":pair.leverage != 0};
		data.type_leveraged={".hidden":pair.leverage == 0};
		data.type_inverted={".hidden":!pair.invert_price};
		data.hdr_position = adjNum(invSize(state.position,pair.invert_price));
		data.cur_pile_ratio = ((state.strategy && state.strategy.Ratio) || 0).toFixed(1);
		data.rpnl = adjNum(state.rpnl);
		data.upnl = adjNum(state.enter_price_pos?(pair.price-state.enter_price)*state.enter_price_pos:0);
		data.enter_price = adjNum(invPrice(state.enter_price,pair.invert_price));
		data.costs = adjNum(state.costs);
		trg._balance = pair.currency_balance+ext_ass.currency-avail.unavailable;
		trg._backtest_balance = pair.currency_balance>trg._balance?pair.currency_balance:trg._balance;
		trg._assets = state.position || pair.asset_balance;
		trg._price = invPrice(pair.price, pair.invert_price);
		trg._budget = avail.allocated;	
		trg._pair = pair;		

		data.balance_currency_free = adjNum(trg._balance);
		data.balance_currency_alloc = adjNum(trg._budget);



		var mp = orders?orders.map(function(z) {
			return {
				id: z.id,
				dir: invSize(z.size,pair.invert_price)>0?"BUY":"SELL",
				price:adjNum(invPrice(z.price, pair.invert_price)),
				size: adjNum(Math.abs(z.size))
			};}):[];

		if (mp.length) {
			var butt = document.createElement("button");
			butt.innerText="X";
			mp[0].action = {
					"rowspan":mp.length,
					"value": butt
			};
			butt.addEventListener("click", this.cancelAllOrders.bind(this, src.id));
		}
		
		data.orders = mp

		var fields = []

		function fill_recursive(path, node) {
			if (node === undefined) return;
			if (typeof node == "object") {
				if (node === null) return;
				Object.keys(node).forEach(function(k) {
					fill_recursive(path.concat(k), node[k]);
				});
			} else {
				fields.push({
					key: path.join("."),
					value: adjNumN(node)
				});
			}
		}
		if (state.strategy) fill_recursive([],state.strategy);

		data.strategy_fields = fields;
		data.icon_broker = {
					".hidden":!broker.settings,
					"!click":this.brokerConfig.bind(this, 
                        (state.exists?this.traderPairURL(src.id, src.pair_symbol):this.pairURL(broker.name,src.pair_symbol))+"/settings")
				};					
		data.hide_on_new = {
				".hidden":!state.exists,
			};
		data.icon_reset = {
				classList:{
					blink:state.need_initial_reset
				}
		};
		data.open_orders_sect = {".hidden":!mp.length};


			

		data.shg_control_pos ={
			".disabled":state.need_initial_reset,
		}

		
		if (first_fetch) {
			["strategy","external_assets","gs_external_assets", "hp_trend_factor","hp_allowshort","hp_power", "hp_recalc", "hp_asym","hp_powadj", 
			"hp_extbal", "hp_reduction","hp_dynred","sh_curv","gamma_exp","pincome_exp",
			"gamma_trend","gamma_fn",
			"shg_w","shg_p","shg_z","shg_lp","shg_offset",
			"pile_ratio","hodlshort_z",
			"incval_w","incval_z"
			]
			.forEach(function(item){
				trg.findElements(item).forEach(function(elem){
					elem.addEventListener("input", recalcStrategyFn.exec.bind(recalcStrategyFn));
					elem.addEventListener("change",  recalcStrategyFn.exec.bind(recalcStrategyFn));
				}.bind(this));
			}.bind(this));

			recalcStrategyFn.exec();
			
			data.vis_spread = {"!click": this.init_spreadvis.bind(this, trg, src.id), ".disabled":false};
			data.show_backtest= {"!click": this.init_backtest.bind(this, trg, src.id, src.pair_symbol, src.broker), ".disabled":false};
			data.inverted_price=pair.invert_price?"true":"false";
			var tmp = trg.readData(["cstep","max_pos","shg_lp","shg_offset"]);
			if (!src.strategy && typeof state.pair.price == "string" && state.pair.price.startsWith("trainer")){
			    this.brokerConfig(this.pairURL(broker.name, src.pair_symbol)+"/settings").then(updateHdr,updateHdr);
			}
			data.more_info={
				".hidden":false,
				"!click":function() {
					var mincursize = pair.min_volume/pair.price;
					var data={
						price:  adjNum(invPrice(pair.price,pair.invert_price)),
						tick_size:adjNum(pair.currency_step),
						quote_symbol:pair.quote_currency,
						asset_step:adjNum(pair.asset_step),
						asset_symbol:pair.asset_symbol,
						min_size:adjNum(pair.min_size),
						min_volume:adjNum(mincursize > pair.min_size?mincursize:pair.min_size),
						leverage:pair.leverage,
						currency_symbol:pair.currency_symbol,
						inverted_price:JSON.stringify(pair.invert_price)
					}					
					this.dlgbox(data,"pair_details");					
				}.bind(this)			
			};			
			if (trg._pair.invert_price) {
				if (tmp.shg_lp) data.shg_lp = -parseInt(tmp.shg_lp);
				if (tmp.shg_offset) data.shg_offset = -tmp.shg_offset;
			}
			data.shg_control_pos["!click"] = this.shgControlPosition.bind(this,src.id, trg);	        


			first_fetch = false;
		}
		if (state.visstrategy) (function(){
			var p = state.visstrategy.points;
			if (Array.isArray(p) && p.length) {
				var c = state.visstrategy.current;
				var elem = trg.findElements("visstr")[0];
				elem.innerText = "";
				var chart = new XYChart(600,400);
				var yld = p.map(function(x){return [x.x, x.y];});
				var bg = p.map(function(x){return [x.x,x.b]});
				var hld = p.map(function(x){return [x.x,x.h]});
				var tng = state.visstrategy.tangent;
				chart.initRange(yld.concat([[c.x,0]]));
				chart.drawArea(yld,"yield");
				chart.drawLines(yld,"yield");
				chart.initRange(bg.filter(function(x) {return x[1]>=0;}).concat([[c.x,c.h],[c.x,0]]));
				chart.drawAxes("axes");
				chart.drawLines(bg,"budget");			
				chart.drawLines(hld,"held");	
				chart.drawLines(tng,"tangent");
				chart.drawVLine(c.x,"current","p");
				chart.drawVLine(state.visstrategy.neutral,"neutral","n");
				chart.drawPoint(c.x,c.h,"current held");
				chart.drawPoint(c.x,c.b,"current budget");
				elem.appendChild(chart.elem);
			}
		}).call(this);
		
		
	}.bind(this);
	
	if (this.fillFormCache[src.id]) {
	setTimeout(function() {
			var data= {};
			fillHeader(this.fillFormCache[src.id], data);
			trg.setData(data);
	}.bind(this),1);
	}
	data.broker_img = this.brokerImgURL(src.broker);
	data.advanced = this.advanced;

	trg._paper = src.paper_trading;
	trg._swap_symbols = src.swap_symbols;
	data.is_paper_trading = src.paper_trading?"true":"false";
	data.strategy = (src.strategy && src.strategy.type) || "";
	data.cstep = 0;
	data.acum_factor = 0;
	data.external_assets = 0;
	data.adj_timeout=5;
	data.kv_valinc = 0;
	data.kv_halfhalf=false;
	data.hp_reduction=0;
	data.sh_curv=1;
	data.hp_limit=1;
	data.hp_asym=0;
	data.hp_power=1;
	data.hp_powadj=0;
	data.hp_dynred=0;
	data.hp_trend_factor=0;
	data.hp_lb_asym="asym";
	data.inverted_price="false";
	data.hp_allowshort=false;
	data.hp_reinvest=false;
	data.gs_rb_lo_p=25;
	data.gs_rb_lo_a=50;
	data.gs_rb_hi_p=55;
	data.gs_rb_hi_a=1;
	data.hp_fastclose=true;
	data.hp_slowopen=false;
	data.max_leverage = 5;
	data.secondary_order = 0;
	data.kb_keep_min = 0;
	data.kb_keep_max = 100;
	data.gamma_exp=2;
	data.gamma_reinvest=false;
	data.gamma_maxrebal=false;
	data.gamma_rebalance=1;
	data.gamma_trend=0;
	data.gamma_fn="halfhalf";
	data.hedge_drop=1;
	data.hedge_long=true;
	data.hedge_short=true;
	data.shg_w=50;
	data.shg_p=50;
	data.shg_b=0;
	data.shg_z=0;
	data.shg_ol=0;
	data.shg_olt={value:"-1"};
	data.shg_lp={value:"0"};
	data.shg_rnv=false;
	data.shg_avgsp=false;
	data.shg_lazyopen=false;
	data.shg_lazyclose=false;
	data.shg_boostmode=0;
	data.shg_offset=0;
	data.shg_show_asym={".hidden":false};
	data.pincome_exp = 40;
	data.pile_ratio=50;
	data.pile_accum=0;
	data.kv2_accum=0;
	data.kv2_chngtm=0;
	data.kv2_boost=false;
	data.kv2_reinvest=false;
	data.hodlshort_z = 1.0;
	data.hodlshort_b = 100;
	data.hodlshort_acc = 0;
	data.hodlshort_rinvst= false;
	data.incval_w = 1;
	data.incval_z = 1;
	data.incval_r = 100;
	data.incval_ms = 0;
	data.incval_ri = false;


	
	if (data.strategy == "halfhalf" || data.strategy == "keepvalue" || data.strategy == "exponencial"|| data.strategy == "hypersquare"||data.strategy == "conststep") {
		data.acum_factor = filledval(defval(src.strategy.accum,0)*100,0);
		data.external_assets = filledval(src.strategy.ea,0);
		data.kv_valinc = filledval(src.strategy.valinc,0);
		data.kv_halfhalf = filledval(src.strategy.halfhalf,false);
	} else if (data.strategy == "keep_balance" ) {
		data.kb_keep_min = filledval(src.strategy.keep_min,0);
		data.kb_keep_max = filledval(src.strategy.keep_max,0);
	} else if (data.strategy == "errorfn" ) {
		data.gs_external_assets = filledval(src.strategy.ea,0);
		data.gs_rb_lo_p=filledval(defval(src.strategy.rb_lo_p,0.1)*100,10);
		data.gs_rb_hi_p=filledval(defval(src.strategy.rb_hi_p,0.95)*100,95);
		data.gs_rb_lo_a=filledval(defval(src.strategy.rb_lo_a,0.5)*100,50);
		data.gs_rb_hi_a=filledval(defval(src.strategy.rb_hi_a,0.5)*100,50);
	} else if (["hyperbolic","linear","sinh","sinh_val","sinh2"].indexOf(data.strategy) != -1) {
		data.hp_reduction = filledval(defval(src.strategy.reduction,0.25)*200,0);
		data.hp_limit = filledval(src.strategy.limit,0);
		data.hp_asym = filledval(defval(src.strategy.asym,0.0)*100,0);
		data.hp_recalc= filledval(src.strategy.recalc_mode,"position");
		data.hp_power = filledval(src.strategy.power,1);
		data.hp_powadj = filledval(src.strategy.powadj,0);
		data.hp_dynred = filledval(src.strategy.dynred,0);
		data.hp_extbal = filledval(src.strategy.extbal,0);
		data.hp_trend_factor = filledval(defval(src.strategy.trend_factor,0)*100,0);
		data.hp_allowshort = src.strategy.longonly == undefined?false:!src.strategy.longonly
		data.hp_reinvest= filledval(src.strategy.reinvest_profit,false);
		data.sh_curv = filledval(src.strategy.curv,5);
		data.hp_fastclose = filledval(src.strategy.fastclose,true);
		data.hp_slowopen = filledval(src.strategy.slowopen,false);
	} else if (data.strategy == "gamma") {
		data.gamma_fn = filledval(src.strategy.function,"halfhalf");
		data.gamma_exp = filledval(src.strategy.exponent,2);
		data.gamma_rebalance = filledval(src.strategy.rebalance,1);
		data.gamma_trend = filledval(src.strategy.trend,0);
		data.gamma_reinvest = filledval(src.strategy.reinvest,false);
		data.gamma_maxrebal = filledval(src.strategy.maxrebalance,false);
	} else if (data.strategy == "passive_income") {
		data.pincome_exp = filledval(src.strategy.exponent,40);
	} else if (data.strategy == "hodlshort") {
		data.hodlshort_z = filledval(src.strategy.z,1);
		data.hodlshort_b = filledval(src.strategy.b,100);
		data.hodlshort_acc = filledval(src.strategy.acc,0);
		data.hodlshort_rinvst = filledval(src.strategy.rinvst,false);
	} else if (data.strategy == "hedge") {
		data.hedge_drop = filledval(src.strategy.drop,1);
		data.hedge_long = filledval(src.strategy.long,true);
		data.hedge_short = filledval(src.strategy.short,true);
	} else if (data.strategy == "sinh_gen") {
		data.shg_w=filledval(src.strategy.w,50);
		data.shg_p=filledval(src.strategy.p,100);
		data.shg_b=filledval(src.strategy.b,0);
		data.shg_z=filledval(src.strategy.z,0);
		data.shg_lp=filledval(src.strategy.disableSide,0);
		data.shg_rnv=filledval(src.strategy.reinvest,false);
		data.shg_avgsp=filledval(src.strategy.avgspread,false);
		data.shg_lazyopen=filledval(src.strategy.lazyopen,false);
		data.shg_lazyclose=filledval(src.strategy.lazyclose,false);
		data.shg_boostmode=filledval(src.strategy.boostmode,0);
		data.shg_ol=filledval(defval(Math.abs(src.strategy.openlimit),0),0);
		if (!src.strategy.openlimit || src.strategy.openlimit==0) data.shg_ol.disabled = true;
		data.shg_olt={value:src.strategy.openlimit>0?1:src.strategy.openlimit<0?-1:0};
		data.shg_offset = filledval(src.strategy.offset,0);
		data.shg_show_asym = {".hidden": src.strategy.disableSide != 0};		
	} else if (data.strategy == "pile") {
		data.pile_accum = filledval(src.strategy.accum,0);
		data.pile_ratio = filledval(src.strategy.ratio,0);
	} else if (data.strategy == "keepvalue2") {
		data.kv2_accum = filledval(src.strategy.accum,0);
		data.kv2_boost = filledval(src.strategy.boost,false);
		data.kv2_chngtm = filledval(src.strategy.chngtm, 0);
		data.kv2_reinvest = filledval(src.strategy.reinvest, false);
	} else if (data.strategy == "inc_value") {
		data.incval_w = filledval(src.strategy.w,1);
		data.incval_z = filledval(src.strategy.z,1);
		data.incval_r = filledval(src.strategy.r,100);
		data.incval_ms = filledval(src.strategy.ms,0);
		data.incval_ri = filledval(src.strategy.ri,false);
	}
	data.shg_olt["!change"] = function() {
		trg.enableItem("shg_ol", this.value != "0");
		if (!trg.readData(["shg_ol"]).shg_ol) trg.setData({shg_ol:2});
	};
	data.shg_lp["!change"] = function() {
		trg.showItem("shg_show_asym", this.value == "0");
	};
	data.enabled = src.enabled;
	data.hidden = !!src.hidden;	
	data.accept_loss = filledval(src.accept_loss,0);
	data.grant_trade_hours= filledval(src.grant_trade_hours,0);
	data.spread_calc_stdev_hours = filledval(src.spread_calc_stdev_hours,4);
	data.spread_calc_sma_hours = filledval(src.spread_calc_sma_hours,25);
	data.dynmult_raise = filledval(src.dynmult_raise,440);
	data.dynmult_cap = filledval(src.dynmult_cap,100);
	data.dynmult_fall = filledval(src.dynmult_fall, 5);
	data.dynmult_mode = filledval(src.dynmult_mode, "independent");
	data.dynmult_sliding = filledval(src.dynmult_sliding,false);
	data.spread_freeze = filledval(src.spread_freeze,false);
	data.dynmult_mult = filledval(src.dynmult_mult, true);
	data.spread_mult = filledval(Math.log(defval(src.buy_step_mult,1))/Math.log(2)*100,0);
	data.min_size = filledval(src.min_size,0);
	data.max_size = filledval(src.max_size,0);
	data.secondary_order = filledval(src.secondary_order,0);
	data.dont_allocate = filledval(src.dont_allocate,false);
	data.report_order = filledval(src.report_order,0);
	data.force_spread = filledval((Math.exp(src.force_spread || Math.log(1.01))*100-100).toFixed(3),"1.000");
	data.spread_mode = filledval(src.force_spread?"fixed":"adaptive","adaptive");
	data.max_balance = filledval(src.max_balance,"");
	data.min_balance = filledval(src.min_balance,"");
	data.max_leverage = filledval(src.max_leverage,5);
	data.reduce_on_leverage = filledval(src.reduce_on_leverage, false);
	data.adj_timeout = filledval(src.adj_timeout,60);
	data.emul_leverage = filledval(src.emulate_leveraged,0);
	data.trade_within_budget = filledval(src.trade_within_budget,false);
	data.max_costs = filledval(src.max_costs, "");
	data.init_open = filledval(src.init_open, 0);

	
	data.icon_reset={"!click": function() {
		this.resetTrader(src.id,initial_pos, trg._budget, trg._balance);
		}.bind(this)};
	data.icon_clearStats={"!click": this.clearStatsTrader.bind(this, src.id)};
	data.icon_delete={"!click": this.deleteTrader.bind(this, src.id)};
	data.icon_undo={"!click": this.undoTrader.bind(this, src.id)};
	data.icon_trading={"!click":this.tradingForm.bind(this, src.id)};
	data.icon_share={"!click":this.shareForm.bind(this, src.id, trg)};
	data.icon_paper={"!click":this.paperTrading.bind(this, src.id, trg)};
	data.advedit = {"!click": this.editStrategyState.bind(this, src.id)};
	data.spread_mode["!change"] = function() {
		trg.setItemValue(this.dataset.name,this.value);
	};
	data.dynmult_mode["!change"] = function() {
		trg.setItemValue(this.dataset.name,this.value);
	};
	
    var timeout_id = null;

	function refresh_hdr() {
		if (trg.getRoot().isConnected) {
			var x = updateHdr().catch(function(){});
			timeout_id = setTimeout(refresh_hdr,60000);
			return x;
		} else {
			return Promise.reject();
		}
	}
	this.reload_trader_header = function(){
		if (timeout_id) clearTimeout(timeout_id);
		refresh_hdr().then(function() {
			recalc_strategy_fn();
		},function(){})
	};

	function unhide_changed(x) {
	
		var root = trg.getRoot();
		var items = root.getElementsByClassName("changed");
		Array.prototype.forEach.call(items,function(x) {
			while (x && x != root) {
				x.classList.add("unhide");
				x = x.parentNode;
			}
		});

		var inputs = trg.getRoot().querySelectorAll("input,select");
		Array.prototype.forEach.call(inputs, function(x) {
			function markModified() {
				var n = this;
				while (n && n != trg.getRoot())  {
					n.classList.add("modified");
					n = n.parentNode;
				}
			}
			x.addEventListener("change", markModified );
		});	

		refresh_hdr();
		
		return x;
	}

	

	return trg.setData(data).catch(function(){}).then(unhide_changed.bind(this)).then(trg.dlgRules.bind(trg));
}

function getStrategyData(data, inv) {
	var strategy = {};
	strategy.type = data.strategy;
	if (data.strategy == "halfhalf" || data.strategy == "keepvalue" || data.strategy == "exponencial"|| data.strategy == "hypersquare"||data.strategy == "conststep") {
		strategy.accum = data.acum_factor/100.0;
		strategy.ea = data.external_assets;
		strategy.valinc = data.kv_valinc;
		strategy.halfhalf = data.kv_halfhalf;
	} else if (data.strategy == "errorfn") {
		strategy = {
				type: data.strategy,
				ea: data.gs_external_assets,
				rb_hi_a: data.gs_rb_hi_a/100,
				rb_lo_a: data.gs_rb_lo_a/100,
				rb_hi_p: data.gs_rb_hi_p/100,
				rb_lo_p: data.gs_rb_lo_p/100,
		};
	} else if (data.strategy == "gamma") {
		strategy = {
			type: data.strategy,
			function: data.gamma_fn,
			exponent: data.gamma_exp,
			rebalance: data.gamma_rebalance,
			trend:data.gamma_trend,
			reinvest:data.gamma_reinvest,
			maxrebalance:data.gamma_maxrebal
		};		
	} else if (data.strategy == "passive_income") {
		strategy = {
			type: data.strategy,
			exponent: data.pincome_exp
		};	
	} else if (data.strategy == "hodlshort") {
		strategy = {
			type: data.strategy,
			z: data.hodlshort_z,
			b: data.hodlshort_b,
			acc: data.hodlshort_acc,
			rinvst: data.hodlshort_rinvst
		};	
	} else if (data.strategy == "pile") {
		strategy = {
			type: data.strategy,
			accum: data.pile_accum,
			ratio: data.pile_ratio,
		};
	} else if (data.strategy == "keepvalue2") {
		strategy = {
			type: data.strategy,
			accum: data.kv2_accum,
			chngtm: data.kv2_chngtm,
			boost: data.kv2_boost,
			reinvest: data.kv2_reinvest
		};
	} else if (data.strategy == "inc_value") {
		strategy = {
			type: data.strategy,
			r: data.incval_r,
			w: data.incval_w,
			z: data.incval_z,
			ms: data.incval_ms,
			ri: data.incval_ri
		}
	} else if (data.strategy == "hedge") {
		strategy = {
			type: data.strategy,
			drop: data.hedge_drop,
			long: data.hedge_long,
			short: data.hedge_short,
		};		
	} else if (data.strategy == "sinh_gen") {
		strategy = {
			type: data.strategy,
			w:data.shg_w,
			b:data.shg_b,
			p:data.shg_p,
			z:data.shg_z,
			openlimit:parseFloat(data.shg_olt)*data.shg_ol,
			disableSide:inv?""+(-parseInt(data.shg_lp)):data.shg_lp,
			reinvest:data.shg_rnv,
			avgspread:data.shg_avgsp,
			lazyopen:data.shg_lazyopen,
			lazyclose:data.shg_lazyclose,
			boostmode:data.shg_boostmode,
			offset: (inv?-1.0:1.0)*data.shg_offset
		};		
	} else 	if (["hyperbolic","linear","sinh","sinh_val","sinh2"].indexOf(data.strategy) != -1) {
		strategy = {
				type: data.strategy,
				power: data.hp_power,
				powadj: data.hp_powadj,
				dynred: data.hp_dynred,
				extbal: data.hp_extbal,
				trend_factor: data.hp_trend_factor*0.01,
				longonly: !data.hp_allowshort,
				reinvest_profit: data.hp_reinvest,
				recalc_mode: data.hp_recalc,
				asym: data.hp_asym / 100,
				reduction: data.hp_reduction/200,
				limit: data.hp_limit,
				curv: data.sh_curv,
				slowopen: data.hp_slowopen,
				fastclose: data.hp_fastclose,
				
		};
	} else if (data.strategy == "keep_balance") {
		strategy = {
				type: data.strategy,
				keep_min: data.kb_keep_min < data.kb_keep_max?data.kb_keep_min:data.kb_keep_max,
				keep_max: data.kb_keep_min > data.kb_keep_max?data.kb_keep_min:data.kb_keep_max
		};
	}
	return strategy;
}

App.prototype.saveForm = function(form, src) {

	var data = form.readData();
	var trader = {}
	var goal = data.goal;
	trader.strategy = getStrategyData(data, form._pair && form._pair.invert_price);
	trader.id = src.id;
	trader.broker =src.broker;
	trader.pair_symbol = src.pair_symbol;
	trader.title = data.title;
	trader.enabled = data.enabled;	
	trader.hidden = data.hidden;	
	this.advanced = data.advanced;
	trader.accept_loss = data.accept_loss;
	trader.grant_trade_hours = data.grant_trade_hours;
	trader.spread_calc_stdev_hours =data.spread_calc_stdev_hours ;
	trader.spread_calc_sma_hours  = data.spread_calc_sma_hours;
	trader.dynmult_raise = data.dynmult_raise;
	trader.dynmult_fall = data.dynmult_fall;
	trader.dynmult_mode = data.dynmult_mode;
	trader.dynmult_sliding = data.dynmult_sliding;
	trader.spread_freeze = data.spread_freeze;
	trader.dynmult_mult = data.dynmult_mult;
	trader.dynmult_cap = data.dynmult_cap;
	trader.buy_step_mult = Math.pow(2,data.spread_mult*0.01)
	trader.sell_step_mult = Math.pow(2,data.spread_mult*0.01)
	trader.min_size = data.min_size;
	trader.max_size = data.max_size;
	trader.max_leverage = data.max_leverage;
	trader.secondary_order = data.secondary_order;
	trader.dont_allocate = data.dont_allocate;
	trader.report_order = data.report_order;
	if (data.spread_mode == "fixed") {
		trader.force_spread = Math.log(data.force_spread/100+1);
	}
	trader.emulate_leveraged = data.emul_leverage;
	trader.trade_within_budget = data.trade_within_budget;
	trader.adj_timeout = data.adj_timeout;
	trader.reduce_on_leverage = data.reduce_on_leverage;
	if (isFinite(data.min_balance)) trader.min_balance = data.min_balance;
	if (isFinite(data.max_balance)) trader.max_balance = data.max_balance;
	if (isFinite(data.max_costs)) trader.max_costs = data.max_costs;
	trader.init_open = data.init_open;
	if (src.paper_trading) trader.paper_trading = true;
	if (src.pp_source) trader.pp_source = src.pp_source;
	if (typeof src.swap_symbols == "boolean") trader.swap_symbols = src.swap_symbols?2:0;
	else trader.swap_symbols = src.swap_symbols;

	return trader;
	
}

App.prototype.openTraderForm = function(trader) {
	var form = this.createTraderForm();
	var p = this.fillForm(this.traders[trader], form);
	return Promise.resolve(form);	
}


App.prototype.init = function() {
	this.strtable = document.getElementById("strtable").dataset;
	this.desktop = TemplateJS.View.createPageRoot(true);
	this.desktop.loadTemplate("desktop");
	var top_panel = TemplateJS.View.fromTemplate("top_panel");
	this.top_panel = top_panel;
	this.desktop.setItemValue("top_panel", top_panel);
	top_panel.setItemEvent("save","click", this.save.bind(this));
	top_panel.setItemEvent("login","click", function() {
		location.href="../api/admin/login";
	});
		
	
	return Promise.all([
	        this.loadConfig(),
	        fetch("../api/admin/news")]
	      ).then(function(x) {
	    this.news = x[1].status == 200;
	    if (this.news) this.news_content = x[1].json();
		top_panel.showItem("login",false);
		top_panel.showItem("save",true);
		var menu = this.createTraderList();
		this.menu =  menu;
		this.desktop.setItemValue("menu", menu);
		this.stopped = {};
		var h = location.hash;
		if (h.startsWith("#")) {
			menu.select(h.substr(1));
		}
		
		
	}.bind(this));
	
}

App.prototype.brokerSelect = function() {
	var _this = this;
	return new Promise(function(ok, cancel) {
		
		var form = TemplateJS.View.fromTemplate("broker_select");
		_this.waitScreen(fetch_with_error("../api/admin/brokers/_all")).then(function(x) {
			form.openModal();
			var lst = function(s){
				return x.filter(function(z){return !s || z.subaccounts;})
				.map(function(z) {
					return {
						image:_this.brokerImgURL(z.name),
						caption: z.exchangeName+(z.subaccount?" ("+z.subaccount+")":""),
						"":{"!click": function() {
								var p;
								if (s) {
									p = this.createSubaccount(z.name).then(
									function(n){
										return this.waitScreen(fetch_with_error(this.brokerURL(n))).then(
											function(r){
												return [n, r.trading_enabled];
											})
									}.bind(this))
								} else {
									p = Promise.resolve([z.name, z.trading_enabled]);
								}
								p = p.then(function(v){									
									if (!v[1]) {
										return this.setApiKeyDlg(v[0]).then(function(){return v[0];});
									} else {
										return v[0];
									}																		
								}.bind(this));
								p.then(function(n){
									form.close();
									ok(n);
								});
							}.bind(this),
							classList:{"available":z.trading_enabled}
						}							
					}	
				},this)
				.sort(function(a,b){return a.caption.localeCompare(b.caption);})
			}.bind(this);
			form.setData({
				"item":lst(false),
				"subaccount":{
					value:false,
					"!change":function(){
						form.setData({"item":lst(form.readData(["subaccount"]).subaccount)})
					}
				}
			});
			form.setCancelAction(function() {
				form.close();
				cancel();
			},"cancel");

		}.bind(this));
	}.bind(this));
}

App.prototype.pairSelect = function(broker) {
	var _this = this;
	return new Promise(function(ok, cancel) {
		var form = TemplateJS.View.fromTemplate("broker_pair");
		form.openModal();
		form.setCancelAction(function() {
			form.close();
			cancel();
		},"cancel");
		form.setDefaultAction(function() {
			form.close();
			var d = form.readData();
			if (!d.pair || !d.name) return;
			var name = d.name.replace(/[^-a-zA-Z0-9_.~]/g,"_");
			ok([broker, d.pair, name, d.swap_mode]);
		},"ok");

        function onSelectPair(pair) {
        	form.setItemValue("pair", p);
        }

		fetch_with_error(_this.pairURL(broker,"")).then(function(data) {
		    var m;
            if (data.struct) m = data.struct;
            else 
                m = data.entries.reduce(function(a,b){
                	a[b] = b;return a;},{}
                );

            function refreshItems() {
            	var cur = form.readData(["sect"]).sect;
            	var path = cur?cur.map(function(z) {
            		return z["pair"];
            	}):[]
            	var sect = [];
            	var p =m;
            	var nxt;
            	for (var i = 0; i < path.length; i++) {
            		if (p[path[i]] === undefined) break;
            		var keys = Object.keys(p); 
            		sect.push ({
            			pair: {
            				"classList":{"noarrow":keys.length < 2},
            				"value":path[i],
            				"!change": refreshItems
            			},
            			item: keys.map(function(k) {
            				return {"":{".value": k, ".label":k,".selected":path[i] == k}}
            			})});
            		p = p[path[i]];
            		if (typeof p != "object") break;
            	}
            	while (typeof p == "object" && Object.keys(p).length == 1) {
            		var k = Object.keys(p)[0];
            		var v = p[k];
            		sect.push({
            			item: [{"":{".value":k, ".label": k, ".selected":true}}],
            			pair:{"classList":{"noarrow":true}}
            		});
            		p = p[k];
            	}
            	if (typeof p == "object") {            		
            		sect.push ({
            			pair: {
            				"classList":{"noarrow":false},
            				"value":"",
            				"!change": refreshItems
            			},
            			item: [{"":{".value":"",".label":"---",".selected":true}}].concat(Object.keys(p).map(function(k) {
            				return {"":{".value": k, ".label":k}} 
            			}))});           		
            	}
            	form.setData({"sect":sect});
            	if (typeof p == "string") {
                    form.setItemValue("pair", p);
            	} else {
                    form.setItemValue("pair", "");
            	}
            	dlgRules();
            }

            refreshItems();

            form.showItem("spinner",false);
		},function() {form.close();cancel();});
		form.setItemValue("image", _this.brokerImgURL(broker));
		var last_gen_name="";
		var prev_pair="";
		form.setData({
		    "swap_ui":{".style.visibility":"hidden"},
			"swap_mode":{
				"value":"0",
				"!change":function() {
					form.setData({"swap_mode":this.dataset.value});
				}	
			}
		});
		function dlgRules() {
			var d = form.readData(["pair","name"]);
			if (d.name == last_gen_name) {
				d.name = last_gen_name = broker+"_"+d.pair;
				form.setItemValue("name", last_gen_name);
			}
			form.enableItem("ok", d.pair != "" && d.name != "");			
            if (prev_pair != d.pair) {
            	prev_pair = d.pair;
            	if (d.pair == "") {
					form.setData({
						"swap_ui":{".style.visibility":"hidden"},
						"swap_mode":"0"
					});
                } else {
					fetch_json(_this.pairURL(broker, d.pair)).then(function(x){
						var data = {};
						data.asset = x.asset_symbol;
						data.currency = x.currency_symbol;	
						data.swap_ui = {".style.visibility":x.leverage?"hidden":"visible"};		
						if (x.leverage) {
							data.swap_mode = 0;
						}
						form.setData(data);
					});
                }
            	                
            }
		};
		
		
		form.setItemEvent("pair","change",dlgRules);
		form.setItemEvent("name","change",dlgRules);
		dlgRules();
	});
	
}

App.prototype.updateTopMenu = function(select) {
	this.createTraderList(this.menu);
	if (select) {        
		this.menu.select("trader/"+select);
	}
}

App.prototype.waitScreen = function(promise) {	
	var d = TemplateJS.View.fromTemplate('waitdlg');
	d.openModal();
	return promise.then(function(x) {
		d.close();return x;
	}, function(e) {
		d.close();throw e;
	});
}

App.prototype.cancelAllOrdersNoDlg = function(id) { 	

		var tr = this.traders[id];
		var cr = fetch_with_error(
			this.traderPairURL(id, tr.pair_symbol)+"/orders",
			{method:"DELETE"});
		var dr = fetch_json(
			this.traderURL(tr.id)+"/stop",
			{method:"POST"}).catch(function() {});


		return this.waitScreen(Promise.all([cr,dr])).catch(
			function(){}).then(
			function() {
				this.stopped[id] = true;				
			}.bind(this));

													
}

App.prototype.deleteTrader = function(id) {
	return this.dlgbox({"text":this.strtable.askdelete},"confirm").then(function(){
			this.curForm.close();
			delete this.traders[id];
			this.updateTopMenu();
			this.curForm = null;
	}.bind(this));
}

App.prototype.undoTrader = function(id) {
	this.curForm.close();
	this.curForm = null;
	this.updateTopMenu(id);
}

App.prototype.clearStatsTrader = function(id) {
	var dlg = TemplateJS.View.fromTemplate("clear_stats");
	dlg.openModal();
	dlg.setDefaultAction(function(){
		var d = dlg.readData();
		var tr = this.traders[id];
		this.waitScreen(fetch_with_error(
			this.traderURL(tr.id)+"/clear_stats",
				{method:"POST",body:JSON.stringify(d.mode)}).then(function() {
					this.updateTopMenu(tr.id);					
				}.bind(this))
		);
		dlg.close();
	}.bind(this),"ok");
	dlg.setCancelAction(function(){
		dlg.close();
	},"cancel")
}

App.prototype.resetTrader = function(id, initial, budget, balance) {
		var form=TemplateJS.View.fromTemplate("reset_strategy");
		var view;
		if (balance<0) balance = 100;
		if (budget <=0 || budget > balance) budget = balance;
		var p = this.dlgbox({rpos:{
			value:initial,			
            "!click":function(){
                view.setItemValue("setpos",this.value);	
            }},
            accept_loss:false,
            cur_pct:{
            	"data-min":0,
            	"data-max":balance,
            	"data-fixed":balance>10000?0:balance>=1?2:8,
            	"data-mult":balance>10000?1:balance>=1?0.01:0.00000001,
            	"value":budget,
            	"!change":function() {
            		var d = view.readData(["cur_pct"]);
            		var p = d.cur_pct/balance;
            		view.setData({"rpos":adjNumN(initial * p)});
            	}}
			},"reset_strategy");

		p.then(function(){
			var tr = this.traders[id];
			var data = view.readData();
			var p = data.cur_pct/balance*100;
			var req = {
                "achieve":data.setpos,
                "alert":data.accept_loss,
                "cur_pct":p,
            }
				
			this.waitScreen(fetch_with_error(
				this.traderURL(tr.id)+"/reset",
				{method:"POST",body:JSON.stringify(req)})).then(function() {
							this.updateTopMenu(tr.id);				
				}.bind(this));
	}.bind(this));
    view = p.view;
    return p;
    
}

App.prototype.cancelAllOrders = function(id) {
	return this.dlgbox({"text":this.strtable.askcancel,
		"ok":this.strtable.yes,
		"cancel":this.strtable.no},"confirm").then(this.cancelAllOrdersNoDlg.bind(this,id)).then(
			function() {
						this.updateTopMenu(id);
					}.bind(this));

}

App.prototype.addUser = function() {
	return new Promise(function(ok,cancel) {
		var dlg = TemplateJS.View.fromTemplate("add_user_dlg");
		dlg.openModal();
		dlg.setCancelAction(function(){
			dlg.close();
			cancel();
		},"cancel");
		dlg.setDefaultAction(function(){
			dlg.close();
			var data = dlg.readData();
			var dlg2 = TemplateJS.View.fromTemplate("password_dlg");
			dlg2.openModal();
			dlg2.setData(data);
			dlg2.setCancelAction(function() {
				dlg2.close();
				cancel();
			},"cancel");
			dlg2.setDefaultAction(function() {
				dlg2.unmark();
				var data2 = dlg2.readData();
				if (data2.pwd == "" ) return
				if (data2.pwd2 == "" ) {
					dlg2.findElements("pwd2")[0].focus();
					return;
				}
				if (data2.pwd  != data2.pwd2) {
					dlg2.mark("errpwd");
				} else {
					ok({
						username:data.username,
						password:data2.pwd,
						comment:data.comment
					});
					dlg2.close();
				}				
			},"ok");
		},"ok");
	});
};

App.prototype.createSubaccount = function(z) {
return new Promise(function(ok,cancel) {
    var dlg = TemplateJS.View.fromTemplate("subaccount_dlg");
	dlg.openModal();
	dlg.setCancelAction(function(){dlg.close();cancel();},"cancel");
	dlg.setDefaultAction(function(){
		var d = dlg.readData();
		if (d.name) {
			dlg.enableItem("ok",false);
			fetch_with_error(this.brokerURL(z)+"/subaccount",{method:"POST",body:JSON.stringify(d.name)}).then(
			function(s){
				dlg.close();
				ok(s);
			}.bind(this))
		}
	}.bind(this),"ok");
	dlg.setItemEvent("name","input",function(){
		dlg.enableItem("ok",!!this.value.length);
	})
	dlg.enableItem("ok",false);
}.bind(this));
};

App.prototype.setApiKeyDlg = function(z) {
	return new Promise(function(ok, cancel) {
	var url = this.brokerURL(z)+"/apikey";
	var w = fetch_with_error(url).then(
		function(ff) {
			return formBuilder(ff)
		},cancel);
		
	var lnk = fetch_with_error(this.brokerURL(z)).then(function(x) {
		return {href:x.exchangeUrl,".hidden":false,"target":"_blank"};
	});
	var dlg = TemplateJS.View.fromTemplate("broker_keys");
	dlg.setData({"form":w,"image":this.brokerImgURL(z), link:lnk});
	dlg.setDefaultAction(function(){
		
		w.then(function(f){
			var k = f.readData();
			return fetch_with_error(url, {"method":"PUT","body":JSON.stringify(k)})
		}).then(function(){
			dlg.close();
			ok();
		})
		
	}.bind(this),"ok");
	dlg.setCancelAction(function(){
		dlg.close();
		cancel();
	},"cancel");
	dlg.openModal();
		
}.bind(this));
}


App.prototype.eraseApiKeyDlg = function(z,name) {
	var strtable = document.getElementById("strtable");
	var txt = strtable.dataset.askdeletekey+": "+name;
	return this.dlgbox({"text":txt},"confirm").then(function(){
		this.waitScreen(fetch_with_error(this.brokerURL(z)+"/apikey",{
			"method":"PUT","body":"null"
		}));
	}.bind(this));
}

App.prototype.securityForm = function() {
	var form = TemplateJS.View.fromTemplate("security_form");
	
	function dropUser(user, text){		
		this.dlgbox({"text": text+user}, "confirm").then(function() {
			this.users = this.users.filter(function(z) {
				return z.username != user;
			});
			update.call(this);
		}.bind(this));
	}
	
	
	function update() {
		
		var rows = this.users.map(function(x) {
			var that = this;
			return {
				user:x.username,
				role:{
					"value":x.admin?"admin":"viewer",
					"!change": function() {
						x.admin = this.value == "admin"
					}
				},
				comment:x.comment,
				drop:{"!click":function() {
					dropUser.call(that, x.username, this.dataset.question);
				}}
			}
		},this)
		
		var userbrokers = Object.keys(this.traders).reduce(function(a,b){
			var t = this.traders[b];
			a[t.broker] = 1;
			return a;
		}.bind(this),{});
		var brokers = fetch_with_error(this.brokerURL("_all")).
			then(function(x) {
				return x.filter(function(binfo){
				    	return binfo.trading_enabled && !binfo.nokeys;
				    }).map(function(binfo) {
					var z = binfo.name;
					var sub = binfo.subaccount?"("+binfo.subaccount+")":"";
					return {
						img:this.brokerImgURL(z),						
						subaccount: sub,
						exchange:  {
								value:binfo.exchangeName,
								href:binfo.exchangeUrl
							},						
						keyset: {
							"!click":function(){
								this.setApiKeyDlg(z).then(update.bind(this));
							}.bind(this)
						},
						keyerase: {
							"!click":function(){
								this.eraseApiKeyDlg(z, binfo.exchangeName+" "+sub).then(update.bind(this));
							}.bind(this),
							".style.visibility":userbrokers[z]?"hidden":"visible"
						},
						
					}
				}.bind(this));
			}.bind(this));
			
		var iscookie = document.cookie.indexOf("auth=") != -1;
		var data = {
			rows:rows,
			brokers:brokers,
			add:{
				"!click":function() {
					
					this.addUser().then(function(u) {
						var itm = this.users.find(function(z) {
							return z.username == u.username;
						});
						if (itm) itm.password = u.password;
						else this.users.push(u);
						update.call(this);
					}.bind(this));					
				}.bind(this)
				
			},
			"logout":{"!click": this.logout.bind(this)},
			"remember":{"!click": iscookie?this.logout.bind(this):this.remember.bind(this),"value":iscookie},
			guest_role:{
				"!change":function() {
					this.config.guest = form.readData(["guest_role"]).guest_role == "viewer";
				}.bind(this),
				"value": !this.config.guest?"none":"viewer"
			},
			spinner: brokers.then(function() {
				return {".hidden":true}
			})
		};
		
		form.setData(data);
		
	}
	update.call(this);
	
	return form;
}

App.prototype.dlgbox = function(data, template) {
	var dlg = TemplateJS.View.fromTemplate(template);
	dlg.openModal();
	dlg.enableItem("ok",false);
	dlg.setData(data).then(function() {
		if (!data.ok || !data.ok[".disabled"]) {
		    dlg.enableItem("ok",true);
		}
	})
	var res = new Promise(function(ok, cancel) {
		dlg.setCancelAction(function() {
			dlg.close();cancel(dlg.readData());
		},"cancel");
		dlg.setDefaultAction(function() {
			dlg.close();ok(dlg.readData());
		},"ok");
	});		
	res.view = dlg;
	return res;
}


App.prototype.saveConfig = function(x) {
	return this.validate(x).then(function(config) {
		var dff = createDiff(this.org_config, config)
		if (dff === undefined) dff = null;		
		return fetch_json("../api/admin/config",{
			method: "POST",
			headers: {
				"Content-Type":"application/json",
			},
			body:JSON.stringify(dff)
		}).then(function(x) {
			this.processConfig(x);
			return x;
		}.bind(this));
	}.bind(this));
}

App.prototype.applyEffect = function(x) {
		var top = this.top_panel;
		top.showItem("saveprogress",true);
		top.showItem("saveok",false);
		function hide() {
			top.showItem("saveprogress",false);
			top.showItem("saveok",true);			
		}
		function hide_err(e) {
			top.showItem("saveprogress",false);
			return Promise.reject(e);			
		}
		x.then(hide,hide_err);
		return x;
}

App.prototype.save = function() {
	if (this.curForm) {
		this.curForm.save();
	}
	if (!this.config) {
		this.desktop.close();
		this.init();
		return;
	}
	this.config.users = this.users;
	this.config.traders = this.traders;
	this.config.ext_assets = this.ext_assets.filter(function(x){
		return x.balance;
	});
	return this.applyEffect(this.saveConfig(this.config).then(function(){
			this.updateTopMenu();
			this.stopped = {};
			this.reload_trader_header();
		}.bind(this)),function(e){
			if (e.trader) {   
				this.menu.select("trader/"+e.trader);		
				this.dlgbox({text:e.message},"validation_error");
			} else {
				fetch_error(e);	
			}		

	}.bind(this));
}

App.prototype.logout = function() {
	this.desktop.setData({menu:"",content:""});
	this.config = null;
	if (document.cookie.indexOf("auth=") != -1) {
		fetch("../api/user",{
			"method":"DELETE",
			"body":"",
		}).then(function(resp) {
			if (resp.status == 200) {
				location.reload();
			}
		});
	} else {
		fetch("../api/admin/logout").then(function(resp) {
			if (resp.status == 200) {
				location.reload();
			}
		});
	}
}

App.prototype.remember = function() {
	
	fetch_with_error("../api/user",{
		"method":"POST",
		"body":JSON.stringify({"cookie":"permanent","needauth":true})
	}).then(function(){
		location.reload();
	})	
}


App.prototype.validate = function(cfg) {
	return new Promise(function(ok, error) {
/*		var admin = cfg.users.find(function(x) {
			return x.admin;
		})
		if (!admin) return error({
				"trader":"!",
				"message":this.strtable.need_admin
			});*/	
		ok(cfg); 
	}.bind(this));
}


App.prototype.gen_backtest = function(form,anchor, template, inputs, updatefn) {
	var el = form.findElements(anchor)[0];
	var bt = TemplateJS.View.fromTemplate(template);
	var spinner_cnt=0;
	el.parentNode.insertBefore(bt.getRoot(),el.nextSibling);
	
	function showSpinner() {
		spinner_cnt++;
		bt.showItem("spinner",true);
	}
	function hideSpinner() {
		if (--spinner_cnt == 0)
			bt.showItem("spinner",false);
	}
	var obj = {
			showSpinner:showSpinner,
			hideSpinner:hideSpinner,
			bt:bt
	}
	var update_in_progress=false;
	var pending_update = false;
	function update() {
		if (update_in_progress) {
			pending_update = true;
		} else {
			update_in_progress = true;
			updatefn(obj).catch(
				function(){}).then(
				function() {
					update_in_progress = false;
					if (pending_update) {
						pending_update = false;
						update();
					}
				});
		}
	}

	var tm = new DelayUpdate(update);

	obj.update = tm.exec.bind(tm);

	inputs.forEach(function(a) {
		form.forEachElement(a, function(x){
			if (x.tagName == "BUTTON")
				x.addEventListener("click", tm.exec.bind(tm));
			else {
				x.addEventListener("input",tm.exec.bind(tm));
				x.addEventListener("change",tm.exec.bind(tm));
			}
		})
	});
	update();
}

App.prototype.init_spreadvis = function(form, id) {
	var url = "../api/admin/spread"
	form.enableItem("vis_spread",false);
	var inputs = ["spread_calc_stdev_hours","secondary_order", "spread_calc_sma_hours","spread_mult","dynmult_raise","dynmult_fall","dynmult_mode","dynmult_sliding","dynmult_cap","dynmult_mult","force_spread","spread_mode","spread_freeze"];
	this.gen_backtest(form,"spread_vis_anchor", "spread_vis",inputs,function(cntr){

		cntr.showSpinner();
		var data = form.readData(inputs);
		var mult = Math.pow(2,data.spread_mult*0.01);
		var req = {
			sma:data.spread_calc_sma_hours,
			stdev:data.spread_calc_stdev_hours,
			force_spread:data.spread_mode=="fixed"?Math.log(data.force_spread/100+1):0,
			mult:mult,
			raise:data.dynmult_raise,
			cap:data.dynmult_cap,
			fall:data.dynmult_fall,
			mode:data.dynmult_mode,
			sliding:data.dynmult_sliding,
			spread_freeze:data.spread_freeze,
			dyn_mult:data.dynmult_mult,
			order2: data.secondary_order,
			id: id
		}
		
		return fetch_with_error(url, {method:"POST", body:JSON.stringify(req)}).then(function(v) {			
			var c = v.chart.map(function(x) {
				x.achg = x.s;
				x.time = x.t;
				return x;
			})
			if (c.length == 0) return;

			var chart1 = cntr.bt.findElements('chart1')[0];
			var interval = c[c.length-1].time-c[0].time;
			var drawChart = initChart(interval,4,700000);

			drawChart(chart1,c,"p",[],"l", "h");
		}).then(cntr.hideSpinner,cntr.hideSpinner);
		
		
	}.bind(this));
}

function createCSV(chart) {
	"use strict";
	
	function makeRow(row) {
		var s = JSON.stringify(row);
		return s.substr(1,s.length-2);
	}
	
	var rows = chart.map(function(rw){
		return makeRow([
			(new Date(rw.tm)).toJSON(),
			rw.pr,
			rw.sz,
			rw.ps,
			rw.pl,
			rw.npl,
			rw.na,
			rw.np,
			rw.op,
			rw.event?rw.event:""
		]);
	})
	
	rows.unshift(makeRow([
			"time",
			"price",
			"size",
			"position",
			"profit/loss",
			"normalized profit",
			"accumulation",
			"neutral price",
			"open price",
			"event"
		]));
	return rows.join("\r\n");
}


function DelayUpdate(fn) {
	var tm;
	var p;
	var done;
	
	this.exec = function() {
		if (!p) p = new Promise(function(ok){done = ok});
		if (tm) clearTimeout(tm);
		tm = setTimeout(function() {
			var d = done;
			tm = null;
			p = null;
			d(fn());
		},250);
		return p;
	}
};

App.prototype.init_backtest = function(form, id, pair, broker) {
	var url = "../api/admin/backtest2";
	form.enableItem("show_backtest",false);		
	var inputs = ["strategy","external_assets", "acum_factor","kv_valinc","kv_halfhalf","min_size","max_size","linear_suggest","linear_suggest_maxpos",
		"dynmult_sliding","accept_loss",
		"hp_trend_factor","hp_allowshort","hp_reinvest","hp_power","hp_asym","hp_reduction","sh_curv","hp_limit","hp_extbal","hp_powadj","hp_dynred",
		"gs_external_assets","gs_rb_hi_a","gs_rb_lo_a","gs_rb_hi_p","gs_rb_lo_p",
		"min_balance","max_balance","max_leverage","reduce_on_leverage","gamma_exp","gamma_rebalance","gamma_trend","gamma_fn","gamma_reinvest","gamma_maxrebal",
		"pincome_exp",
		"hodlshort_z","hodlshort_acc","hodlshort_rinvst","hodlshort_b",
		"pile_accum","pile_ratio",
		"kv2_accum","kv2_boost","kv2_chngtm","kv2_reinvest",
		"incval_w","incval_r","incval_ms","incval_ri","incval_z",
		"hedge_short","hedge_long","hedge_drop",
		"shg_w","shg_p","shg_z","shg_b","shg_olt","shg_ol","shg_lp","shg_rnv","shg_avgsp","shg_boostmode","shg_lazyopen","shg_lazyclose","shg_offset",
		"trade_within_budget"];
	var spread_inputs = ["spread_calc_stdev_hours","secondary_order", "spread_calc_sma_hours","spread_mult","dynmult_raise","dynmult_fall","dynmult_mode","dynmult_sliding","dynmult_cap","dynmult_mult","force_spread","spread_mode","spread_freeze"];
	var leverage = form._pair.leverage != 0;	
	var pairinfo = form._pair;
	var invert_price = form._pair.invert_price;
	var days = 45*60*60*24*1000;
    var offset = 0;
    var offset_max = 0;
	var sttype =form.readData(["strategy"]).strategy;
	var infoElm;
	var balance = form._backtest_balance+(leverage?0:form._assets*invPrice(form._price,form._pair.invert_price))
	var this_bt=this.backtest[id];
	if (!this_bt) this.backtest[id]=this_bt={};
	if (!this.backtest_opts) this.backtest_opts = {};
	var btopts = this.backtest_opts[id] || {
			fill_atprice:true,
			show_op:false,
			invert_chart:false,
			reverse_chart:false,
			allow_neg_balance:false,
			spend_profit:false,
			hist_smooth:0,
			rnd_preset:{
				"volatility":1,
				"noise":1
			},			
			show_norm:["halfhalf","pile","keepvalue","exponencial","errorfn","hypersquare","conststep"].indexOf(sttype) != -1?1:0,
			initial_price:form._price,
			initial_balance:balance,
			initial_pos:undefined,			
		};
	this.backtest_opts[id] =  btopts;
	


	function draw(cntr, v, offset, balance) {

        var vlast = v[v.length-1];
		var interval = vlast.tm-v[0].tm;	
		offset_max = interval - days;
		if (offset_max < 0) offset_max = 0;
		var imin = vlast.tm - offset - days;
		var imax = vlast.tm - offset;
		var max_pos = 0;
		var min_pl = 0;
		var max_pl = 0;		
		var max_downdraw = 0;
		var max_lev = 0;
		var cost = 0;
		var max_cost = 0;
		var trades = 0;
		var buys = 0;
		var sells = 0;
		var alerts = -1;
		var ascents = 0;
		var descents = 0;
		var max_streak = 0;
		var cur_streak = 0;
		var cnt_liq = 0;
		var cnt_mgc = 0;
		var cnt_nbl = 0;
		var cnt_als = 0;
		var last_dir = 0;
		var lastp = v[0].pr;
		var init_price = invPrice(v[0].pr, invert_price);

		var c = [];
		var ilst,acp = false;
		v.forEach(function(x) {
			var nacp = x.tm >= imin && x.tm <=imax;
			var npr = invert_price?1.0/x.pr: x.pr;
			if (nacp || acp) {
				x.achg = x.sz;
				x.time = x.tm;
				x.man = x.event !== undefined;
				if (ilst) {
				    c.push(ilst);
					ilst = null;
				}
				x.plb = x.pl/npr;
				x.nplb = x.npl/npr;
				c.push(x);				
			} else {
				ilst = x;
			}				
			acp = nacp;
			if (balance!==undefined) {
				var ap = Math.abs(x.ps);
				if (ap > max_pos) 
				    max_pos = ap;
				if (x.pl > max_pl) {max_pl = x.pl;min_pl = x.pl;}
				if (x.pl < min_pl) {min_pl = x.pl;var downdraw = max_pl - min_pl; if (downdraw> max_downdraw) max_downdraw = downdraw;}
				cost = cost + x.sz * npr;
				if (cost > max_cost) max_cost = cost;
				if (x.sz > 0) buys++;
				if (x.sz < 0) sells++;
				if (x.sz == 0) alerts++; else trades++;
				var dir = Math.sign(x.pr - lastp);
				lastp = x.pr;
				if (dir != last_dir) {
					if (cur_streak > max_streak) max_streak = cur_streak;
					cur_streak = 1;
					last_dir = dir;
				} else {
					cur_streak++;
				}
				if (dir > 0) ascents++;
				if (dir < 0) descents++;			
				if (x.event) {
					switch (x.event) {
						case "liquidation": cnt_liq++;break;
						case "margin_call": cnt_mgc++;break;
						case "no_balance": cnt_nbl++;break;
						case "accept_loss": cnt_als++;break;
					}
				}
				if (x.pl>0) {
					var lv = ap * npr / (x.pl+balance);
					if (max_lev < lv) max_lev = lv;
				}
				
			}
		});
		if (cur_streak > max_streak) max_streak = cur_streak;


        
		var last = c[c.length-1]|| {};
		var lastnp;
		var lastnpl;
		var lastna;
		var lastop;
		var lastpl;
		var lastpla;
		var lastrpnl;
		if (offset) {
			var x = Object.assign({}, last);
			x.time = imax;
			c.push(x);
		}
		lastnp = last.np;
		lastnpl = last.npla;
		lastop = last.op;
		lastpl = last.pl;
		lastna = last.na;
		lastpla = last.plb;		
		lastrpnl = last.rpnl;

		skip_norm = lastnpl == 0;
		skip_accum = lastna == 0;
					   
		var year_mlt = 31536000000/interval;
		if (balance!==undefined) {
        cntr.bt.setData({
        	"pl":adjNumBuySell(vlast.pl),
        	"ply":adjNumBuySell(vlast.pl*year_mlt),
        	"npl":adjNumBuySell(vlast.npl),
        	"nply":adjNumBuySell(vlast.npl*year_mlt),
        	"npla":adjNumBuySell(vlast.na),
        	"nplya":adjNumBuySell(vlast.na*year_mlt),
        	"max_pos":adjNum(max_pos),
        	"max_cost":adjNum(max_cost),
        	"max_loss":adjNumBuySell(-max_downdraw),
        	"max_loss_pc":adjNum(max_downdraw/balance*100,0),
        	"max_profit":adjNumBuySell(max_pl),
        	"pr": adjNum(max_pl/max_downdraw),
        	"pc": adjNumBuySell(vlast.pl*year_mlt*100/balance,0),
        	"npc": adjNumBuySell(vlast.npl*year_mlt*100/balance,1),
        	"npca": adjNumBuySell(vlast.na*year_mlt*100/(balance/init_price),1),
			"pla": adjNumBuySell(lastpla),
			"plya": adjNumBuySell(lastpla*year_mlt),
			"pca": adjNumBuySell(lastpla*year_mlt*100/(balance/init_price),1),
			"rpnl": adjNumBuySell(lastrpnl),
			"rpnly": adjNumBuySell(lastrpnl*year_mlt),
			"pcrpnl": adjNumBuySell(lastrpnl*year_mlt*100/balance,1),
			"max_lev": adjNum(max_lev,2),
        	"showpl":{".hidden":btopts.show_norm!=0},
        	"showpla":{".hidden":btopts.show_norm!=3},
        	"shownorm":{".hidden":btopts.show_norm!=1},
        	"showaccum":{".hidden":btopts.show_norm!=2},
			"showrpnl":{".hidden":btopts.show_norm!=4},
        	"graphtype":btopts.show_norm,
        	"trades": trades,
        	"buys": buys,
        	"sells": sells,
        	"alerts": alerts,
        	"ascents": ascents,
        	"descents": descents,
        	"streak": max_streak,
        	"ev_liq": cnt_liq,
        	"ev_mcall": cnt_mgc,
        	"ev_nobal": cnt_nbl,
        	"ev_aclos": cnt_als,
        	"ev_liq_hid": {".hidden":!cnt_liq},
        	"ev_mcall_hid": {".hidden":!cnt_mgc},
        	"ev_nobal_hid": {".hidden":!cnt_nbl},
        	"ev_aclos_hid": {".hidden":!cnt_als}
        });
		}

		var chart1 = cntr.bt.findElements('chart1')[0];
		var chart2 = cntr.bt.findElements('chart2')[0];
		if (interval > days) interval = days;
		var ratio = 4;		
		var scale = 1000000;
		var drawChart = initChart(interval,ratio,scale,true);
		var drawMap1;
		var drawMap2;
		if (btopts.show_norm==4) {
		    drawMap1=drawChart(chart1,c,"pr",lastop?[{label:"open",pr:lastop}]:[],"op");
		} else {
		    drawMap1=drawChart(chart1,c,"pr",lastnp?[{label:"neutral",pr:lastnp}]:[],"np");			
		}
		switch (btopts.show_norm) {
			case 1: drawMap2=drawChart(chart2,c,"npl",[{label:"PL",npla:lastpl}],"pl");break;
			case 2: drawMap2=drawChart(chart2,c,"na",[]);break;
			case 3: drawMap2=drawChart(chart2,c,"plb",[],"nplb");break;
			case 4:  drawMap2=drawChart(chart2,c,"rpnl",[],"pl");break;
			default: drawMap2=drawChart(chart2,c,"pl",[{label:"norm",pl:lastnpl}],"npla");break;
		}
		var tm;
		if (infoElm)  {
		    infoElm.close();
		    infoElm = null;
		}
		cntr.bt.showItem("icon_test",!!(this_bt.minute && this_bt.minute.id));

        show_info_fn = function(ev,clear) {
        	if (tm) clearTimeout(tm);
        	tm = setTimeout(function() {
				tm = undefined;
        		var selmap;
        		if (cntr.bt.findElements("options")[0].classList.contains("sel") || clear) {
        			if (infoElm) infoElm.close();
        			infoElm = null;
                    return;        			
        		} else if (drawMap1.msin(ev.clientX, ev.clientY)) selmap = drawMap1;
        		else if (drawMap2.msin(ev.clientX, ev.clientY)) selmap = drawMap2;
        		else {
        			if (infoElm) infoElm.close();
        			infoElm = null;
                    return;        			
        		}
                var bb = selmap.box();
                var ofsx = ev.clientX - bb.left;
                var ofsy = ev.clientY - bb.top;
                var cont = chart1.parentNode.parentNode.parentNode.parentNode;
                var contBox = cont.getBoundingClientRect();
                if (!infoElm) {
                	infoElm = TemplateJS.View.fromTemplate("backtest_info");
					infoElm.getRoot().style.position="absolute";
					cont.appendChild(infoElm.getRoot());
                }
				cont.style.position="relative";                
                var infoElmElm = infoElm.getRoot();
                var bestItem ;
                var bestTime = Date.now();
                var symb = selmap.symb();
                c.forEach(function(v){
                	var val = v[symb];
                	if (val !== undefined) {
						var ptx = selmap.point_x(v.time);
						var pty = selmap.point_y(val);
						var d = Math.sqrt(Math.pow(ptx - ofsx,2)+Math.pow(pty - ofsy,2));
						if (d < bestTime) {
							bestTime = d;
							bestItem = v;
						};
                	}
                });
                if (bestItem) {
                	infoElm.setData({
                		pr:adjNum(bestItem.pr),
                		ps:adjNumBuySell(bestItem.ps),
                		pl:adjNumBuySell(bestItem.pl),
                		na:adjNumBuySell(bestItem.na),
                		npl:adjNumBuySell(bestItem.npl),
                		sz:adjNumBuySell(bestItem.sz),
                		bt_event: bestItem.event,
                		bt_event_row: {".hidden": bestItem.event === undefined},
                		bal:adjNumBuySell(bestItem.bal),
						ubal:adjNumBuySell(bestItem.ubal),
						ubal_row:{".hidden":bestItem.bal==bestItem.ubal},
						accum_row:{".hidden":bestItem.na==0},
                		info:Object.keys(bestItem.info)
                		    .map(function(n) {
                		    	var v = bestItem.info[n];
                		    	return {
                		    		key:n,
                		    		value:adjNum(v),
                		    	}
                		    })
                	});
					var val = bestItem[symb];
					if (val !== undefined) {
						var ptx = selmap.point_x(bestItem.time);
						var pty = selmap.point_y(val);
						ptx = ptx + bb.left - contBox.left;
						pty = pty + bb.top - contBox.top;
						infoElmElm.style.right = (contBox.width - ptx+7)+"px";
						infoElmElm.style.bottom = (contBox.height - pty+7)+"px";
					}
                }
        	},600);
        }

	}

    function get_bt_file(what) {
    	if (what && what.id) {
    	    fetch_json(url+"/get_file",{method:"POST",body:JSON.stringify({source:what.id})}).then(
				function(x){what.chart = x;});
    	}
    }

	var frst = true;

	var update;
	var update_recalc = function() {}
	var tm;
	var config;
	var opts
	var bal;
	var req;
	var res_data;	
	var delayed_update_trades = new DelayUpdate(function(){get_bt_file(this_bt.trades)});
	var delayed_update_minute = new DelayUpdate(function(){get_bt_file(this_bt.minute)});
    var skip_accum = false;
    var skip_norm = false;

	function swapshowpl() {
		btopts.show_norm = (btopts.show_norm+1)%3;
		if (skip_norm && btopts.show_norm == 1) swapshowpl();
		else if (skip_accum && btopts.show_norm == 2) swapshowpl();
		update_recalc();
	}

	
	var import_error = function() {
		this.dlgbox({text:this.strtable.import_invalid_format,cancel:{".hidden":true}},"confirm");		
	}.bind(this);
	
    var show_info_fn = function(ev) {
    }    

	function gen_spread(offset) {
		var data = form.readData(spread_inputs);
		var mult = Math.pow(2,data.spread_mult*0.01);
		offset = offset || 0;
		var sreq = {
			sma:data.spread_calc_sma_hours,
			stdev:data.spread_calc_stdev_hours,
			force_spread:data.spread_mode=="fixed"?Math.log(data.force_spread/100+1):0,
			mult:mult,
			raise:data.dynmult_raise,
			cap:data.dynmult_cap,
			fall:data.dynmult_fall,
			mode:data.dynmult_mode,
			sliding:data.dynmult_sliding,
			spread_freeze:data.spread_freeze,
			dyn_mult:data.dynmult_mult,
			reverse: btopts.reverse_chart,
			invert: btopts.invert_chart,
			ifutures: invert_price,			
			source: this_bt.minute.id,
			order2: data.secondary_order,
			offset: offset,
			swap:  form._swap_symbols == 1
		}
		return fetch_json(url+"/gen_trades",{method:"POST",body:JSON.stringify(sreq)});
	}
 	

	this.gen_backtest(form,"backtest_anchor", "backtest_vis",inputs,function(cntr){

		function download_historical_dlg() {
					this.waitScreen(Promise.all([
						fetch_json("../api/admin/btdata"), 
						fetch_json("../api/admin/brokers/"+encodeURIComponent(broker)+"/pairs/"+encodeURIComponent(pair)),
						fetch("../api/admin/brokers/"+encodeURIComponent(broker)+"/pairs/"+encodeURIComponent(pair)+"/history")
					])).then(function(resp) {
						function dlgRules() {
							var en = !d.view.readData(["from_broker"]).from_broker;
							var sv = !d.view.readData(["save"]).from_broker;
							d.view.enableItem("asset",en);
							d.view.enableItem("currency",en);
							d.view.enableItem("smooth",en);							
							d.view.enableItem("save",!en);							
						};
						var brkhist = resp[2].status==200;
						var ddata = {
							    from_broker:{
							    	value:brkhist,
							    	"disabled":brkhist?null:"",
							    	"!change": dlgRules
							    },
							    save:{
							    	"disabled":!brkhist?null:"",
									"value":false
							    },
								symbols: resp[0].map(function(x){
									return {"":x};
								}),
								asset:resp[1].quote_asset,
								currency:resp[1].quote_currency,
								smooth:btopts.hist_smooth,
						};
						var d;
						(d = this.dlgbox(ddata,"download_price_dlg")).then(function(){
							ddata = d.view.readData(["asset","currency","smooth","from_broker","save"]);;
							if (ddata.from_broker) {
								var dataid;
								showProgress(fetch_json("../api/admin/brokers/"+encodeURIComponent(broker)+"/pairs/"+encodeURIComponent(pair)+"/history",{method:"POST"}).then(function(info){
                                	dataid = info.data;
                                	return info.progress;
								})).then(function(dlg){
                                		this_bt.minute={id:dataid};
										this_bt.trades = null;
										get_chart(cntr.update().then(function(){
											dlg.close();
											if (ddata.save) {
											    var a  = document.createElement("a");
											    a.setAttribute("href",url+"/"+dataid);
											    a.setAttribute("download","minute_"+broker+"_"+pair+".csv");
											    document.body.appendChild(a);
											    a.click();
											    document.body.removeChild(a);
											}
										}), delayed_update_minute);
                                });
							} else { 
								this_bt.minute = {"mode":"historical_chart","args":ddata};
								this_bt.trades = null;
								btopts.hist_smooth = ddata.smooth;
								cntr.update();
							}
						}.bind(this));					
						dlgRules();
					}.bind(this));
		}


		cntr.showSpinner();
		config = this.saveForm(form,{});
        opts = frst?btopts:cntr.bt.readData(["initial_balance", "initial_pos","initial_price"]);
		Object.assign(btopts, opts);
		config.broker = broker;
		config.pair_symbol = pair;
		var init_price = isFinite(opts.initial_price)?opts.initial_price:form._price;
        var norm_init_price = invert_price?1.0/init_price:init_price;
        bal =( isFinite(opts.initial_balance)?opts.initial_balance:balance)
                    + (leverage?0:(isFinite(opts.initial_pos)?opts.initial_pos:0)*norm_init_price);

        function has_minute() {return this_bt.minute && this_bt.minute.chart;};
       	req = {
			config: config,
			minfo: pairinfo,
			init_pos:isFinite(opts.initial_pos)?opts.initial_pos:undefined,
			init_price:init_price,
			balance:isFinite(opts.initial_balance)?opts.initial_balance:bal,
			fill_atprice:btopts.fill_atprice,
			start_date:btopts.start_date,
			reverse: btopts.reverse_chart,
			invert: btopts.invert_chart,
			neg_bal: btopts.allow_neg_balance,
			spend: btopts.spend_profit,
		};


		if (frst) {
			frst = false;

			var d = new DelayUpdate(function() {
                this_bt.trades=null;
                cntr.update();
			});
			spread_inputs.forEach(function(a) {
				form.forEachElement(a, function(x){
					if (x.tagName == "BUTTON")
						x.addEventListener("click", d.exec);
					else {
						x.addEventListener("input",d.exec);
						x.addEventListener("change",d.exec);
					}
				})
			});
			
			cntr.bt.setItemValue("initial_price",adjNumN(btopts.initial_price));
			cntr.bt.setItemValue("initial_balance",adjNumN(btopts.initial_balance));
			cntr.bt.setItemValue("initial_pos",adjNumN(btopts.initial_pos));
			cntr.bt.setData({"start_date":{".valueAsNumber":btopts.start_date}});
			cntr.bt.setItemEvent("options", "click", function() {
				this.classList.toggle("sel");
				cntr.bt.setData({"options_form":{classList:{shown:this.classList.contains("sel")}}});
				if (infoElm) {infoElm.close(); infoElm = null;}
			});
			cntr.bt.setItemEvent("initial_balance","input",cntr.update);
			cntr.bt.setItemEvent("initial_pos","input",cntr.update);
			cntr.bt.setItemEvent("initial_price","input",cntr.update);
			cntr.bt.setItemEvent("reverse_chart","change",function() {
				btopts.reverse_chart = cntr.bt.readData(["reverse_chart"]).reverse_chart;
				if (has_minute()) this_bt.trades = null;
				cntr.update();				
			});
			cntr.bt.setItemEvent("invert_chart","change",function() {
				btopts.invert_chart = cntr.bt.readData(["invert_chart"]).invert_chart;
				if (has_minute()) this_bt.trades = null;
				cntr.update();				
			});
			cntr.bt.setItemEvent("graphtype", "change", function(){
				btopts.show_norm =  parseInt(this.value);
				update_recalc();
			});
			cntr.bt.setItemEvent("icon_test", "click", stabilityTest.bind(this));
			cntr.bt.setItemValue("show_op", btopts.show_op);
			cntr.bt.setItemValue("fill_atprice",btopts.fill_atprice);
			cntr.bt.setItemValue("allow_neg_bal",btopts.allow_neg_balance);
			cntr.bt.setItemValue("spend_profit",btopts.spend_profit);
			cntr.bt.setItemValue("reverse_chart",btopts.reverse_chart);
			cntr.bt.setItemValue("invert_chart",btopts.invert_chart);
			cntr.bt.setItemEvent("allow_neg_bal","change", function() {
				btopts.allow_neg_balance = cntr.bt.readData(["allow_neg_bal"]).allow_neg_bal;
				cntr.update();
			})
			cntr.bt.setItemEvent("spend_profit","change", function() {
				btopts.spend_profit = cntr.bt.readData(["spend_profit"]).spend_profit;
				cntr.update();
			})
			cntr.bt.setItemEvent("show_op","change", function() {
				btopts.show_op = cntr.bt.readData(["show_op"]).show_op;
				update();
			})
			cntr.bt.setItemEvent("start_date","input", function() {
				btopts.start_date=this.valueAsNumber;
				if (btopts.start_date>0 || isNaN(btopts.start_date)) cntr.update();
			})
			cntr.bt.setItemEvent("fill_atprice","change", function() {
				btopts.fill_atprice = cntr.bt.readData(["fill_atprice"]).fill_atprice;
				cntr.update();
			})
			cntr.bt.setItemEvent("showpl","click",swapshowpl);
			cntr.bt.setItemEvent("showpla","click",swapshowpl);
			cntr.bt.setItemEvent("showrpnl","click",swapshowpl);
			cntr.bt.setItemEvent("shownorm","click",swapshowpl);
			cntr.bt.setItemEvent("showaccum","click",swapshowpl);
			cntr.bt.setItemEvent("select_file","click",function(){
				var el = cntr.bt.findElements("price_file")[0];
				el.value="";
				el.click();
			});
			cntr.bt.setItemEvent("random_chart","click",function(){
				if (btopts.rnd_preset.seed === undefined) {
				    btopts.rnd_preset.seed = (Math.random()*32768*65536)|0;
				}
				var d;
				(d = this.dlgbox(btopts.rnd_preset,"random_dlg")).then(function(){
					btopts.rnd_preset = d.view.readData();
					this_bt.trades = null;
					this_bt.minute = {mode:"random_chart", args: btopts.rnd_preset};
					cntr.update();
				});

			}.bind(this));
			cntr.bt.setItemEvent("download_prices","click",download_historical_dlg.bind(this));

			cntr.bt.setItemEvent("chart1","mousemove", function(ev){show_info_fn(ev);});
			cntr.bt.setItemEvent("chart2","mousemove", function(ev){show_info_fn(ev);});
			cntr.bt.setItemEvent("","mouseout",function(ev){show_info_fn(ev,true);});
			cntr.bt.setItemEvent("export","click",function() {
				doDownlaodFile(createCSV(res_data),id+".csv","text/plain");
			})			
			cntr.bt.setItemEvent("price_file","change",function() {
				if (this.files[0]) {
					var reader = new FileReader();
					var mode = this.dataset.mode;
					reader.onload = function() {
						var min=0;
						var trs=0;
						var prices = reader.result.split("\n").map(function(x) {
							var jr = "["+x+"]";
							try {
								var rowdata = JSON.parse(jr);
								if (rowdata.length == 1) {
									min++;										
									var out = typeof rowdata[0] == "number"?rowdata[0]:parseFloat(rowdata[0]);
									return isFinite(out)?out:null;
								} else if (rowdata.length > 1) {
									trs++;
									var out = [(new Date(rowdata[0]))*1, (typeof rowdata[1] == "string"?parseFloat(rowdata[1]):rowdata[1])];
									return isFinite(out[1])?out:null;
								} else {
									return null;
								}
							} catch(e) {
								return null;
							}
						}).filter(function(x) {return x !== null});
						if (min > trs && trs/min < 0.1) {
							this_bt.minute={"chart":prices};
							this_bt.trades=null;
							cntr.update();
						} else if (min < trs && min/trs < 0.1) {
							this_bt.trades={"chart":prices};
							cntr.update();
						} else {
							import_error();
						}
					}.bind(this);
					reader.readAsText(this.files[0]);
				}
			});
			cntr.bt.setItemEvent("icon_internal","click",function(){
				this_bt.minute={"mode":"trader_minute_chart","args":{"trader":id}};
				this_bt.trades=null;
				cntr.update();
			})
			cntr.bt.setItemEvent("icon_recalc","click",function(){
				this_bt.trades = null;
				cntr.update();
			})
			cntr.bt.setItemEvent("icon_reset","click",function(){
				this_bt.minute = null;
				this_bt.trades = null;
				cntr.update();
			})
			

			cntr.bt.setItemEvent("pos","input",function() {
				var v = -this.value;
				offset = v * offset_max/100;
				if (update) update();
			})

		}

		function get_chart(p, du) {
			p.then(function(){
				du.exec();
			});
			return p;
		}
		
        var download_historical_fn = download_historical_dlg.bind(this);

		
		function call_update() {
			
			var ret;
			
			if (this_bt.trades && this_bt.trades.id) {
				req.source = this_bt.trades.id; 			
				if (has_minute()) {
					req.reverse = false;
					req.invert = false;
				}
				ret =  fetch_json(url+"/run", {method:"POST", body:JSON.stringify(req)}).then(function(v) {
					cntr.bt.setData({error_window:{classList:{mark:false}}});					    
					if (v.length == 0) {
						var chart1 = cntr.bt.findElements('chart1')[0];
						var templ = TemplateJS.View.fromTemplate("no_data_panel");
						templ.setData({"download":{
							"!click":download_historical_fn
						    }});
						TemplateJS.View.clearContent(chart1);
						chart1.appendChild(templ.getRoot());
						update_recalc = function() {};
					} else {
						res_data = v;
						draw(cntr,v,offset,bal);			
						update = function() {
							if (tm) clearTimeout(tm);
							tm = setTimeout(draw.bind(this,cntr,v,offset), 1);
						}
						update_recalc = draw.bind(this,cntr,v,offset,bal);
					}
				},function(e){
							if (e.status == 410) {
								delete this_bt.trades.id;
								return call_update();
							} else {
								throw e;
							}							
						});
			} else if (this_bt.trades && this_bt.trades.chart) {
				ret = fetch_json(url+"/upload", {method:"POST", body:JSON.stringify(this_bt.trades.chart)}).then(
				function(r) {
					this_bt.trades.id = r.id;
					return call_update();
				});				
			} else if (this_bt.trades && this_bt.trades.mode) {
				ret = fetch_json(url+"/"+this_bt.trades.mode, {method:"POST", body:JSON.stringify(this_bt.trades.args)}).then(
				function(r) {
					this_bt.trades.id = r;
					return get_chart(call_update(),delayed_update_trades);
				});
			} else {
				if (this_bt.minute && this_bt.minute.id) {
					ret = gen_spread().then(function(r) {
						this_bt.trades = r;
						return get_chart(call_update(),delayed_update_trades);
					},function(e){
						if (e.status == 410) {
							delete this_bt.minute.id;
							return call_update();
						} else {
							throw e;
						}							
					});
				} else if (this_bt.minute && this_bt.minute.chart) {
					ret = fetch_json(url+"/upload", {method:"POST", body:JSON.stringify(this_bt.minute.chart)}).then(
					function(r) {
						this_bt.minute.id = r.id;
						return call_update();
					})
				} else if (this_bt.minute && this_bt.minute.mode) {
					ret = fetch_json(url+"/"+this_bt.minute.mode, {method:"POST", body:JSON.stringify(this_bt.minute.args)}).then(
					function(r) {
						this_bt.minute = r;
						return get_chart(call_update(),delayed_update_minute);
					})
				} else {
					ret = fetch_json(url+"/trader_chart",{method:"POST",body:JSON.stringify({"trader":id})}).then(
					function(r) {
						this_bt.trades = r;
						return get_chart(call_update(),delayed_update_trades);
					},function(e){
						if (e.status == 404) {
							this_bt.trades = {chart:[]};
							call_update();
						}
					})
				}									
			}
			return ret;			
		}

		return call_update().then(function(v) {
			cntr.hideSpinner();
		},function(e){
			parse_fetch_error(e).then(function(x) {
				cntr.bt.setData({error_window:{
					classList:{
						mark:true
					},value:x.msg+" " +x.t
					}});
				cntr.hideSpinner();throw e;
			});
		});
		

        
		
	}.bind(this));

	function stabilityTest() {
		var stop=false;
		var cnt = 0;
		var dlg = TemplateJS.View.fromTemplate("stability_test");
		dlg.openModal();
		dlg.setCancelAction(function() {
			dlg.close();			
			stop = true;
		},"close");
		function runTest() {
            if (!stop) {
            	gen_spread(cnt).then(
                    function(r) {
                    	req.source = r.id;
                        fetch_json(url+"/probe", {method:"POST", body:JSON.stringify(req)}).then(function(v) {
                            updateResults(v);
                            cnt++;
                            dlg.unmark();
                            runTest();
                        },function(e){
                        	reportError(e).then(function(){
                        	    runTest();
                        	});
                        })
                    },function(e) {
                    	reportError(e).then(function(){
                    	    runTest();
                    	})
                    })
            }
    
		}

		runTest.call(this);

        var stats = {
        	profit:[0,0,0,0],
        	norm_profit:[0,0,0,0],
        	alerts:[0,0,0,0],
        	margin:[0,0,0,0],
        	liquid:[0,0,0,0]        
        }

        function reportError(e) {
        	return parse_fetch_error(e).then(function(x) {
        		dlg.setItemValue("error",x.t + " " +x.msg);
        		dlg.mark("error");
        		return TemplateJS.delay(5000);
        	});
        }

        function updateStat(st, val) {
			st[0] = val;
			st[3] = st[3]+val;
        	if (cnt) {
				st[1] = st[1]<val?st[1]:val;
				st[2] = st[2]>val?st[2]:val;
        	} else {
    			st[1] = st[2] = val;

        	}
        }

        function fillData(st, prefix, data) {
        	data[prefix+"_last"] = adjNumBuySell(st[0],2);
        	data[prefix+"_min"] = adjNumBuySell(st[1],2);
        	data[prefix+"_max"] = adjNumBuySell(st[2],2);
        	data[prefix+"_avg"] = adjNumBuySell(st[3]/(cnt+1),2);
        }

		function updateResults(v) {
            updateStat(stats.profit, v.pc_pl);
            updateStat(stats.norm_profit, v.pc_npl);
            updateStat(stats.alerts, v.events.alerts);
            updateStat(stats.margin, v.events.margin_call);
            updateStat(stats.liquid, v.events.liquidation);
            var data = {};
            fillData(stats.profit,"profit", data);
            fillData(stats.norm_profit,"norm_profit", data);
            fillData(stats.alerts,"alerts", data);
            fillData(stats.margin,"margin", data);
            fillData(stats.liquid,"liquid", data);
            data["tests"] = cnt+1;
            dlg.setData(data);
		}

	};
}

App.prototype.optionsForm = function() {
	var form = TemplateJS.View.fromTemplate("options_form");
	var data = {
			report_interval: defval(this.config.report_interval,864000000)/86400000,
			stop:{"!click": function() {
					this.waitScreen(fetch_with_error("../api/admin/stop",{"method":"POST"}));
					for (var x in this.traders) this.stopped[x] = true;
			}.bind(this)},
			reload_brokers:{"!click":function() {
				this.waitScreen(fetch_with_error("../api/admin/brokers/_reload",{"method":"POST"}));
			}.bind(this)}
			
	};
	var utm = Date.now();
	function updateUtilz() {
		fetch_json("../api/admin/utilization?tm="+utm).then(function(data) {
			var f = {
				p:{".style.width":(data.reset/600).toFixed(1)+"%"},
				v:(data.reset/600).toFixed(1)
			}
			var total = data.reset;
			var items = [];
			for (var id in data.traders) {
				var t = data.traders[id];
				var broker = this.traders[id].broker;
				total += t;
				items.push({		
					"":{classList:{"updated":data.updated.indexOf(id) != -1}},		
					icon:this.brokerImgURL(broker),
					name:this.traders[id].title || id,
					p:{".style.width":(t/600).toFixed(1)+"%"},
					v:(t/600).toFixed(1)
				});
			}
			f.items = items;
			f.total_p = {".style.width":(total/600).toFixed(1)+"%"};
			f.total_v = (total/600).toFixed(1);
			form.setData(f);
			utm = data.last_update;
		}.bind(this));
	}
	
	updateUtilz.call(this);
	var tm = setInterval(updateUtilz.bind(this),1000);
	
	form.setData(data);
	form.save = function() {
		clearInterval(tm);
		var data = form.readData();		
		this.config.report_interval = data.report_interval*86400000;
	}.bind(this);
	return form;
	
}

App.prototype.messagesForm = function() {
	var form = TemplateJS.View.fromTemplate("news_messages");
	var me = this;
	form.save = function(){};
	function update(src) {
		var data = {};
		var items = src.items || [];
		var newest = items.reduce(function(a,b){
			return b.time>a?b.time:a;
		},0);
		data.mark = {
			".disabled": !items.find(function(x){return x.unread;}),
			"!click": function() {
				fetch("../api/admin/news",{
					"method":"POST",
					"body":JSON.stringify(newest+1)
				}).then(function(e){
					if (e.status != 202) {
						fetch_error(e);
					} else {
						form.setData({"items":[]});
						me.news_content = fetch_with_error("../api/admin/news")
						me.news_content.then(update);
					}
				});
			}
		};
		data.goto = {
			"value":src.title,
			"!click":function() {
				window.open(src.url)
			}
		};
		data.reload = {
			"!click": function() {
				form.setData({"items":[]});
				me.news_content =fetch_with_error("../api/admin/news");
				me.news_content.then(update);
			}
		};
		data.items = items.sort(function(a,b){
			return b.time-a.time;
		}).map(function(x){
			return {
				"":{
					classList:{
						unread: x.unread,
						hl: x.hl
					}
				},
				"rowx":{								
					"!click": function() {
						this.parentElement.classList.toggle("open");					
					}
				},
				"date":(new Date(x.time)).toLocaleDateString(),
				"topic":x.title,
				"body":x.body,
			};
		});
		form.setData(data);						
		var elem = form.getRoot().getElementsByClassName("body");
		Array.prototype.forEach.call(elem,function(e) {
			e.innerHTML = e.innerHTML.replace(/(https:\/\/[^\s]+)/g, "<a href='$1' target='_blank'>$1</a>");
		});
		
	}
	this.news_content.then(update)
	return form;
}

App.prototype.walletForm = function() {
	var form = TemplateJS.View.fromTemplate("wallet_form");
	var wallets = [];	
	var rfr = -1;
	var ext_assets = this.ext_assets;
	var incntr = document.createElement("input");	
	incntr.setAttribute("type","number");
	incntr.setAttribute("step","none");
	incntr.setAttribute("class","editval");
	var incntr_rec={};
	incntr.addEventListener("blur", function(){
		if (!incntr_rec.canceled) {			
			var flt = function(x) {return x.broker == incntr_rec.broker && x.wallet == incntr_rec.wallet && x.symbol == incntr_rec.symbol};
			var val = incntr.valueAsNumber;
			if (isNaN(val) || incntr_rec.balance == val) {
				this.ext_assets = ext_assets = ext_assets.filter(function(z){return !flt(z);});
			} else {
				var diff = val - incntr_rec.balance;
				var ea = ext_assets.find(flt);
				if (ea) ea.balance = diff;
				else {
					var v = Object.assign({},incntr_rec);
					v.balance = diff;
					ext_assets.push(v);
				}
			}
		}
		update();
		incntr.parentNode.removeChild(incntr);
	}.bind(this));
	incntr.addEventListener("keydown",function(ev){
		if (ev.code == "Enter" || ev.code == "Escape") {
			ev.preventDefault();
			ev.stopPropagation();
			if (ev.code == "Escape") incntr_rec.canceled = true;
			incntr.blur();
		}
	}.bind(this));
	function format(x) {
	    var r = x.toFixed(6);
	    return r;
	}
	function update() {			
		var data = fetch_json("../api/admin/wallet").then(function(data) {
			var allocs = data.wallet.map(function(x) {
				var bal = x.balance;
				var out = Object.assign({},x);
				var balstr = format(bal);
				var baltot;
				var ea = ext_assets.find(function(y){
					return y.broker == x.broker && y.wallet == x.wallet && y.symbol == x.symbol; 
				});
				out.allocated = x.allocated.toFixed(6);
				var mod;
				if (ea && ea.balance) {
					baltot = x.balance+ea.balance;
					mod = true;
			    } else {
			    	mod = false;
			    	baltot = x.balance;
					out.balance = {
						value: format(baltot)
					}
				}						
				out.balance = {value: format(baltot),
							  title: balstr,
							  classList:{modified:mod}
							};
				out.editval={"!click": function() {
					incntr_rec={
						broker:x.broker,
						wallet:x.wallet,
						symbol:x.symbol,
						balance:x.balance,						
					}
					this.parentNode.appendChild(incntr);
					incntr.value = baltot;
					incntr.focus();
					incntr.select();
				}};
				out.img="../api/admin/brokers/"+encodeURIComponent(x.broker)+"/icon.png";
				return out;
			}.bind(this));
			var brokers = data.entries;
			var form_data = {allocs:allocs};
			var wt = brokers.map(function(brk) {	
				var idx = wallets.findIndex(function(a) {
					return a["@id"] == brk;
				});
				if (idx == -1) {
					wallets.push({"@id":brk});
				}							
				return fetch_json("../api/admin/wallet/"+encodeURIComponent(brk)).then(
				function(wdata) {
					var wlts = [];
					for (var x in wdata) {
						var assts = [];
						for (var y in wdata[x]) {
							assts.push({
								symbol:y,
								value: format(parseFloat(wdata[x][y])),
							});
						}
						wlts.push({
							wallet_name: x,
							balances: assts
						});
					}
					return {			
						broker_icon:"../api/admin/brokers/"+encodeURIComponent(brk)+"/icon.png",
						broker_name:brk,
						account_wallets: wlts,
						spinner:{".hidden":true}
					}
				}, function(err) {						
					return null;
				}).then(function(n) {
					var idx = wallets.findIndex(function(a) {
						return a["@id"] == brk;
					});
					if (idx!=-1) {
						if (n != null) {
						    Object.assign(wallets[idx],n);	
						} else {
							wallets.splice(idx,1);
						}						
					}
					form.setData({wallets:wallets});					
				});
			});
			wallets.sort(function(a,b) {
				return a["@id"]<b["@id"]?-1:a["@id"]<b["@id"]?0:1;
			});
			form_data.spinner = {".hidden":true};
			form.setData(form_data);
		}.bind(this));
	}
	rfr = setInterval(update, 60000);
	update();	
	form.save = function() {
		clearInterval(rfr);
	}.bind(this);
	return form;
}

App.prototype.tradingForm = function(id) {
	if (this.curForm && this.curForm.save) {
		this.curForm.save();
	}
	this.curForm = null;
	var _this = this;
	var cfg = this.traders[id];
	if (!cfg) return;
	
	
	var form = TemplateJS.View.fromTemplate("trading_form");
	this.desktop.setItemValue("content", form);
	
	function dialogRules() {
		var data = form.readData(["order_size","order_price","edit_order"]);				
		var p = !!data.order_price;
		var q = !!data.order_size;
		form.showItem("button_buy",p);
		form.showItem("button_sell",p);
		form.showItem("button_buybid",!p);
		form.showItem("button_sellask",!p);
		form.enableItem("button_buy",q);
		form.enableItem("button_sell",q);
		form.enableItem("button_buybid",q);
		form.enableItem("button_sellask",q)
	} 
	
	form.setItemEvent("order_price","input",function(){
	    form.setData({"order_size":{"placeholder":""}});
		dialogRules();
	});
	form.setItemEvent("order_size","input",dialogRules);
	form.setItemEvent("edit_order","change",dialogRules);
	form.setItemEvent("bal_currency","click",function(){
		var n = parseFloat(this.value);
		if (n && !isNaN(n)) {
			var dt = form.readData(["last_price","order_price"]);
			var k = parseFloat(dt.last_price);
			var l = parseFloat(dt.order_price);
			if (l) l = parseFloat(l);
			if (l && !isNaN(l)) {
				k = l;
			} else {
				k = parseFloat(k);
			}
			if (k && !isNaN(k)) {
				form.setData({
					"order_price":k,
					"order_size":(n/k*0.95).toFixed(8)
				});
				dialogRules();
			}
		}
	});
	form.setItemEvent("bal_assets","click", function(){
		var n = parseFloat(this.value);
		if (n && !isNaN(n)) {
			form.setData({"order_size":Math.abs(n)});
			dialogRules();
		}
	});
	["ask_price","last_price","bid_price"].forEach(function(x){
		form.setItemEvent(x,"click", function(){
			var n = parseFloat(this.value);
			if (n && !isNaN(n)) {
				form.setData({"order_price":n});
			}
			dialogRules();
		});
	});

	dialogRules();
		
	var running_update = false;
	function update() {		
		var traderURL = _this.traderURL(id);
		var params="";
		var data = form.readData(["order_price"]);		
		var req_intrprice =data.order_price;
		if (!isNaN(req_intrprice)) params="/"+req_intrprice;
		if (running_update) return;
		running_update = true;
		var f = fetch_json(traderURL+"/trading"+params).then(function(rs) {
				var pair = rs.pair;
				var chartData = rs.chart;
				var trades = rs.trades;
				var orders = rs.orders;
				var ticker = rs.ticker;
				var strategy = rs.strategy || {};
				var invp = function(x) {return pair.invert_price?1/x:x;}
				var invs = function(x) {return pair.invert_price?-x:x;}				
				var now = Date.now();
				var skip = true;
				chartData.push(ticker);
				trades.unshift({
					time: chartData[0].time-100000,
					price: chartData[0].last
				});
				
				var drawChart = initChart(chartData[chartData.length-1].time - chartData[0].time);
				var data = mergeArrays(chartData, trades, function(a,b) {
					return a.time - b.time
				},function(n, k, f, t) {
					var p1;
					var p2;
					var achg;
					if (n == 0) {
						if (f != null && t != null) {
							p2 = invp(interpolate(f.time, t.time, k.time, f.price, t.price));
							if (f.time == k.time) achg = invs(t.size);
						} else if (f != null) {
							p2 = invp(f.price);
						}
						p1 = invp(k.last);
						skip = false;
						
					} else {
						if (skip) return null;						
						p1 = p2 = invp(k.price);
						achg = invs(k.size);
					} 
					return {
						time: k.time,
						p1:p1,
						p2:p2,
						achg: achg
					}
				}).filter(function(x) {return x != null;});
				
				var lines = [];
				orders.forEach(function(x) {
					var sz = invs(x.size);
					var l = sz < 0?_this.strtable.sell:_this.strtable.buy
					lines.push({
						p1: invp(x.price),
						label: l+" "+Math.abs(sz)+" @",
						class: sz < 0?"sell":"buy"
					});
				});
				form.findElements("chart").forEach(function(elem) {
					drawChart(elem, data, "p1",lines,"p2" );
				})			
				var formdata = {};
				if (pair.invert_price) {
					formdata.ask_price = adjNumN(1/ticker.bid);
					formdata.bid_price = adjNumN(1/ticker.ask);					
				} else {
					formdata.ask_price = adjNumN(ticker.ask);
					formdata.bid_price = adjNumN(ticker.bid);										
				}
				formdata.bal_assets = adjNumN(invs(pair.asset_balance)) + " "+pair.asset_symbol;
				formdata.bal_currency = adjNumN(pair.currency_balance) + " "+pair.currency_symbol;
				formdata.last_price = adjNumN(invp(ticker.last));
				formdata.order_price = {"placeholder":adjNumN(invp(ticker.last))};
				var data2 = form.readData(["order_price"]);
				if (strategy.size && data2.order_price == req_intrprice || (isNaN(data2.order_price) && isNaN(req_intrprice))) { 
					formdata.order_size={"placeholder":(strategy.size<0?"SELL":"BUY") +" " + adjNumN(Math.abs(strategy.size))};
				}
				var orderMap  = orders.map(function(x) {
					return {"":{
						".value":JSON.stringify(x.id),
						"value":(invs(x.size)<0?_this.strtable.sell:_this.strtable.buy) 
								+ " " + adjNumN(Math.abs(x.size))
								+" @ " + adjNumN(invp(x.price))
					}}
				});
				formdata.orders = orders.map(function(x) {
					return {id:x.id,
						dir:invs(x.size)<0?_this.strtable.sell:_this.strtable.buy,
						size:adjNumN(Math.abs(x.size)),
						price:adjNumN(invp(x.price)),
						cancel:{
							"!click":function(ev) {
								ev.stopPropagation();
								this.hidden = true;
								this.nextSibling.hidden = false;
								asyncUpdate(_this.cancelOrder(cfg.id, cfg.pair_symbol, x.id));
							},
							".hidden":false
						
						},
						"spinner":{
							".hidden":true
						},
						"":{
							"!click":function() {
								form.setData({
									edit_orders: orderMap,
									edit_order: JSON.stringify(x.id),
									order_size: adjNumN(Math.abs(x.size)),
									order_price: adjNumN(invp(x.price)),
								});
								dialogRules();
							}
						}
					}
				});
				var cur_edit = form.readData(["edit_order"]);
				if (cur_edit.edit_order == "") {
					formdata.edit_orders=orderMap;
					formdata.edit_order = {
							"!change": function() {
								if (this.value=="") {
									form.setData({
										"order_size":"",
										"order_price":""
									});
								} else {								
									var itm = orders.find(function(x) {
										return this.value == JSON.stringify(x.id);
									}.bind(this));
									if (itm) form.setData({
										order_size: adjNumN(Math.abs(itm.size)),
										order_price: adjNumN(invp(itm.price))
									});
								}
								dialogRules();
							}
					}
				}
				
				form.setData(formdata);				
				return rs;
			}).then(
				function(x){running_update=false;return x;},function(x){running_update=false;console.error(x);});
			
		return f;
	}
	var willUpdate = 0;
	function asyncUpdate(p) {
		willUpdate++;
		return p.then(function(x) {
			willUpdate--;
			if (willUpdate <= 0) {
				willUpdate = 0;
			    return update().then(function(){return x;});				
			} else {
				return x;
			}
		},function(x) {
			willUpdate--;
			if (willUpdate <= 0) {
				willUpdate = 0;
			}
			throw x;
		});
	}
	function cycle_update() {
		if (!form.getRoot().isConnected) return;
		setTimeout(cycle_update, 15000);
		;if (willUpdate == 0) return update();
	}
	
	function clearForm() {
		form.setData({
			"order_size":"",
			"order_price":"",
			"edit_order":""
		});
		dialogRules();
	}
	
	
	cycle_update().then(function(f) {
		var pair = f.pair;

		function postOrder() {
			if (!_this.stopped[id] && cfg.enable) {
				_this.dlgbox({text:_this.strtable.trade_ask_stop},"confirm").then(
					function() {
						return _this.waitScreen(fetch_with_error(_this.traderURL(cfg.id)+"/stop",{method:"POST"}));
					}).then(function() {
						_this.stopped[id] = true;
						postOrder.call(this);
					}.bind(this));
				return;
			}

			var b = this.dataset.name;
			var d = form.readData(["edit_order","order_price","order_size","orders"]);

			var price = b == "button_buybid"?(pair.invert_price?"ask":"bid")
						:(b == "button_sellask"?(pair.invert_price?"bid":"ask"):
								(pair.invert_price?1/d.order_price:d.order_price));
			var size = ((b == "button_buybid" || b == "button_buy") == pair.invert_price?-1:1)*d.order_size;	
			var id;
			if (d.edit_order) id = JSON.parse(d.edit_order);
			var url = _this.traderPairURL(cfg.id, cfg.pair_symbol)+"/orders";
			var req = {
					size: size,
					price: price,
					replaceId: id,
					replaceSize: 0
			};
			var olst = d.orders;
			olst.push({
			    dir:d.order_size<0?_this.strtable.sell:_this.strtable.buy,
				size:adjNumN(Math.abs(d.order_size)),
				price:adjNumN(d.order_price),
				cancel:{".hidden":true},
				spinner:{".hidden":false}
			})
			form.setData({"orders":olst});
			clearForm();
			asyncUpdate(fetch_with_error(url, {method:"POST", body: JSON.stringify(req)}));
		}

		form.setItemEvent("button_buy","click", postOrder);
		form.setItemEvent("button_sell", "click",postOrder);
		form.setItemEvent("button_buybid", "click",postOrder);
		form.setItemEvent("button_sellask", "click",postOrder);
	}).catch(fetch_error);
} 

App.prototype.cancelOrder = function(trader, pair, id) {
	var url = this.traderPairURL(trader, pair)+"/orders"
	var req = {
			size:0,
			price:0,
			replaceId: id,
			replaceSize: 0,			
	};
	return fetch_with_error(url,{method:"POST",body: JSON.stringify(req)});
}

App.prototype.brokerConfig = function(burl) {
//	var burl = (exists?this.traderPairURL(trade_id, pair):this.pairURL(broker.name, pair))+"/settings";
	var form = fetch_with_error(burl).then(formBuilder);
	return this.dlgbox({form:form}, "broker_options_dlg").then(function() {
		form.then(function(f) {
			var d = f.readData();
			return fetch_with_error(burl, {method:"PUT",body:JSON.stringify(d)}).then(
			function(a){
				this.reload_trader_header();return a;
			}.bind(this));				
		}.bind(this))
	}.bind(this))
}

App.prototype.shareForm = function(id, form) {
	var cfg = this.saveForm(form, {});
	delete cfg.title;
	delete cfg.enable;
	delete cfg.dry_run;
	delete cfg.report_order;
	delete cfg.hidden;
	var cfgstr = JSON.stringify(cfg);	
	var splt = "{{"+btoa(cfgstr).match(/.{1,24}/g).join(" ")+"}}";	
	var p = this.dlgbox({share_input:splt},"share_dlg");
	var dlg = p.view;
	var shelm = dlg.findElements("share_input")[0];
	shelm.readOnly=true;
	dlg.showItem("ok", false);
	dlg.showItem("paste", false)
	dlg.setItemEvent("import","click",function(){
		dlg.setItemValue("share_input","");
		dlg.showItem("ok", true);
		dlg.showItem("import", false);
		dlg.showItem("paste", true);
		dlg.showItem("copy", false);		
		shelm.readOnly=false;
		shelm.focus();
	});
	dlg.setItemEvent("copy","click",function(){
			shelm.focus();
			shelm.select();
			document.execCommand("copy");
	});
	dlg.setItemEvent("paste","click",function(){
		if (navigator.clipboard && navigator.clipboard.readText) {
			navigator.clipboard.readText().then(function(x){
				shelm.value = x;
			});			
		} else {
			shelm.focus();
			shelm.select();			
			document.execCommand("paste");
		}
	});
	p.then(function(){
		var txt = shelm.value;	
		txt = txt.trim();
		if (txt.startsWith("{{") && txt.endsWith("}}")) {
			txt = txt.substr(2,txt.length-4);
			txt = txt.split(/\s+/).join("");
			var json = atob(txt);
			try {
				var newcfg = JSON.parse(json);
				this.traders[id] = Object.assign(this.traders[id], newcfg);
				this.undoTrader(id);
				return;
			} catch (e) {
				
			}
		}
		this.dlgbox({"text":this.strtable.import_failed,"cancel":{hidden:true}},"confirm");
	}.bind(this));
}

App.prototype.editStrategyState = function(id) {
	var url = this.traderURL(id)+"/strategy";
	this.waitScreen(fetch_with_error(url)).then(function(data){
		var dlg = TemplateJS.View.fromTemplate("advedit_dlg");
		dlg.setData({"json_data":JSON.stringify(data,null,"  ")});
		dlg.openModal();		
		dlg.setCancelAction(function(){
			dlg.close();
		},"cancel");
		dlg.setDefaultAction(function(){
			var data = dlg.readData().json_data;
			var vald = JSON.parse(data);
			this.waitScreen(fetch_with_error(url,{"method":"PUT","body":data})).then(
			function() {
				dlg.close();
				this.undoTrader(id);
			 }.bind(this));
		}.bind(this),"ok");
	}.bind(this));
}

function showProgress(id) {
	var dlg = TemplateJS.View.fromTemplate("progress_dlg");
	dlg.openModal();
	dlg.setData({
		"progress":0,
	});
	return Promise.resolve(id).then(function(id) {
		dlg.setItemEvent("stop","click",function(){
				fetch("../api/admin/progress/"+id, {"method":"DELETE"});
		});
		return new Promise(function(ok) {
			
			var intr = setInterval(function(){
				fetch("../api/admin/progress/"+id).then(
				function(resp){
					if (resp.status == 204) {
						clearInterval(intr);
						dlg.setData({"progress":100})
						ok(dlg);
					} else if (resp.status == 200) {
						resp.json().then(function(numb) {
							dlg.setData({"progress":numb});
							dlg.enableItem("stop",true);
						})
					}
				});
			},1000);
		});
	},function(e){
		dlg.close();
		throw e;
	});
}


App.prototype.shgControlPosition = function(id, form) {
	var inv = form._pair.invert_price;
	var cur = form._backtest_balance;
	var url = this.traderURL(id)+"/strategy";
    fetch_with_error(url).then(function(res){
    	var prom;
    	var dlg;
    	if (!res.sinh_gen || !res.sinh_gen.budget) {
    		this.dlgbox({"text":this.strtable.sinhgen_na,"cancel":{".hidden":true}},"confirm");
    		return;
    	}
        prom = this.dlgbox({curbudget: adjNumN(cur),profit:0,wantbudget:adjNumN(cur),ok:{".disabled":true}},"shg_control_position");
        prom.then(function(r) {
        	 this.dlgbox({
        	 	   "text":this.strtable.sinhgen_overwrite,
        	 	   "ok":this.strtable.yes,
        	 	   "cancel":this.strtable.no},"confirm").then(function(){
        	 	   	    var rd = dlg.readData();
                        this.waitScreen(fetch_with_error(url).then(function(res){
							res.sinh_gen.budget = rd.wantbudget;
							res.sinh_gen.find_k = invSize((rd.wantbudget-rd.curbudget) * parseInt(rd.side),inv);
                        	return fetch_with_error(url,{method:"PUT",body:JSON.stringify(res)}).then(
							function(){
                        		this.undoTrader(id);
                        	}.bind(this));
                        }.bind(this)));
        	 	   }.bind(this));
        }.bind(this));
        dlg = prom.view;
        var dlgRules = function(){
            var rd = dlg.readData();
            if (this.dataset.name == "profit") {
            	rd.wantbudget = rd.curbudget*(1+rd.profit*0.01);
            	dlg.setItemValue("wantbudget", adjNumN(rd.wantbudget));
            }
            if (this.dataset.name == "wantbudget") {
            	dlg.setItemValue("profit", (((rd.wantbudget/rd.curbudget)-1)*100).toFixed(2));
            }
            if (parseInt(rd.side) && rd.wantbudget>rd.curbudget) {
            	res.sinh_gen.budget = rd.wantbudget;
            	res.sinh_gen.find_k = invSize((rd.wantbudget-rd.curbudget) * parseInt(rd.side),inv);
            	res.dry_run = true;
            	fetch_with_error(url,{method:"PUT",body:JSON.stringify(res)}).then(function(out){
            		dlg.setItemValue("position", adjNumN(out.Position));
            		dlg.setItemValue("leverage", out["Leverage[x]"].toFixed(2));
            		dlg.setItemValue("pt", out["Price-neutral"].toFixed(2));
            		dlg.enableItem("ok",true);
            	});
            } else {
            		dlg.setItemValue("position", "");
            		dlg.setItemValue("leverage", "");            	
            		dlg.setItemValue("pt","");
            		dlg.enableItem("ok",false);
            }
        };
        ["curbudget","profit","wantbudget"].forEach(function(itm){
        	dlg.setItemEvent(itm,"input",dlgRules)
        },this);
        dlg.setItemEvent("side","change",dlgRules);        
    }.bind(this));
}

App.prototype.paperTrading = function(id,trg) {
	if (trg._paper) {
        var txts = document.getElementById("strtable");
        this.dlgbox({text: txts.dataset.paper_merge},"confirm").then(function() {
        	trg.save();
			this.curForm.close();
			var orig_id = this.traders[id].pp_source;
            var newcfg = Object.assign({},this.traders[id]);
            var orig_trd = this.traders[orig_id];
            newcfg.adj_timeout = orig_trd.adj_timeout;                        
            newcfg.id = orig_id;
            newcfg.enabled = orig_trd.enabled;
            delete newcfg.paper_trading;
            delete newcfg.pp_source;
            this.traders[orig_id] = newcfg;
            delete this.traders[id];
            trg.save = function() {};
            this.updateTopMenu(orig_id);
		}.bind(this));        		
	} else {
		trg.save();
		var new_id = id+"_paper";
		var b;
		(b = this.dlgbox({"name":{
			value: new_id,
			"!input":function() {
				b.view.enableItem("ok",!!this.value.length);
			}}},"clone_enter_id")).then(function(x) {				
			new_id = x.name.trim();			
			if (new_id.length) {
				if (!this.traders[new_id]) {
					var pap  =this.traders[new_id] = Object.assign({},this.traders[id]);
					pap.id = new_id;
					pap.paper_trading = true;
					pap.pp_source = id;
					pap.adj_timeout = 5;
					pap.enabled = true;
				}
				this.updateTopMenu(new_id);
			}
		}.bind(this));

	}	
}
