"use strict";

function app_start() {
	window.app = new App();
	window.app.init();
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

App.prototype.createTraderList = function() {
	var form = TemplateJS.View.fromTemplate("trader_list");
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
		if (id != "+") {
			nf = this.openTraderForm(id);			
			var f = this.curForm;
			this.curForm = nf; 
			var p;
		
			if (f) p = f.close(); else p = Promise.resolve();
			p.then(function() {
				this.desktop.setItemValue("content", nf);
			}.bind(this));
		} else {
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
				t.fillForm = this.fillForm.bind(this, t);
				var f2 = this.createTraderList();
				form.replace(f2);
				f2.select(name);
			}.bind(this))
		}
	}.bind(this);
	
	return form;
}

App.prototype.loadConfig = function() {
	return fetch_json("api/config").then(function(x) {
		this.config = x;
		var lst = x.traders.list.split(" ");		
		this.traders = lst.reduce(function(a, n) {
			var t = x[n];
			t.fillForm = this.fillForm.bind(this, t);
			t.id = n
			a[n] = x[n];
			return a;
		}.bind(this),{});		
		return x;
	}.bind(this))
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

function defval(v,w) {
	if (v === undefined) return w;
	else return v;
}

App.prototype.fillForm = function (src, trg) {
	var data = {};	
	var broker = fetch_json(this.brokerURL(src.broker));
	var pair = fetch_json(this.pairURL(src.broker, src.pair_symbol));
	data.id = src.id;
	data.title = src.title;
	data.symbol = src.pair_symbol;
	data.broker = broker.then(function(x) {return x.exchangeName;});
	data.broker_id = broker.then(function(x) {return x.name;});
	data.broker_ver = broker.then(function(x) {return x.version;});
	data.asset = pair.then(function(x) {return x.asset_symbol;})
	data.currency = pair.then(function(x) {return x.currency_symbol;})
	data.balance_asset= pair.then(function(x) {return adjNum(x.asset_balance);})
	data.balance_currency = pair.then(function(x) {return adjNum(x.currency_balance);})
	data.price= pair.then(function(x) {return adjNum(x.price);})
	data.leverage=pair.then(function(x) {return x.leverage?x.leverage+"x":"n/a";});
	data.broker_img = this.brokerImgURL(src.broker);
	data.goal = pair.then(function(x) {
		var pl = x.leverage || src.force_margin || src.neutral_pos;
		var dissw = !!x.leverage;
		return {
			"value":pl?"pl":"norm",
			".disabled": dissw 
		};
	})
	data.enabled = src.enabled != "off"
	data.dry_run = src.dry_run == "on"
	data.external_assets = src.external_assets;
	data.acum_factor =defval(src.acum_factor,0);
	data.accept_loss = defval(src.accept_loss,0);
	data.power = src.external_assets;
	var neutral_pos = src.neutral_pos?src.neutral_pos.split(" "):[];
	data.nautral_pos_type = neutral_pos.length == 1?"assets":neutral_pos[0];
	data.nautral_pos_val = neutral_pos.length == 1?neutral_pos[0]:neutral_pos[1];
	data.max_pos = defval(src.max_pos,0);
	data.sliding_pos_hours = defval(src.sliding_pos_hours,240);
	data.sliding_pos_weaken = defval(src.sliding_pos_weaken,11);
	data.spread_calc_hours = defval(src.spread_calc_hours,5*24);
	data.spread_calc_min_trades = defval(src.spread_calc_min_trades,1);
	data.dynmult_raise = defval(src.dynmult_raise,250);
	data.dynmult_fall = defval(src.dynmult_fall, 0.5);
	data.dynmult_mode = src.dynmult_mode || "half_alternate";
	data.order_mult = defval(src.buy_mult,1);
	data.min_size = defval(src.min_size,0);
	data.internal_balance = src.internal_balance == "on";
	data.dust_orders = src.internal_balance != "off";
	data.detect_manual_trades = src.detect_manual_trades== "on";
	data.report_position_offset = src.report_position_offset;
	data.force_spread = src.force_spread;
	
	
	
	trg.setData(data).then(trg.dlgRules.bind(trg));
}

App.prototype.openTraderForm = function(trader) {
	var form = this.createTraderForm();
	this.traders[trader].fillForm(form);
	return form;
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
				});
				number.addEventListener("change", function() {
					var v = parseFloat(this.value);
					var val = v / mult;
					range.value = val;
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
			if (number) return parseFloat(number.value);
			else return 0;
			
		},
		function(elem,attrs) {
			
		}
));

App.prototype.init = function() {
	return this.loadConfig().then(function() {
		this.desktop = TemplateJS.View.fromTemplate("desktop");
		var menu = this.createTraderList();
		this.desktop.setItemValue("menu", menu);
		this.desktop.open();
	}.bind(this));
}

App.prototype.brokerSelect = function() {
	var _this = this;
	return new Promise(function(ok, cancel) {
		
		var form = TemplateJS.View.fromTemplate("broker_select");
		form.openModal();
		fetch_json("api/brokers").then(function(x) {
			var lst = x["entries"].map(function(itm) {
				var broker = fetch_json(_this.brokerURL(itm));
				return {
					image:_this.brokerImgURL(itm),
					caption: broker.then(function(b){return b.exchangeName;}),
					"":{
						"!click": function() {
							form.close();
							ok(itm);
						}
					}
				};
			});
			form.setItemValue("item",lst);
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
		fetch_json(_this.pairURL(broker,"")).then(function(data) {
			var pairs = [{"":{"value":"----",".value":""}}].concat(data["entries"].map(function(x) {
				return {"":{"value":x,".value":x}};
			}));			
			form.setItemValue("item",pairs);
			dlgRules();
		});
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
