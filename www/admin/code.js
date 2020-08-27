"use strict";

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
	this.curForm = null;
	this.advanced = false;
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
		form.showItem("strategy_stairs",state.strategy == "stairs");
		form.showItem("strategy_gauss",state.strategy == "errorfn");
		form.showItem("strategy_hyperbolic",state.strategy == "hyperbolic"||state.strategy == "linear"||state.strategy == "sinh");
		form.showItem("kv_valinc_h",state.strategy == "keepvalue");
		form.showItem("exp_optp_h",state.strategy == "exponencial"||state.strategy == "hypersquare"||state.strategy == "conststep");
		form.showItem("show_curvature", state.strategy == "sinh")
		form.setData({"help_goal":{"class":state.strategy}});
		form.getRoot().classList.toggle("no_adv", !state["advanced"]);
		form.getRoot().classList.toggle("no_experimental", !state["check_unsupp"]);
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
			id: this.traders[x].id,			
		};
	},this);
	items.sort(function(a,b) {
		return a.broker.localeCompare(b.broker);
	});
	items.unshift({
		"image":"../res/options.png",
		"caption":this.strtable.options,				
		"id":"$"
		
	})
	items.unshift({
		"image":"../res/security.png",
		"caption":this.strtable.access_control,				
		"id":"!"
		
	})
	items.push({
		"image":"../res/add_icon.png",
		"caption":"",				
		"id":"+"
	});
	items.forEach(function(x) {
		x[""] = {"!click":function(id) {
			form.select(id);
		}.bind(null,x.id)}
	});
	form.setData({"item": items});
	form.select = function(id) {
		var update = items.map(function(x) {
			return {"":{"classList":{"selected": x.id == id}}};
		});
		form.setData({"item": update});
		
		var nf;
		if (id == "!") {
			if (this.curForm) {
				this.curForm.save();				
				this.curForm = null;
			}
			nf = this.securityForm();
			this.desktop.setItemValue("content", nf);
			nf.save = function() {};

		} else if (id == '$') {
			if (this.curForm) {
				this.curForm.save();				
				this.curForm = null;
			}
			nf = this.optionsForm();
			this.desktop.setItemValue("content", nf);
			this.curForm = nf;
			
		} else if (id == "+") {
			this.brokerSelect().then(this.pairSelect.bind(this)).then(function(res) {
				var broker = res[0];
				var pair = res[1];
				var name = res[2];				
				if (!this.traders[name]) this.traders[name] = {};
				var t = this.traders[name];
				t.broker = broker;
				t.pair_symbol = pair;
				t.id = name;
				t.enabled = true;
				t.dry_run = false;
				if (!t.title) t.title = pair;
				this.updateTopMenu(name);
			}.bind(this))
		} else {
			if (this.curForm) {
				this.curForm.save();				
				this.curForm = null;
			}

			nf = this.openTraderForm(id);			

			this.desktop.setItemValue("content", TemplateJS.View.fromTemplate("main_form_wait"));
			
			nf.then(function (nf) {
				this.desktop.setItemValue("content", nf);
				this.curForm = nf;
				this.curForm.save = function() {
					this.traders[id] = this.saveForm(nf, this.traders[id]);
					this.updateTopMenu();
				}.bind(this);
				this.curTrader = this.traders[id];
			}.bind(this));
		}
	}.bind(this);
	
	return form;
}

App.prototype.processConfig = function(x) {	
	this.config = x;
	this.users = x["users"] || [];
	this.traders = x["traders"] || {}
	for (var id in this.traders) this.traders[id].id = id;
	return x;	
}

App.prototype.loadConfig = function() {
	return fetch_with_error("api/config").then(this.processConfig.bind(this));
}

App.prototype.brokerURL = function(broker) {
	return "api/brokers/"+encodeURIComponent(broker);
}
App.prototype.pairURL = function(broker, pair) {
	return this.brokerURL(broker) + "/pairs/" + encodeURIComponent(pair);	
}
App.prototype.brokerImgURL = function(broker) {
	return this.brokerURL(broker) + "/icon.png";	
}
App.prototype.traderURL = function(trader) {
	return "api/traders/"+encodeURIComponent(trader);
}
App.prototype.traderPairURL = function(trader, pair) {
	return "api/traders/"+encodeURIComponent(trader)+"/broker/pairs/" + encodeURIComponent(pair);	
}
App.reload_trader_header=function() {}

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
	
	
	var apikey = this.config.apikeys && this.config.apikeys[src.broker];
	var first_fetch = true;

	var updateHdr = function(){
		var state = fetch_json("api/editor",{
			method:"POST",
			body: JSON.stringify({
				broker: src.broker,
				trader: src.id,
				pair: src.pair_symbol
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
				if (x.t.indexOf("swap assets") != -1) {
				    data.swap_symbols_hide={".hidden":false};
				}
				trg.setData(data);
			});
		});			
	}.bind(this);
	
	var fillHeader = function(state, data) {
				
	
		var broker = state.broker;
		var pair = state.pair;
		var orders = state.orders;
		this.fillFormCache[src.id] = state;

		data.broker = broker.exchangeName;
		data.no_api_key = {".hidden": !!(apikey === undefined?broker.trading_enabled:apikey)};
		data.api_key_not_saved = {".hidden": apikey === undefined || !(!broker.trading_enabled && apikey)};
		data.broker_id = broker.name;
		data.broker_ver = broker.version;
		data.asset = pair.asset_symbol;
		data.currency = pair.currency_symbol;
		data.balance_asset= adjNum(invSize(pair.asset_balance,pair.invert_price));
		data.balance_currency = adjNum(pair.currency_balance);
		data.price= adjNum(invPrice(pair.price,pair.invert_price));
		data.fees =adjNum(pair.fees*100,4);
		data.leverage=pair.leverage?pair.leverage+"x":"n/a";
		trg._balance = pair.currency_balance;
		trg._price = invPrice(pair.price, pair.invert_price);

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
					"!click":this.brokerConfig.bind(this, src.broker, src.pair_symbol)
				}					
		data.open_orders_sect = {".hidden":!mp.length};


		
		function calcPosition(data) {
			var v = trg.readData(["report_position_offset"]);
			var cpos = invSize((isFinite(v.report_position_offset)?v.report_position_offset:0) + state.position,pair.invert_price);
			if (isFinite(cpos)) {
				var apos = invSize(pair.asset_balance, pair.invert_price) ;
				data.hdr_position = adjNum(cpos);
				data.sync_pos = {
						".hidden":(Math.abs(cpos - apos) <= (Math.abs(cpos)+Math.abs(apos))*1e-8)
				};
			} else {
				data.hdr_position = adjNum();
				data.sync_pos = {};
			}
		}
		calcPosition(data);

		function recalcStrategy() {
			var data = trg.readData()
			var strategy = getStrategyData(data);
			var req = {
					strategy:strategy,
					price:pair.price,
					assets:pair.asset_balance,
					currency:pair.currency_balance,
					leverage:pair.leverage,
					inverted: pair.invert_price,
					trader:src.id
			};
			if (recalcStrategy.ip) {
				recalcStrategy.queued = true;
			} else {
				recalcStrategy.ip = true;
				recalcStrategy.queued = false;
				fetch_json("api/strategy", {method:"POST",body:JSON.stringify(req)})
					.then(function(r){
						recalcStrategy.ip = false;
						trg.setData({range_min_price:adjNum(r.min), range_max_price:adjNum(r.max), range_initial:adjNum(r.initial)});
						initial_pos = adjNumN(r.initial);
						if (recalcStrategy.queued) {
							recalcStrategy();
						}
					},function(e){
						recalcStrategy.ip = false;
						console.error(e);
						if (recalcStrategy.queued) {
							recalcStrategy();
						}
					})
			}
		}

		recalc_strategy_fn = recalcStrategy.bind(this);
		
		data.err_external_assets={
			"classList":{mark:!pair.asset_balance && !pair.leverage}
		};
		data.err_external_assets_margin={
			"classList":{mark:!pair.asset_balance && pair.leverage && !pair.invert_price}
		};
		data.err_external_assets_inverse={
			"classList":{mark:!pair.asset_balance && pair.invert_price}
		};
		if (state.budget) {
		    data.calc_budget = adjNum(state.budget.total);
		    data.calc_budget_extra = adjNum(state.budget.extra);
		    data.l_budget = {".hidden":false};
		} else {
            data.l_budget = {".hidden":true};			
		}
		
		if (first_fetch) {
			["strategy","external_assets","gs_external_assets", "hp_dtrend","hp_longonly","hp_power", "hp_maxloss", "hp_recalc", "hp_asym","hp_powadj", "hp_extbal", "hp_reduction","hp_dynred","exp_optp","sh_curv"]
			.forEach(function(item){
				trg.findElements(item).forEach(function(elem){
					elem.addEventListener("input", function(){recalc_strategy_fn();});
					elem.addEventListener("change", function(){recalc_strategy_fn();});
				}.bind(this));
			}.bind(this));
            var optp = trg.readData(["exp_optp"]);
           	var nprice = pair.invert_price?1.0/pair.price:pair.price;
            if (isNaN(optp.exp_optp)) {
           	var nstep = Math.pow(10,Math.round(Math.log10(nprice))-2);            	
                data.exp_optp = {
                	value:Math.floor(nprice/nstep)*nstep,
                	step:nstep
                }
            } else{
               	var nstep = Math.pow(10,Math.round(Math.log10(optp.exp_optp))-2);            	
                data.exp_optp = {
                	step:nstep
                }            	
            }

			setTimeout(recalc_strategy_fn,1);
			
			data.vis_spread = {"!click": this.init_spreadvis.bind(this, trg, src.id), ".disabled":false};
			data.show_backtest= {"!click": this.init_backtest.bind(this, trg, src.id, src.pair_symbol, src.broker), ".disabled":false};
			data.inverted_price=pair.invert_price?"true":"false";
			var tmp = trg.readData(["cstep","max_pos"]);
			data.sync_pos["!click"] = function() {
				var data = {};
				var v = trg.readData(["report_position_offset"]);
				var s = pair.asset_balance  - state.position;
				trg.setData({report_position_offset:s});
				calcPosition(data);
				trg.setData(data);
			}
			if (!pair.leverage) {
				var elm = trg.findElements("st_power")[0].querySelector("input[type=range]");
				elm.setAttribute("max","199");
			}
			if (!src.strategy && typeof state.pair.price == "string" && state.pair.price.startsWith("trainer")){
			    this.brokerConfig(src.broker, src.pair_symbol).then(updateHdr,updateHdr);
			}
			first_fetch = false;
		}
		data.swap_symbols_hide = {".hidden":!!(pair.leverage || state.trades) && !src.swap_symbols};			
		
		
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

	
	data.strategy = (src.strategy && src.strategy.type) || "";
	data.cstep = 0;
	data.acum_factor = 0;
	data.external_assets = 0;
	data.kv_valinc = 0;
	data.kv_halfhalf=false;
	data.st_power={"value":1.7};
	data.st_max_step=1;
	data.st_reduction_step=2;
	data.st_pattern = "constant";
	data.st_sl=false;
	data.hp_reduction=50;
	data.sh_curv=5;
	data.hp_initboost=0;
	data.hp_asym=0;
	data.hp_power=1;
	data.hp_powadj=0;
	data.hp_dynred=0;
	data.hp_maxloss=0;
	data.hp_dtrend={value:false};
	data.hp_lb_asym="asym";
	data.inverted_price="false";
	data.hp_longonly=false;
	data.gs_rb_lo_p=20;
	data.gs_rb_lo_a=85;
	data.gs_rb_hi_p=95;
	data.gs_rb_hi_a=100;

	function powerCalc(x) {return adjNumN(Math.pow(10,x)*0.01);};

	
	if (data.strategy == "halfhalf" || data.strategy == "keepvalue" || data.strategy == "exponencial"|| data.strategy == "hypersquare"||data.strategy == "conststep") {
		data.acum_factor = filledval(defval(src.strategy.accum,0)*100,0);
		data.external_assets = filledval(src.strategy.ea,0);
		data.kv_valinc = filledval(src.strategy.valinc,0);
		data.kv_halfhalf = filledval(src.strategy.halfhalf,false);
		data.exp_optp = filledval(src.strategy.optp,"");
	} else if (data.strategy == "errorfn" ) {
		data.gs_external_assets = filledval(src.strategy.ea,0);
		data.gs_rb_lo_p=filledval(defval(src.strategy.rb_lo_p,0.1)*100,10);
		data.gs_rb_hi_p=filledval(defval(src.strategy.rb_hi_p,0.95)*100,95);
		data.gs_rb_lo_a=filledval(defval(src.strategy.rb_lo_a,0.5)*100,50);
		data.gs_rb_hi_a=filledval(defval(src.strategy.rb_hi_a,0.5)*100,50);
	} else if (data.strategy == "hyperbolic"||data.strategy == "linear"||data.strategy == "sinh") {
		data.hp_reduction = filledval(defval(src.strategy.reduction,0.25)*200,50);
		data.hp_initboost = filledval(src.strategy.initboost,0);
		data.hp_asym = filledval(defval(src.strategy.asym,0.2)*100,20);
		data.hp_maxloss = filledval(src.strategy.max_loss,0);
		data.hp_recalc= filledval(src.strategy.recalc_mode,"position");
		data.hp_power = filledval(src.strategy.power,1);
		data.hp_powadj = filledval(src.strategy.powadj,0);
		data.hp_dynred = filledval(src.strategy.dynred,0);
		data.hp_extbal = filledval(src.strategy.extbal,0);
		data.hp_dtrend = filledval(src.strategy.dtrend,false);
		data.hp_longonly = filledval(src.strategy.longonly,false);
		data.sh_curv = filledval(src.strategy.curv,5);
		data.hp_lb_asym = src.strategy.dtrend?"trend":"asym"; 
	} else if (data.strategy == "stairs") {
		data.st_power = filledval(src.strategy.power,1.7);
		data.st_show_factor = powerCalc(data.st_power.value)
		data.st_max_step = filledval(src.strategy.max_steps,1);
		data.st_pattern = filledval(src.strategy.pattern,"constant");
		data.st_reduction_step= filledval(src.strategy.reduction_steps,2);
		data.st_redmode= filledval(src.strategy.redmode,"stepsBack");
		data.st_tmode=filledval(src.strategy.mode, "auto");
		data.st_sl=filledval(src.strategy.sl,false);
	}
	data.st_power["!change"] = function() {
		trg.setItemValue("st_show_factor",powerCalc(trg.readData(["st_power"]).st_power));
	};
	data.hp_dtrend["!change"] = function() {
		trg.setItemValue("hp_lb_asym",trg.readData(["hp_dtrend"]).hp_dtrend?"trend":"asym");
	};
	data.enabled = src.enabled;
	data.hidden = !!src.hidden;
	data.dry_run = src.dry_run;
	data.swap_symbols = !!src.swap_symbols;
	data.accept_loss = filledval(src.accept_loss,0);
	data.grant_trade_hours= filledval(src.grant_trade_hours,0);
	data.spread_calc_stdev_hours = filledval(src.spread_calc_stdev_hours,4);
	data.spread_calc_sma_hours = filledval(src.spread_calc_sma_hours,25);
	data.dynmult_raise = filledval(src.dynmult_raise,440);
	data.dynmult_fall = filledval(src.dynmult_fall, 5);
	data.dynmult_mode = filledval(src.dynmult_mode, "independent");
	data.dynmult_scale = filledval(src.dynmult_scale,true);
	data.dynmult_sliding = filledval(src.dynmult_sliding,false);
	data.dynmult_mult = filledval(src.dynmult_mult, true);
	data.spread_mult = filledval(Math.log(defval(src.buy_step_mult,1))/Math.log(2)*100,0);
	data.order_mult = filledval(defval(src.buy_mult,1)*100,100);
	data.min_size = filledval(src.min_size,0);
	data.max_size = filledval(src.max_size,0);
	data.internal_balance = filledval(src.internal_balance,0);
	data.alerts = filledval(src.alerts,false);
	data.delayed_alerts= filledval(src.delayed_alerts,false);
	data.detect_manual_trades = filledval(src.detect_manual_trades,false);
	data.report_position_offset = filledval(src.report_position_offset,0);
	data.report_order = filledval(src.report_order,0);
	data.force_spread = filledval(adjNum((Math.exp(defval(src.force_spread,0))-1)*100),"0.000");
	data.max_balance = filledval(src.max_balance,"");
	data.min_balance = filledval(src.min_balance,"");
	data.zigzag = filledval(src.zigzag,false);
		

	
	
	data.icon_repair={"!click": function() {
		this.repairTrader(src.id,initial_pos)}.bind(this)};
	data.icon_reset={"!click": this.resetTrader.bind(this, src.id)};
	data.icon_delete={"!click": this.deleteTrader.bind(this, src.id)};
	data.icon_undo={"!click": this.undoTrader.bind(this, src.id)};
	data.icon_trading={"!click":this.tradingForm.bind(this, src.id)};
	data.icon_share={"!click":this.shareForm.bind(this, src.id, trg)};
	data.advedit = {"!click": this.editStrategyState.bind(this, src.id)};
	
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

function getStrategyData(data) {
	var strategy = {};
	strategy.type = data.strategy;
	if (data.strategy == "halfhalf" || data.strategy == "keepvalue" || data.strategy == "exponencial"|| data.strategy == "hypersquare"||data.strategy == "conststep") {
		strategy.accum = data.acum_factor/100.0;
		strategy.ea = data.external_assets;
		strategy.valinc = data.kv_valinc;
		strategy.halfhalf = data.kv_halfhalf;
		strategy.optp = data.exp_optp;
	} else if (data.strategy == "errorfn") {
		strategy = {
				type: data.strategy,
				ea: data.gs_external_assets,
				rb_hi_a: data.gs_rb_hi_a/100,
				rb_lo_a: data.gs_rb_lo_a/100,
				rb_hi_p: data.gs_rb_hi_p/100,
				rb_lo_p: data.gs_rb_lo_p/100,
		};
	} else 	if (data.strategy == "hyperbolic"||data.strategy == "linear"||data.strategy == "sinh") {
		strategy = {
				type: data.strategy,
				power: data.hp_power,
				powadj: data.hp_powadj,
				dynred: data.hp_dynred,
				extbal: data.hp_extbal,
				dtrend: data.hp_dtrend,
				longonly: data.hp_longonly,
				max_loss: data.hp_maxloss,
				recalc_mode: data.hp_recalc,
				asym: data.hp_asym / 100,
				reduction: data.hp_reduction/200,
				initboost: data.hp_initboost,
				curv: data.sh_curv,
				
		};
	} else 	if (data.strategy == "stairs") {
		strategy ={
				type: data.strategy,
				power : data.st_power,
				max_steps: data.st_max_step,
				pattern: data.st_pattern,
				reduction_steps:data.st_reduction_step,
				redmode:data.st_redmode,
				mode:data.st_tmode,
				sl:data.st_sl
		}
	}
	return strategy;
}

App.prototype.saveForm = function(form, src) {

	var data = form.readData();
	var trader = {}
	var goal = data.goal;
	trader.strategy = getStrategyData(data);
	trader.id = src.id;
	trader.broker =src.broker;
	trader.pair_symbol = src.pair_symbol;
	trader.title = data.title;
	trader.enabled = data.enabled;
	trader.dry_run = data.dry_run;
	trader.hidden = data.hidden;
	trader.swap_symbols = data.swap_symbols;
	this.advanced = data.advanced;
	trader.accept_loss = data.accept_loss;
	trader.grant_trade_hours = data.grant_trade_hours;
	trader.spread_calc_stdev_hours =data.spread_calc_stdev_hours ;
	trader.spread_calc_sma_hours  = data.spread_calc_sma_hours;
	trader.dynmult_raise = data.dynmult_raise;
	trader.dynmult_fall = data.dynmult_fall;
	trader.dynmult_mode = data.dynmult_mode;
	trader.dynmult_scale = data.dynmult_scale; 
	trader.dynmult_sliding = data.dynmult_sliding;
	trader.dynmult_mult = data.dynmult_mult;
	trader.zigzag = data.zigzag;
	trader.buy_mult = data.order_mult/100;
	trader.sell_mult = data.order_mult/100;
	trader.buy_step_mult = Math.pow(2,data.spread_mult*0.01)
	trader.sell_step_mult = Math.pow(2,data.spread_mult*0.01)
	trader.min_size = data.min_size;
	trader.max_size = data.max_size;
	trader.internal_balance = data.internal_balance;
	trader.alerts = data.alerts;
	trader.delayed_alerts= data.delayed_alerts;
	trader.detect_manual_trades = data.detect_manual_trades;
	trader.report_position_offset = data.report_position_offset;
	trader.report_order = data.report_order;
	trader.force_spread = Math.log(data.force_spread/100+1);
	if (isFinite(data.min_balance)) trader.min_balance = data.min_balance;
	if (isFinite(data.max_balance)) trader.max_balance = data.max_balance;
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
		location.href="api/login";
	});
	
	
	return this.loadConfig().then(function() {
		top_panel.showItem("login",false);
		top_panel.showItem("save",true);
		var menu = this.createTraderList();
		this.menu =  menu;
		this.desktop.setItemValue("menu", menu);
		this.stopped = {};
		
		
	}.bind(this));
	
}

App.prototype.brokerSelect = function() {
	var _this = this;
	return new Promise(function(ok, cancel) {
		
		var form = TemplateJS.View.fromTemplate("broker_select");
		_this.waitScreen(fetch_with_error("api/brokers/_all")).then(function(x) {
			form.openModal();
			var show_excl = false;
			var lst = x.map(function(z) {
				var e = _this.config.apikeys && _this.config.apikeys[z];
				var ex = e !== undefined || z.trading_enabled;
				show_excl = show_excl || !ex;
				return {
					excl_info: {".hidden":ex},
					image:_this.brokerImgURL(z.name),
					caption: z.name,
					"":{"!click": function() {
							form.close();
							ok(z.name);
						}}
				};
			});
			form.setData({
				"item":lst,
				"excl_info": {".hidden": !show_excl},
			});
			form.setCancelAction(function() {
				form.close();
				cancel();
			},"cancel");

		});
	});
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
			ok([broker, d.pair, name]);
		},"ok");
		fetch_with_error(_this.pairURL(broker,"")).then(function(data) {
			var pairs = [{"":{"value":"----",".value":""}}].concat(data["entries"].map(function(x) {
				return {"":{"value":x,".value":x}};
			}));			
			form.setItemValue("item",pairs);
			form.showItem("spinner",false);
			dlgRules();
		},function() {form.close();cancel();});
		form.setItemValue("image", _this.brokerImgURL(broker));
		var last_gen_name="";
		function dlgRules() {
			var d = form.readData(["pair","name"]);
			if (d.name == last_gen_name) {
				d.name = last_gen_name = broker+"_"+d.pair;
				form.setItemValue("name", last_gen_name);
			}
			form.enableItem("ok", d.pair != "" && d.name != "");			
		};
		
		
		form.setItemEvent("pair","change",dlgRules);
		form.setItemEvent("name","change",dlgRules);
		dlgRules();
	});
	
}

App.prototype.updateTopMenu = function(select) {
	this.createTraderList(this.menu);
	if (select) this.menu.select(select);
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


		return this.waitScreen(Promise.all([cr,dr]))
			.catch(function(){})
			.then(function() {
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

App.prototype.resetTrader = function(id) {
	this.dlgbox({"text":this.strtable.askreset,
		"ok":this.strtable.yes,
		"cancel":this.strtable.no},"confirm").then(function(){

		var tr = this.traders[id];
		this.waitScreen(fetch_with_error(
			this.traderURL(tr.id)+"/reset",
			{method:"POST"})).then(function() {				
						this.updateTopMenu(tr.id);				
			}.bind(this));
	}.bind(this));
}

App.prototype.repairTrader = function(id, initial) {
		var form=TemplateJS.View.fromTemplate("reset_strategy");
		var view;
		var p = this.dlgbox({rpos:{
			value:initial,
            "!click":function(){
                view.setItemValue("setpos",this.value);	
            }}
			},"reset_strategy");

		p.then(function(){
			var tr = this.traders[id];
			var data = view.readData();
			var req ="";
			if (!isNaN(data.setpos)) {
                req=JSON.stringify({
                	"achieve":data.setpos
                });
			}
			this.waitScreen(fetch_with_error(
				this.traderURL(tr.id)+"/repair",
				{method:"POST",body:req})).then(function() {
							this.updateTopMenu(tr.id);				
				}.bind(this));
	}.bind(this));
    view = p.view;
}

App.prototype.cancelAllOrders = function(id) {
	return this.dlgbox({"text":this.strtable.askcancel,
		"ok":this.strtable.yes,
		"cancel":this.strtable.no},"confirm").then(this.cancelAllOrdersNoDlg.bind(this,id))
			.then(function() {
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
		
		function setKey(flag, broker, binfo, cfg) {
	
			if (flag) {
				fetch_with_error(this.brokerURL(broker)+"/apikey")
				.then(function(ff){
					var w = formBuilder(ff);
					w.setData(cfg);
					this.dlgbox({text:w},"confirm").then(function() {
						if (!this.config.apikeys) this.config.apikeys = {};
						this.config.apikeys[broker] = w.readData();
						form.showItem("need_save",true);
						update.call(this);
					}.bind(this));
				}.bind(this));
			} else {
				if (!this.config.apikeys) this.config.apikeys = {};
				this.config.apikeys[broker] = null;
				form.showItem("need_save",true);
				update.call(this);
				
			}
			
		}
		
		var brokers = fetch_with_error(this.brokerURL("_all")).
			then(function(x) {
				return x.map(function(binfo) {
					var z = binfo.name;
					var cfg = this.config.apikeys && this.config.apikeys[z]
					var e = cfg === undefined?binfo.trading_enabled:cfg;
					return {
						img:this.brokerImgURL(z),
						broker: z,
						exchange:  {
								value:binfo.exchangeName,
								href:binfo.exchangeUrl
							},						
						state: {
								value: e?"✓":"∅",
								classList:{set:e, notset:!e}
							},
						bset: {
								".disabled": binfo.trading_enabled && cfg === undefined,
								"!click": setKey.bind(this, true, z, binfo, cfg)
							},
						
						berase: {
								".disabled":!binfo.trading_enabled && !cfg,
								"!click": setKey.bind(this, false, z, binfo, cfg)
							},
						info_button: {
								"!click": function() {
									this.dlgbox({text:fetch_with_error(this.brokerURL(z)+"/licence")
											.then(function(t) {
												return {"value":t,class:"textdoc"};
											}),
										cancel:{".hidden":true}},"confirm")
								}.bind(this)
							},
						sub_button: {
							    "!click": function() {
                                    var dlg = TemplateJS.View.fromTemplate("subaccount_dlg");
                                    dlg.openModal();
                                    dlg.setCancelAction(function(){dlg.close();},"cancel");
                                    dlg.setDefaultAction(function(){
                                    	var d = dlg.readData();
                                    	if (d.name) {
                                    		fetch_with_error(this.brokerURL(z)+"/subaccount",{method:"POST",body:JSON.stringify(d.name)})
                                    		.then(update.bind(this))
                                    	}
                                    	dlg.close();
                                    }.bind(this),"ok");
							    }.bind(this),
							    ".hidden": !binfo.subaccounts
						}						
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
		dlg.enableItem("ok",true);
	})
	var res = new Promise(function(ok, cancel) {
		dlg.setCancelAction(function() {
			dlg.close();cancel();
		},"cancel");
		dlg.setDefaultAction(function() {
			dlg.close();ok();
		},"ok");
	});		
	res.view = dlg;
	return res;
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
	var top = this.top_panel;
	top.showItem("saveprogress",true);
	top.showItem("saveok",false);
	this.config.users = this.users;
	this.config.traders = this.traders;
	this.validate(this.config).then(function(config) {		
		fetch_json("api/config",{
			method: "PUT",
			headers: {
				"Content-Type":"application/json",
			},
			body:JSON.stringify(config)
		}).then(function(x) {
			top.showItem("saveprogress",false);
			top.showItem("saveok",true);
			this.processConfig(x);
			this.updateTopMenu();
			this.stopped = {};
			this.reload_trader_header();
		}.bind(this),function(e){			
			top.showItem("saveprogress",false);
			if (e.status == 409) {
				this.dlgbox({hdr:this.strtable.conflict1, 
							text:this.strtable.conflict2,
							ok:this.strtable.reload}, "confirm")
				.then(function() {
					location.reload();
				})
			} else {
				fetch_error(e);
			}
			
		}.bind(this));
	}.bind(this), function(e) {
		top.showItem("saveprogress",false);
		if (e.trader)   
			this.menu.select(e.trader);		
		this.dlgbox({text:e.message},"validation_error");
	}.bind(this));
}

App.prototype.logout = function() {
	this.desktop.setData({menu:"",content:""});
	this.config = null;
	if (document.cookie.indexOf("auth=") != -1) {
		fetch("../set_cookie",{
			"method":"POST",
			"body":"auth="
		}).then(function(resp) {
			if (resp.status == 202) {
				location.reload();
			}
		});
	} else {
		fetch("api/logout").then(function(resp) {
			if (resp.status == 200) {
				location.reload();
			}
		});
	}
}

App.prototype.remember = function() {
	
	function post(path, params, method='post') {

		  const form = document.createElement('form');
		  form.method = method;
		  form.action = path;

		  for (const key in params) {
		    if (params.hasOwnProperty(key)) {
		      const hiddenField = document.createElement('input');
		      hiddenField.type = 'hidden';
		      hiddenField.name = key;
		      hiddenField.value = params[key];

		      form.appendChild(hiddenField);
		    }
		  }

		  document.body.appendChild(form);
		  form.submit();
	}
	
	post("../set_cookie", {
		"auth":"auth",
		"permanent":"true",
		"redir":"admin/index.html"
	});
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
	function update() {
		updatefn(obj)
	}

	var tm;

	function delayUpdate() {
		if (tm) clearTimeout(tm);
		tm = setTimeout(function() {
			tm = null;
			update()},250);
	}
	obj.update = delayUpdate;

	inputs.forEach(function(a) {
		form.forEachElement(a, function(x){
			if (x.tagName == "BUTTON")
				x.addEventListener("click", delayUpdate);
			else {
				x.addEventListener("input",delayUpdate);
				x.addEventListener("change",delayUpdate);
			}
		})
	});
	update();
}

App.prototype.init_spreadvis = function(form, id) {
	var url = "api/spread"
	form.enableItem("vis_spread",false);
	var inputs = ["spread_calc_stdev_hours", "spread_calc_sma_hours","spread_mult","dynmult_raise","dynmult_fall","dynmult_mode","dynmult_sliding","dynmult_mult"];
	this.gen_backtest(form,"spread_vis_anchor", "spread_vis",inputs,function(cntr){

		cntr.showSpinner();
		var data = form.readData(inputs);
		var mult = Math.pow(2,data.spread_mult*0.01);
		var req = {
			sma:data.spread_calc_sma_hours,
			stdev:data.spread_calc_stdev_hours,
			mult:mult,
			raise:data.dynmult_raise,
			fall:data.dynmult_fall,
			mode:data.dynmult_mode,
			sliding:data.dynmult_sliding,
			dyn_mult:data.dynmult_mult,
			id: id
		}
		
		fetch_with_error(url, {method:"POST", body:JSON.stringify(req)}).then(function(v) {			
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
		]));
	return rows.join("\r\n");
}

var fill_atprice=true;
var show_op=false;
var invert_chart = false;
var reverse_chart = false;


App.prototype.init_backtest = function(form, id, pair, broker) {
	var url = "api/backtest";
	form.enableItem("show_backtest",false);		
	var inputs = ["strategy","external_assets", "acum_factor","kv_valinc","kv_halfhalf","min_size","max_size","order_mult","alerts","delayed_alerts","linear_suggest","linear_suggest_maxpos",
		"st_power","st_reduction_step","st_sl","st_redmode","st_max_step","st_pattern","dynmult_sliding","accept_loss","spread_calc_sma_hours","st_tmode","zigzag",
		"hp_dtrend","hp_longonly","hp_power","hp_maxloss","hp_asym","hp_reduction","sh_curv","hp_initboost","hp_extbal","hp_powadj","hp_dynred",
		"exp_optp","gs_external_assets","gs_rb_hi_a","gs_rb_lo_a","gs_rb_hi_p","gs_rb_lo_p",
		"min_balance","max_balance"];
	var spread_inputs = ["spread_calc_stdev_hours", "spread_calc_sma_hours","spread_mult","dynmult_raise","dynmult_fall","dynmult_mode","dynmult_sliding","dynmult_mult"];
	var balance = form._balance;
	var init_def_price = form._price;
	var days = 45*60*60*24*1000;
    var offset = 0;
    var offset_max = 0;
	var start_date = "";
	var sttype =this.traders[id].strategy.type;
    var show_norm= ["halfhalf","keepvalue","exponencial","errorfn","hypersquare","conststep"].indexOf(sttype) != -1;
	var infoElm;


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
		var last_dir = 0;
		var lastp = v[0].pr;

		var c = [];
		var ilst,acp = false;
		v.forEach(function(x) {
			var nacp = x.tm >= imin && x.tm <=imax;
			if (nacp || acp) {
				x.achg = x.sz;
				x.time = x.tm;
				if (ilst) {
				    c.push(ilst);
					ilst = null;
				}
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
				cost = cost + x.sz * x.pr;
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
			}
		});
		if (cur_streak > max_streak) max_streak = cur_streak;


        
		var last = c[c.length-1];
		var lastnp;
		var lastnpl;
		var lastop;
		var lastpl;
		if (offset) {
			var x = Object.assign({}, last);
			x.time = imax;
			lastnp = x.np;
			lastnpl = x.npla;
			lastop = x.op;
			lastpl = x.pl;
			c.push(x);
		}

		if (balance!==undefined) {
        cntr.bt.setData({
        	"pl":adjNumBuySell(vlast.pl),
        	"ply":adjNumBuySell(vlast.pl*31536000000/interval),
        	"npl":adjNumBuySell(vlast.npla),
        	"nply":adjNumBuySell(vlast.npla*31536000000/interval),
        	"max_pos":adjNum(max_pos),
        	"max_cost":adjNum(max_cost),
        	"max_loss":adjNumBuySell(-max_downdraw),
        	"max_loss_pc":adjNum(max_downdraw/balance*100,0),
        	"max_profit":adjNumBuySell(max_pl),
        	"pr": adjNum(max_pl/max_downdraw),
        	"pc": adjNumBuySell(vlast.pl*3153600000000/(interval*balance),0),
        	"npc": adjNumBuySell(vlast.npla*3153600000000/(interval*balance),1),
        	"showpl":{".hidden":show_norm},
        	"shownorm":{".hidden":!show_norm},
        	"trades": trades,
        	"buys": buys,
        	"sells": sells,
        	"alerts": alerts,
        	"ascents": ascents,
        	"descents": descents,
        	"streak": max_streak        
        });
		}

		var chart1 = cntr.bt.findElements('chart1')[0];
		var chart2 = cntr.bt.findElements('chart2')[0];
		if (interval > days) interval = days;
		var ratio = 4;		
		var scale = 900000;
		var drawChart = initChart(interval,ratio,scale,true);
		var drawMap1;
		var drawMap2;
		if (show_op) {
		    drawMap1=drawChart(chart1,c,"pr",lastop?[{label:"open",pr:lastop}]:[],"op");
		} else {
		    drawMap1=drawChart(chart1,c,"pr",lastnp?[{label:"neutral",pr:lastnp}]:[],"np");			
		}
		if (show_norm) {
		    drawMap2=drawChart(chart2,c,"npla",[{label:"PL",npla:lastpl}],"pl");
		} else {
		    drawMap2=drawChart(chart2,c,"pl",[{label:"norm",pl:lastnpl}],"npla");
		}
		var tm;
		if (infoElm)  {
		    infoElm.close();
		    infoElm = null;
		}

        show_info_fn = function(ev) {
        	if (tm) clearTimeout(tm);
        	tm = setTimeout(function() {
				tm = undefined;
        		var selmap;
        		if (drawMap1.msin(ev.clientX, ev.clientY)) selmap = drawMap1;
        		else if (drawMap2.msin(ev.clientX, ev.clientY)) selmap = drawMap2;
        		else {
        			if (infoElm) infoElm.close();
        			infoElm = null;
                    return;        			
        		}
                var bb = selmap.box();
                var ofsx = ev.clientX - bb.left;
                var ofsy = ev.clientY - bb.top;
                var cont = chart1.parentNode;
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
                		npl:adjNumBuySell(bestItem.npl),
                		sz:adjNumBuySell(bestItem.sz),
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


	var frst = true;

	var update;
	var update_recalc = function() {}
	var tm;
	var config;
	var opts
	var bal;
	var req;
	var res_data;	

	function swapshowpl() {
		show_norm = !show_norm;
		update_recalc();
	}

	function progress_wait() {		
		var dlg = TemplateJS.View.fromTemplate("upload_chart_wait");
		dlg.openModal();
		function wait(cb) {
			fetch_with_error("api/upload_prices").then(function(x) {
				if (x == -1) {
					dlg.close();
					cb();
				} 
				else {
					dlg.setData({progress:{"style":"width: "+x+"%"}});
					setTimeout(wait.bind(null,cb),1000);
				}
			}).catch(function() {
					setTimeout(wait.bind(null,cb),1000);										
			});
		}
		dlg.wait = function() {
			dlg.showItem("upload_mark",false);
			dlg.showItem("process_mark",true);
			return new Promise(wait);
		}
		dlg.setItemEvent("stop","click", function() {
			fetch_json("api/upload_prices", {method:"DELETE"});
		});
		return dlg;		
	}

	function recalc_spread(prices, cntr) {
		var data = form.readData(spread_inputs);
		var mult = Math.pow(2,data.spread_mult*0.01);
		var req = {
			sma:data.spread_calc_sma_hours,
			stdev:data.spread_calc_stdev_hours,
			mult:mult,
			raise:data.dynmult_raise,
			fall:data.dynmult_fall,
			mode:data.dynmult_mode,
			sliding:data.dynmult_sliding,
			dyn_mult:data.dynmult_mult,
			id: id,
			prices: prices,
			reverse: reverse_chart,
			invert: invert_chart,
		}
		var w = progress_wait();
		fetch_with_error("api/upload_prices", {method:"POST", body:JSON.stringify(req)}).then(function(){							
			w.wait().then(cntr.update.bind(cntr));
		}).catch(function(e) {
			w.close();
			console.error(e);
		});								
	}
	
	function upload_trades(data, cntr) {
		var req = {
			id: id,
			prices: data
		}
		cntr.showSpinner();
		fetch_with_error("api/upload_trades", {method:"POST", body:JSON.stringify(req)}).then(function(){							
			cntr.update();
			cntr.hideSpinner();
		}).catch(function(e) {
			console.error(e);
			cntr.hideSpinner();
		});										
	}
	
	var import_error = function() {
		this.dlgbox({text:this.strtable.import_invalid_format,cancel:{".hidden":true}},"confirm");		
	}.bind(this);
	
    var show_info_fn = function(ev) {
    	console.log("Mouse at: ",ev);
    }    

    	
	this.gen_backtest(form,"backtest_anchor", "backtest_vis",inputs,function(cntr){

		cntr.showSpinner();
		config = this.saveForm(form,{});
        opts = cntr.bt.readData(["initial_balance", "initial_pos","initial_price"]);
		config.broker = broker;
		config.pair_symbol = pair;
		bal = isFinite(opts.initial_balance)?opts.initial_balance:balance;
		req = {
			config: config,
			id: id,
			init_pos:isFinite(opts.initial_pos)?opts.initial_pos:undefined,
			init_price:isFinite(opts.initial_price)?opts.initial_price:init_def_price,
			balance:bal,
			fill_atprice:fill_atprice,
			start_date: start_date,
			reverse: reverse_chart,
			invert: invert_chart,
		};


		if (frst) {
			frst = false;
			cntr.bt.setItemEvent("options", "click", function() {
				this.classList.toggle("sel");
				cntr.bt.showItem("options_form", this.classList.contains("sel"));
			});
			cntr.bt.setItemEvent("initial_balance","input",cntr.update);
			cntr.bt.setItemEvent("initial_pos","input",cntr.update);
			cntr.bt.setItemEvent("initial_price","input",cntr.update);
			cntr.bt.setItemEvent("reverse_chart","change",function() {
				reverse_chart = cntr.bt.readData(["reverse_chart"]).reverse_chart;
				cntr.update();				
			});
			cntr.bt.setItemEvent("invert_chart","change",function() {
				invert_chart = cntr.bt.readData(["invert_chart"]).invert_chart;
				cntr.update();				
			});
			cntr.bt.setItemValue("show_op", show_op);
			cntr.bt.setItemValue("fill_atprice",fill_atprice);
			cntr.bt.setItemValue("reverse_chart",reverse_chart);
			cntr.bt.setItemValue("invert_chart",invert_chart);
			cntr.bt.setItemEvent("show_op","change", function() {
				show_op = cntr.bt.readData(["show_op"]).show_op;
				update();
			})
			cntr.bt.setItemEvent("start_date","input", function() {
				start_date=this.valueAsNumber;
				cntr.update();
			})
			cntr.bt.setItemEvent("fill_atprice","change", function() {
				fill_atprice = cntr.bt.readData(["fill_atprice"]).fill_atprice;
				cntr.update();
			})
			cntr.bt.setItemEvent("showpl","click",swapshowpl);
			cntr.bt.setItemEvent("shownorm","click",swapshowpl);
			cntr.bt.setItemEvent("select_file","click",function(){
				var el = cntr.bt.findElements("price_file")[0];
				el.value="";
				el.click();
			});
			cntr.bt.setItemEvent("chart1","mousemove", function(ev){show_info_fn(ev);});
			cntr.bt.setItemEvent("chart2","mousemove", function(ev){show_info_fn(ev);});
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
						if (min > trs && trs/min < 0.1) recalc_spread(prices,cntr);
						else if (min < trs && min/trs < 0.1) upload_trades(prices, cntr);
						else import_error();
					}.bind(this);
					reader.readAsText(this.files[0]);
				}
			});
			cntr.bt.setItemEvent("icon_internal","click",function(){
				recalc_spread("internal",cntr);
			})
			cntr.bt.setItemEvent("icon_recalc","click",function(){
				recalc_spread("update",cntr);
			})
			cntr.bt.setItemEvent("icon_reset","click",function(){
				fetch_json("api/backtest", {method:"DELETE"}).then(cntr.update.bind(cntr));
			})
			

			cntr.bt.setItemEvent("pos","input",function() {
				var v = -this.value;
				offset = v * offset_max/100;
				if (update) update();
			})

		}
		fetch_with_error(url, {method:"POST", body:JSON.stringify(req)}).then(function(v) {					    
			if (v.length == 0) return;
			res_data = v;
			draw(cntr,v,offset,bal);			
			update = function() {
				if (tm) clearTimeout(tm);
				tm = setTimeout(draw.bind(this,cntr,v,offset), 1);
			}
			update_recalc = draw.bind(this,cntr,v,offset,bal);
		}).then(cntr.hideSpinner,function(e){
			cntr.hideSpinner();throw e;
		});
		

        
		
	}.bind(this));
}

App.prototype.optionsForm = function() {
	var form = TemplateJS.View.fromTemplate("options_form");
	var data = {
			report_interval: defval(this.config.report_interval,864000000)/86400000,
			backtest_interval: defval(this.config.backtest_interval,864000000)/86400000,
			stop:{"!click": function() {
					this.waitScreen(fetch_with_error("api/stop",{"method":"POST"}));
					for (var x in this.traders) this.stopped[x] = true;
			}.bind(this)},
			reload_brokers:{"!click":function() {
				this.waitScreen(fetch_with_error("api/brokers/_reload",{"method":"POST"}));
			}.bind(this)}
			
	};
	form.setData(data);
	form.save = function() {
		var data = form.readData();
		this.config.report_interval = data.report_interval*86400000;
		this.config.backtest_interval = data.backtest_interval*86400000;
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
	dialogRules();
		
	function update() {		
		var traderURL = _this.traderURL(id);
		var params="";
		var data = form.readData(["order_price"]);		
		var req_intrprice =data.order_price;
		if (!isNaN(req_intrprice)) params="/"+req_intrprice;
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
								_this.cancelOrder(cfg.id, cfg.pair_symbol, x.id).
									then(update);
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
			})
			
		return f;
	}
	function cycle_update() {
		if (!form.getRoot().isConnected) return;
		setTimeout(cycle_update, 15000);
		return update();
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
				_this.dlgbox({text:_this.strtable.trade_ask_stop},"confirm")
					.then(function() {
						return _this.waitScreen(fetch_with_error(_this.traderURL(cfg.id)+"/stop",{method:"POST"}));
					}).then(function() {
						_this.stopped[id] = true;
						postOrder.call(this);
					}.bind(this));
				return;
			}

			var b = this.dataset.name;
			var d = form.readData(["edit_order","order_price","order_size"]);

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
			clearForm();
			_this.waitScreen(fetch_with_error(url, {method:"POST", body: JSON.stringify(req)}).then(update));
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

App.prototype.brokerConfig = function(id, pair) {
	var burl = this.pairURL(id, pair)+"/settings";
	var form = fetch_with_error(burl).then(formBuilder);
	return this.dlgbox({form:form}, "broker_options_dlg").then(function() {
		form.then(function(f) {
			var d = f.readData();
			return fetch_with_error(burl, {method:"PUT",body:JSON.stringify(d)})
			.then(function(a){
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
	delete cfg.report_position_offset;
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
			this.waitScreen(fetch_with_error(url,{"method":"PUT","body":data}))
			 .then(function() {
				dlg.close();
				this.undoTrader(id);
			 }.bind(this));
		}.bind(this),"ok");
	}.bind(this));
}