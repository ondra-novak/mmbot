

function adjNum(n) {
	"use strict";
	if (typeof n != "number") return n;
	if (!isFinite(n)) { 
		if (s < 0) return "-∞";
		else return "∞";
	}
	var an = Math.abs(n);
	if (an > 9999) return n.toFixed(0);
	else if (an >= 1) return n.toFixed(2);
	else if (an > 0.0001) return n.toFixed(6);
	else {
		var s = (n*1000000).toFixed(3);
		if (s == "0.000") return s;
		return s+"µ";
	}
}


function app_start() {
	
	"use strict";
	
	var rangeDb= new WeakMap();
	var markets = {};
	var selectors = {};
	var marketInfo = new WeakMap();
	
	function fetchJSON(url) {
		return fetch(url).then(function(resp) {
			return resp.json();
		});
	}

	function initRanges(topElement) {

		var ranges = topElement.querySelectorAll(".range input");
		
		function updateRanges(nd) {
			
				var p = nd.parentNode;
				var n = rangeDb.get(p);
				if (!n) {
					n = document.createElement("input");
					n.setAttribute("type","range");
					n.classList.add("range");
					n.addEventListener("input", function() {
						nd.value = this.value;
					});
					p.appendChild(n);
					rangeDb.set(p,n);
				}
				n.value = nd.value;
		}
		Array.prototype.forEach.call(ranges, function(nd) {
			nd.addEventListener("input", updateRanges.bind(null,nd));
			updateRanges(nd);
		});
	}

	var mtemp = document.getElementById("market_template");
	var stemp = document.getElementById("selector_template");
	var mstop = document.getElementById("create_new");
	var mlist = document.getElementById("market_list");
	var selector = document.getElementById("selector_list");
	var symbols = fetchJSON("all_pairs");
	
	function loadSymbols(element, broker, symb) {
		var opval = broker+":"+symb;
		while (element.firstChild) element.removeChild(element.firstChild);
		symbols.then(function(ss){
			for (var b in ss) {
				var symbs = ss[b];
				var optgrp = document.createElement("optgroup");
				optgrp.setAttribute("label",b);
				element.appendChild(optgrp);
				symbs.forEach(function(n) {
					var x = b+":"+n;
					var opt = document.createElement("option");
					opt.setAttribute("value", x);
					opt.innerText = n;
					optgrp.appendChild(opt);
				});
			}
			element.value = opval;
		})
	}
	
	function unpackSymbol(s) {
		var n = s.indexOf(":");
		return {
			broker: s.substr(0,n),
			pair_symbol: s.substr(n+1)
		}
	}
	
	function setStaticFields(element, fields) {
		for (var name in fields) {
			var f = element.querySelectorAll("[data-name="+name+"]");
			Array.prototype.forEach.call(f,function(x) {
				x.innerText = adjNum(fields[name]);			
			});
		}
	} 
	
	
	
	function updateSymbolInfo(element, broker, symb) {
		fetchJSON("info?broker="+encodeURIComponent(broker)+"&symbol="+encodeURIComponent(symb))
			.then(function(info) {
				setStaticFields(element, {
					"info_price":adjNum(info.price)+" "+ info.currency_symbol,
					"info_asset":adjNum(info.asset_balance)+" "+ info.asset_symbol,
					"info_currency":adjNum(info.currency_balance)+" "+ info.currency_symbol,
					"info_fees":info.fees*100,
					"info_leverage":info.leverage+"x"
				});
			});
	}
	
	function getBool(x) {
		return x == "1" || x == "yes" || x == "on" || x == "true";
	}

	function createMarket(name, config, enabled) {
		var nw;
		var init = false;
		if (markets[name]) {
			nw = markets[name];
		} else {
			nw = document.importNode(mtemp.content,true).firstElementChild;
			markets[name] = nw;
			init = true;
		}

		var form = nw.querySelector("form");
		form.title.value = config.title;
		updateSymbolInfo(nw, config.broker, config.pair_symbol);
		
		form.external_assets.value = config.external_assets || 0;
		form.spread_calc_hours.value = config.spread_calc_hours || (24*5);
		form.spread_calc_min_trades.value = config.spread_calc_min_trades || 8;
		form.spread_calc_max_trades.value = config.spread_calc_max_trades || 24;
		form.acum_factor_buy.value = config.acum_factor_buy!=undefined?(config.acum_factor_buy*100):50;
		form.acum_factor_sell.value = config.acum_factor_sell!=undefined?(config.acum_factor_sell*100):50;
		form.dynmult_raise.value= config.dynmult_raise || 200;
		form.dynmult_fall.value = config.dynmult_fall || 1;
		
		form.enabled.checked = enabled;
		form.dryrun.checked = getBool(config.dry_run);
		form.ibal.checked = getBool(config.internal_balance);
		form.mantrade.checked = getBool(config.detect_manual_trades);
		setStaticFields(nw, {"title":config.title});
		setStaticFields(nw, {"broker":config.broker, "pair_symbol":config.pair_symbol});
		form.title.addEventListener("input",function() {
			setStaticFields(nw, {"title":this.value});
		});
		
		initRanges(nw);
		marketInfo.set(nw, {
			id: name,
			config: config,
			mode: enabled?"enabled":"disabled"
			})
		
		return nw;
	}
	
	
	function update_selector(id, config, mode) {
		var row = selectors[id];
		if (config !== undefined) 
			setStaticFields(row, config);
		if (mode !== undefined) { 
			row.classList.toggle("disabled",mode == "disabled");
			row.classList.toggle("deleted",mode == "deleted");
		}
		
	}
	
	function create_selectors(lst) {
		selector_list.innertText = "";
		lst.forEach(function(el) {
			var info = marketInfo.get(el);
			var row = document.importNode(stemp.content,true).firstElementChild;
			selector_list.appendChild(row);
			selectors[info.id] = row;
			update_selector(info.id, info.config, info.mode);
		})
	}
	
	function loadConfig() {
		fetch("config").then(function(resp) {return resp.json();})
					   .then(function(data){
						   
						   var mlist = [];
						   
						   var lst = data.traders.list.split(" ");
						   lst.forEach(function(name) {
							   mlist.push(createMarket(name, data[name], true));
						   });
						   var disable_list = data.traders.disabled;
						   if (disable_list) {
							   var lst = disable_list.split(" ");
							   lst.forEach(function(name) {
								   mlist.push(createMarket(name, data[name], false));
							   });
							   
						   }
						   
						   mlist.push(mstop);
						   
						   create_selectors(mlist);
					   });
	}

	marketInfo.set(mstop, {"title":mstop.dataset.title});
	mstop.parentElement.removeChild(mstop);

	
	loadConfig();
	
	var cform = mstop.querySelector("form");
	loadSymbols(cform.symbol);
	cform.create.addEventListener("click", function() {
		var s = unpackSymbol(cform.symbol.value);
		s.title = s.pair_symbol;
		var x = createMarket(cform.symbol.value,s,true);		
		x.scrollIntoView();
	})
	
	
	
}