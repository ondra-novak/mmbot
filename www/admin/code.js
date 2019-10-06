"use strict";

function app_start() {
	TemplateJS.View.lightbox_class = "lightbox";
	window.app = new App();
	window.app.init();
	
}

function fetch_error(e) {
	if (!fetch_error.shown) {
		fetch_error.shown = true;
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
		txt.then(function(t) {
			var msg = (e.status || "")+" "+(e.statusText || "");
			if (t == msg) t = "";
			app.dlgbox({text:msg, desc:t},"network_error").then(function() {
				fetch_error.shown = false;
			});
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
		var state = this.readData(["goal","advanced"]);
		this.showWithAnim("goal_norm",state.goal == "norm");
		this.showWithAnim("goal_pl",state.goal == "pl");
		form.getRoot().classList.toggle("no_adv", !state["advanced"]);
		
	};
	form.setItemEvent("goal","change", form.dlgRules.bind(form));
	form.setItemEvent("advanced","change", form.dlgRules.bind(form));
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

function defval(v,w) {
	if (v === undefined) return w;
	else return v;
}

function filledval(v,w) {
	if (v === undefined) return w;
	else if (v == w) return v;
	else {
		return {
			"value":v,
			"classList":{
				"changed":true
			}
		}
	}
}



function powerToEA(power, pair) {
		var total; 
		if (pair.leverage) {
			total = pair.currency_balance / pair.price;
			return power * total;
		} else {
			total = pair.currency_balance / pair.price + pair.asset_balance;
			return power * total;
		}
}


function calc_range(a, ea, c, p, inv, leverage) {
	a = a+ea;
	var value = a * p;
	var max_price = pow2((a * Math.sqrt(p))/ea);
	var S = value - c
	var min_price = S<=0?0:pow2(S/(a*Math.sqrt(p)));
	if (leverage) {
		var colateral = c* (1 - 1 / leverage); 
		min_price = (ea*p - 2*Math.sqrt(ea*colateral*p) + colateral)/ea;
		max_price = (ea*p + 2*Math.sqrt(ea*colateral*p) + colateral)/ea;

	}
	if (!isFinite(max_price)) max_price = "∞";
	if (inv) {
		var k = 1/min_price;
		min_price = 1/max_price;
		max_price =k;
	}
	return {
		range_min_price: adjNumN(min_price),
		range_max_price: adjNumN(max_price)
	}

	
}

App.prototype.fillForm = function (src, trg) {
	var data = {};	
	data.id = src.id;
	data.title = src.title;
	data.symbol = src.pair_symbol;
	var broker = fetch_with_error(this.brokerURL(src.broker));
	var pair;
	var orders;

	var updateHdr = function(data) {
		
		pair = fetch_json(this.pairURL(src.broker, src.pair_symbol));
		orders = fetch_json(this.pairURL(src.broker, src.pair_symbol)+"/orders");

		data.broker = broker.then(function(x) {return x.exchangeName;});
		data.no_api_key = broker.then(function(x) {return {".hidden": x.trading_enabled};});
		data.broker_id = broker.then(function(x) {return x.name;});
		data.broker_ver = broker.then(function(x) {return x.version;});
		data.asset = pair.then(function(x) {return x.asset_symbol;})
		data.currency = pair.then(function(x) {return x.currency_symbol;})
		data.balance_asset= pair.then(function(x) {return adjNum(invSize(x.asset_balance,x.invert_price));})
		data.balance_currency = pair.then(function(x) {return adjNum(x.currency_balance);})
		data.price= pair.then(function(x) {return adjNum(invPrice(x.price,x.invert_price));})
		data.leverage=pair.then(function(x) {return x.leverage?x.leverage+"x":"n/a";});
		data.orders = Promise.all([orders,pair]).then(function(x) {
			var orders = x[0];
			var pair = x[1];
			var mp = orders.map(function(z) {
				return {
					id: z.id,
					dir: invSize(z.size,pair.invert_price)>0?"BUY":"SELL",
					price:adjNum(invPrice(z.price, pair.invert_price)),
					size: adjNum(Math.abs(z.size))
				};});
			if (mp.length) {
				var butt = document.createElement("button");
				butt.innerText="X";
				mp[0].action = {
						"rowspan":mp.length,
						"value": butt
				};
				butt.addEventListener("click", this.cancelAllOrders.bind(this, src.id));
			}
			return mp;
		}.bind(this),function(){});
	}.bind(this);
	updateHdr(data);
	data.broker_img = this.brokerImgURL(src.broker);
	data.advanced = src.advanced;

	
	data.goal = pair.then(function(x) {
		var pl = x.leverage || src.force_margin || src.neutral_pos;
		var dissw = !!x.leverage;
		return {
			"value":pl?"pl":"norm",
			".disabled": dissw 
		};
	})	
	data.enabled = src.enable;
	data.dry_run = src.dry_run;
	data.external_assets = defval(src.external_assets,0);
	trg.setItemEvent("external_assets","!input",updateRange);
	data.acum_factor =filledval(src.acum_factor,0);
	data.accept_loss = filledval(src.accept_loss,0);
	data.power = src.power;
	var neutral_pos = src.neutral_pos?src.neutral_pos.split(" "):[];
	data.neutral_pos_type = filledval(neutral_pos.length == 1?"assets":neutral_pos[0],"assets");
	data.neutral_pos_val = filledval(src.neutral_pos?(neutral_pos.length == 1?neutral_pos[0]:neutral_pos[1]):0,1)
	data.max_pos = filledval(src.max_pos,0);
	data.sliding_pos_hours = filledval(src["sliding_pos.hours"],240);
	data.sliding_pos_fade = filledval(src["sliding_pos.fade"],0);
	data.spread_calc_hours = filledval(src.spread_calc_hours,5*24);
	data.spread_calc_min_trades = filledval(src.spread_calc_min_trades,4);
	data.dynmult_raise = filledval(src.dynmult_raise,200);
	data.dynmult_fall = filledval(src.dynmult_fall, 5);
	data.dynmult_mode = filledval(src.dynmult_mode, "half_alternate");
	data.order_mult = filledval(defval(src.buy_mult,1)*100,100);
	data.min_size = filledval(src.min_size,0);
	data.max_size = filledval(src.max_size,0);
	data.internal_balance = filledval(src.internal_balance,0);
	data.dust_orders = filledval(src.dust_orders,true);
	data.detect_manual_trades = filledval(src.detect_manual_trades,false);
	data.report_position_offset = filledval(src.report_position_offset,0);
	data.init_backtest = {"!click":this.init_backtest.bind(this,trg,src.id, pair)};
	data.force_spread = filledval(adjNum((Math.exp(defval(src.force_spread,0))-1)*100),"0.000");
	data.expected_trend=filledval(src.expected_trend,"");
	data.auto_max_size=filledval(src.auto_max_size,true);
	data.open_orders_sect = orders.then(function(x) {return {".hidden":!x.length};},function() {return{".hidden":true};});	

	function updateEA() {	
		var d = trg.readData(["power","goal","advanced"]);
		if (d.goal == "pl") {
			pair.then(function(p) {
				d.external_assets = adjNum(powerToEA(d.power, p));
				d.external_volume = adjNum(d.external_assets * p.price);
				trg.setData(d);
				updateRange();
			});
		} else if (!d.advanced) {
			trg.setData(d);
			updateRange();
		}
	}
	
	function updateRange() {
		pair.then(function(p) {
			var d = trg.readData(["external_assets"]);
			var r = calc_range(p.asset_balance, parseFloat(d.external_assets), p.currency_balance, p.price, p.invert_price, p.leverage);
			trg.setData(r);
		});
		
	}
	data.power ={"!change": updateEA};		
	
	Promise.all([pair,data.goal]).then(function(pp) {
		var p = pp[0];
		var goal = pp[1].value;
		var data = {};
		var l = p.leverage || 1;
		if (!src.power) {
			if (src.external_assets) {
				var part = powerToEA(1,p);
				data.power = src.external_assets/part; 
			} else if (goal == "pl") {
				data.power = 20;
				data.neutral_pos_val = 0;
				data.neutral_pos_type = "assets";
			} else if (goal == "norm") {
				data.power = 1;
				data.neutral_pos_val = 1;
				data.neutral_pos_type = "center";
			}
		}
		trg.setData(data);
		trg.p = p;
		updateEA();
	})	
	
	data.icon_repair={"!click": this.repairTrader.bind(this, src.id)};
	data.icon_reset={"!click": this.resetTrader.bind(this, src.id)};
	data.icon_delete={"!click": this.deleteTrader.bind(this, src.id)};
	data.icon_undo={"!click": this.undoTrader.bind(this, src.id)};
	data.icon_trading={"!click":this.tradingForm.bind(this, src.id)};
	
	function refresh_hdr() {
		if (trg.getRoot().isConnected) {
			var data = {};
			updateHdr(data);
			trg.setData(data);
			setTimeout(refresh_hdr,60000);
		}
	}

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

		setTimeout(refresh_hdr,60000);

		return x;
	}


	return trg.setData(data).catch(function(){}).then(unhide_changed).then(trg.dlgRules.bind(trg));
}


App.prototype.saveForm = function(form, src) {

	var data = form.readData();
	var trader = {}
	var goal = data.goal;
	trader.id = src.id;
	trader.broker =src.broker;
	trader.pair_symbol = src.pair_symbol;
	trader.title = data.title;
	trader.enable = data.enabled;
	trader.dry_run = data.dry_run;
	trader.advanced = data.advanced;
	trader.accept_loss = data.accept_loss;
	if (goal == "norm") {
		trader.acum_factor = data.acum_factor;
		trader.force_margin=false;
	} else {
		trader.neutral_pos = data.neutral_pos_type+" "+data.neutral_pos_val;
		trader.max_pos = data.max_pos;
		trader["sliding_pos.hours"] = data.sliding_pos_hours;
		trader["sliding_pos.fade"] = data.sliding_pos_fade;
		trader.force_margin=true;
	}
	trader.external_assets = data.external_assets;
	trader.spread_calc_hours =data.spread_calc_hours;
	trader.spread_calc_min_trades = data.spread_calc_min_trades;
	trader.dynmult_raise = data.dynmult_raise;
	trader.dynmult_fall = data.dynmult_fall;
	trader.dynmult_mode = data.dynmult_mode;
	trader.buy_mult = data.order_mult/100;
	trader.sell_mult = data.order_mult/100;
	trader.min_size = data.min_size;
	trader.max_size = data.max_size;
	trader.internal_balance = data.internal_balance;
	trader.dust_orders = data.dust_orders;
	trader.detect_manual_trades = data.detect_manual_trades;
	trader.report_position_offset = data.report_position_offset;
	trader.force_spread = Math.log(data.force_spread/100+1);	
	trader.expected_trend = data.expected_trend;
	trader.auto_max_size = data.auto_max_size;
	return trader;
	
}

App.prototype.openTraderForm = function(trader) {
	var form = this.createTraderForm();
	var p = this.fillForm(this.traders[trader], form);	
	return p.then(function() {return form;});
}

TemplateJS.View.regCustomElement("X-SLIDER", new TemplateJS.CustomElement(
		function(elem,val) {
			var range = elem.querySelector("input[type=range]");
			var number = elem.querySelector("input[type=number]");
			var mult = parseFloat(elem.dataset.mult);
			var fixed = parseInt(elem.dataset.fixed)
			var toFixed = function(v) {
				if (!isNaN(fixed)) return parseFloat(v).toFixed(fixed);
				else return v;
			}
			if (!range) {
				range = document.createElement("input");
				range.setAttribute("type","range");
				number = document.createElement("input");
				number.setAttribute("type","number");
				number.setAttribute("step",mult);
				var env1 = document.createElement("div");
				var env2 = document.createElement("div");
				var min = parseFloat(elem.dataset.min);
				var max = parseFloat(elem.dataset.max);
				var rmin = Math.floor(min/mult);
				var rmax = Math.floor(max/mult);
				range.setAttribute("min",rmin);
				range.setAttribute("max",rmax);
				range.addEventListener("input",function() {
					var v = parseInt(this.value);
					var val = v * mult;
					number.value = toFixed(val);
					elem.dispatchEvent(new Event("change"));
				});
				number.addEventListener("change", function() {
					var v = parseFloat(this.value);
					var val = v / mult;
					range.value = val;
					elem.dispatchEvent(new Event("change"));
				});				
				env1.appendChild(range);
				env2.appendChild(number);
				elem.appendChild(env1);
				elem.appendChild(env2);
			}
			range.value = val / mult;
			number.value = toFixed(val);

			
		},
		function(elem) {
			var number = elem.querySelector("input[type=number]");
			if (number) return parseFloat(number.valueAsNumber);
			else return 0;
			
		},
		function(elem,attrs) {
			
		}
));

App.prototype.init = function() {
	this.strtable = document.getElementById("strtable").dataset;
	this.desktop = TemplateJS.View.createPageRoot(true);
	this.desktop.loadTemplate("desktop");
	var top_panel = TemplateJS.View.fromTemplate("top_panel");
	this.top_panel = top_panel;
	this.desktop.setItemValue("top_panel", top_panel);
	top_panel.setItemEvent("save","click", this.save.bind(this));
	top_panel.setItemEvent("login","click", function() {
		location.reload();
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
		form.openModal();
		fetch_with_error("api/brokers").then(function(x) {
			var excl_info = [];
			var lst = x["entries"].map(function(itm) {
				var z = fetch_with_error(_this.brokerURL(itm))
					.then(function(z) {
						return {
								".hidden":z.trading_enabled
								};
					});
				excl_info.push(z);
				return {
					excl_info: z,
					image:_this.brokerImgURL(itm),
					caption: itm,
					"":{
						"!click": function() {
							form.close();
							ok(itm);
						}
					}
				};
			});
			excl_info = Promise.all(excl_info)
				.then(function(lst) {
					return {
						".hidden": lst.reduce(function(a,b) {
						return a && b[".hidden"];
						},true)};				
				});
			form.setData({
				"item":lst,
				"excl_info": excl_info
			});
		});
		form.setCancelAction(function() {
			form.close();
			cancel();
		},"cancel");
	
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
			this.pairURL(tr.broker, tr.pair_symbol)+"/orders",
			{method:"DELETE"});
		var dr = fetch_json(
			this.traderURL(tr.id)+"/stop",
			{method:"POST"}).catch(function() {});


		return this.waitScreen(Promise.all([cr,dr]))
			.catch(function(){})
			.then(function() {
				this.stopped[id] = true;
				return fetch_with_error(this.pairURL(tr.broker, tr.pair_symbol), {cache: 'reload'});
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

App.prototype.repairTrader = function(id) {
		this.dlgbox({"text":this.strtable.askrepair,
		"ok":this.strtable.yes,
		"cancel":this.strtable.no},"confirm").then(function(){

		var tr = this.traders[id];
		this.waitScreen(fetch_with_error(
			this.traderURL(tr.id)+"/repair",
			{method:"POST"})).then(function() {
						this.updateTopMenu(tr.id);				
			}.bind(this));
	}.bind(this));
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
		
		var data = {
			rows:rows,
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
			guest_role:{
				"!change":function() {
					this.config.guest = form.readData(["guest_role"]).guest_role == "viewer";
				}.bind(this),
				"value": !this.config.guest?"none":"viewer"
			}
		};
		
		form.setData(data);
		
	}
	update.call(this);
	
	return form;
}

App.prototype.dlgbox = function(data, template) {
	return new Promise(function(ok, cancel) {
		var dlg = TemplateJS.View.fromTemplate(template);
		dlg.openModal();
		dlg.setData(data);
		dlg.setCancelAction(function() {
			dlg.close();cancel();
		},"cancel");
		dlg.setDefaultAction(function() {
			dlg.close();ok();
		},"ok");
	});		
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
			this.updateTopMenu
			this.stopped = {};
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
	fetch("api/logout").then(function(resp) {
		if (resp.status == 200) {
			location.reload();
		}
	});
}

App.prototype.validate = function(cfg) {
	return new Promise(function(ok, error) {
		var admin = cfg.users.find(function(x) {
			return x.admin;
		})
		if (!admin) return error({
				"trader":"!",
				"message":this.strtable.need_admin
			});	
		ok(cfg); 
	}.bind(this));
}




App.prototype.init_backtest = function(form, id, pair) {
	var url = this.traderURL(id)+"/trades";
	var el = form.findElements("backtest_anchor")[0];
	var bt = TemplateJS.View.fromTemplate("backtest_res");
	el.parentNode.insertBefore(bt.getRoot(),el.nextSibling);
	form.enableItem("init_backtest",false);		
	bt.getRoot().scrollIntoView(false);
	Promise.all([fetch_with_error(url),pair]).then(function(v) {
		var trades=v[0];
		var pair = v[1];
		if (trades.length == 0) {
			bt.close();
			this.dlgbox({text:this.strtable.backtest_nodata,cancel:{hidden:"hidden"}},"confirm");
			return;
		}
		if (trades.length < 100) {
			this.dlgbox({text:this.strtable.backtest_lesstrades,cancel:{hidden:"hidden"}},"confirm");
		}
		
		var elems = ["external_assets","power", 
			"sliding_pos_hours", "sliding_pos_fade",
			"order_mult","min_size","max_size","dust_orders","expected_trend","goal"];

		var chart1 = bt.findElements('chart1')[0];
		var chart2 = bt.findElements('chart2')[0];
		//var chart3 = bt.findElements('chart3')[0];
		var xdata = {
			"date_from":{
				"value":new Date(trades[0].time),
				"!change":recalc
			},
			"date_to":{
				"value":new Date(trades[trades.length-1].time+86400000),
				"!change":recalc
			},
			"apply_from":{
				"value":new Date(trades[0].time),
				"!change":recalc
			},
			"start_pos":{
					"value":0,
					"!input":recalc
			}
		};
		bt.setData(xdata);
		var chart3 = bt.findElements('chart3')[0];
		
		var tm;
				
		var prevChart;
		
		function recalc() {
			if (tm) clearTimeout(tm);
			tm = setTimeout(function() {
				var data = form.readData(elems);
				var xdata = bt.readData();
			

				var min_size = parseFloat(data.min_size);
				if (pair.min_size > data.min_size) min_size = pair.min_size;
				if (data.dust_orders == false) min_size = 0;

				var h =  parseFloat(data.sliding_pos_hours);
				var w =  parseFloat(data.sliding_pos_fade);
				var g = data.goal;
				if (g == "norm") {
					w = 0;
					h = 0;
				}
				var et =  parseFloat(data.expected_trend)*0.01;
				if (pair.invert_price) {
					et = -et;
				}
				
				var trades2 = trades.filter(function(x) {
					var d = new Date(x.time);
					return d>=xdata.apply_from;
				});
				
				var btdata = {
						data:trades2,
						external_assets:parseFloat(data.external_assets),
						sliding_pos:h,
						fade:w,
						multiplicator:parseFloat(data.order_mult)*0.01,
						min_order_size:min_size,
						invert:false,
						expected_trend:et,
						start_pos:parseFloat(xdata.start_pos),
						max_order_size:parseFloat(data.max_size),
						step: pair.min_size
						};
				
				var res = calculateBacktest(btdata);				

				if (pair.invert_price) {
					res.chart.forEach(function(x){
						x.price = 1/x.price;
						x.np = 1/x.np;
						x.achg = -x.achg;
					});
				}

				var chartData = res.chart.filter(function(x){
					var d = new Date(x.time);
					return d >=xdata.date_from && d<=xdata.date_to;
				});
				var interval = chartData.length?chartData[chartData.length-1].time - chartData[0].time:1;
				var drawChart = initChart(interval,5,700000);

			
				bt.setData({
					bt_pl: adjNum(res.pl),
					bt_pln: adjNum(res.pln),
					bt_dd: adjNum(res.mdd),
					bt_mp: adjNum(res.maxpos),
				})

				drawChart(chart1,chartData,"pl",[],"pln");
				drawChart(chart2,chartData,"price",[],"np");
				drawChart(chart3,chartData,"achg",[]);
				tm = setTimeout(function() {
					form.setItemValue("spinner", TemplateJS.View.fromTemplate({"tag":"div","attrs":{"class":"spinner inline fast"}}));			
					btdata.max_order_size = 0;
					var res = calculateBacktest(btdata);
					var max_size = res.chart.reduce(function(a,b) {
						var sz = Math.abs(b.achg);
						return a < sz?sz:a;
					},0);
					
					var actions = [];
					var vmin = pair.min_size;
					var vmax = max_size;
					var best = -1e90;
					var level = 10;

					function createActions(f, t) {
						for (var i = 0; i < 50; i++) {
							var st = (t - f)/20;
							var z = f + i * st;
							actions.push(function(z) {
								btdata.max_order_size = z;
								var r = calculateBacktest(btdata);
								if (r.pln > best) {
									best = r.pln;
									vmin = z;
									vmax = z+st;
								}							
							}.bind(null,z));
						}
						actions.push(function() {
							if (level) {
								level--;
								best = -1e90;
								createActions(vmin,vmax);
							} else {
								btdata.max_order_size = (vmin+vmax)/2;
								res = calculateBacktest(btdata);
								form.setItemValue("optimal_max", adjNumN(btdata.max_order_size));			
								form.setItemValue("optimal_pl", adjNumN(res.pl));
								form.setItemValue("optimal_pln", adjNumN(res.pln));
								form.setItemValue("spinner","");
							}
						})
					}

					createActions(vmin,vmax);
					function cycle() {
						if (actions.length) {
							var f = actions.shift();
							f();
							tm = setTimeout(cycle,1);
						}
					}
					tm = setTimeout(cycle,1);
				}.bind(this),1)
			}.bind(this), 1);
		}
		
		recalc.call(this);
		elems.forEach(function(x) {
			var el = form.findElements(x)[0];
			el.addEventListener("input", recalc.bind(this));
		},this);		


		
	}.bind(this),function() {
			bt.close();
	});
}

App.prototype.optionsForm = function() {
	var form = TemplateJS.View.fromTemplate("options_form");
	var data = {
			report_interval: defval(this.config.report_interval,864000000)/86400000,
			stop:{"!click": function() {
				this.dlgbox({text:this.strtable.global_stop_confirm},"confirm")
					.then(function(){
						this.waitScreen(fetch_with_error("api/stop",{"method":"POST"}));
						for (var x in this.traders) this.stopped[x] = true;
					}.bind(this))
			}.bind(this)}
	};
	form.setData(data);
	form.save = function() {
		var data = form.readData();
		this.config.report_interval = data.report_interval*86400000;
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
		form.enableItem("button_sellask",q);
	} 
	
	form.setItemEvent("order_price","input",dialogRules);
	form.setItemEvent("order_size","input",dialogRules);
	form.setItemEvent("edit_order","change",dialogRules);
	dialogRules();
	
	var pair = fetch_with_error(this.pairURL(cfg.broker,cfg.pair_symbol));
	var limit = 8*60;
	function update() {		
		var traderURL = _this.traderURL(id);
		var pairURL = _this.pairURL(cfg.broker,cfg.pair_symbol);
		Promise.all([pair,
			fetch_with_error(traderURL+"/chart?limit="+limit),
			fetch_with_error(traderURL+"/trades"),
			fetch_json(pairURL+"/orders?reset=1",{cache: 'reload'}),
			fetch_json(pairURL+"/ticker?reset=1",{cache: 'reload'})])
			.then(function(rs) {
				var pair = rs[0];
				var chartData = rs[1];
				var trades = rs[2];
				var orders = rs[3];
				var ticker = rs[4];
				var invp = function(x) {return pair.invert_price?1/x:x;}
				var invs = function(x) {return pair.invert_price?-x:x;}				
				var now = Date.now();
				var skip = true;
				chartData.push(ticker);
				
				var drawChart = initChart(limit*60000);
				var data = mergeArrays(chartData, trades, function(a,b) {
					return a.time - b.time
				},function(n, k, f, t) {
					var p1;
					var p2;
					var achg;
					if (n == 0) {
						if (f != null && t != null) {
							p2 = invp(interpolate(f.time, t.time, k.time, f.price, t.price));
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
								_this.cancelOrder(cfg.broker, cfg.pair_symbol, x.id).
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
			}).catch(fetch_error);
			
		return pair;
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
	
	
	cycle_update().then(function(pair) {	

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
			var url = _this.pairURL(cfg.broker, cfg.pair_symbol)+"/orders";
			var req = {
					size: size,
					price: price,
					replaceId: id,
					replaceSize: 0
			};
			clearForm();
			_this.waitScreen(fetch_with_error(url, {method:"POST", body: JSON.stringify(req)}).then(function(x) {
				return fetch_with_error(url+"?reset=1",{cache:"reload"}).then(function() {return x;});
			}).then(update));
		}

		form.setItemEvent("button_buy","click", postOrder);
		form.setItemEvent("button_sell", "click",postOrder);
		form.setItemEvent("button_buybid", "click",postOrder);
		form.setItemEvent("button_sellask", "click",postOrder);
	});
} 

App.prototype.cancelOrder = function(broker, pair, id) {
	var url = this.pairURL(broker, pair)+"/orders"
	var req = {
			size:0,
			price:0,
			replaceId: id,
			replaceSize: 0,			
	};
	return fetch_with_error(url,{method:"POST",body: JSON.stringify(req)}).
		then(function(x) {
			return fetch_with_error(url+"?reset=1",{cache:"reload"}).then(function() {return x;});
		});
}

