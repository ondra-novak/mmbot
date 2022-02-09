"use strict";

window.addEventListener('DOMContentLoaded', (event) => {
	   app_start();
	});


var changeinterval=null;
var source_data=null;
var drawChart=null;
var svcreg = Promise.resolve(null);
var notifyTradesFn = null;

function app_start(){

	"use strict";

	
	if (navigator.serviceWorker) {
		svcreg=navigator.serviceWorker.register('sw.js', {	
			scope: '.'
		});
	}
	
	var lastField="";	
	var donation_repeat = (10*24*60*60*1000);
	var last_ntf_time=Date.now();
	var chart_padding = document.createElement("div");
	var last_rev = [0,0]
	var mmbot_time = 0;
	
	try {
		lastField = localStorage["markettrader_lastfield"];
		last_rev = JSON.parse(localStorage["last_rev"] || "[0,0]");
		mmbot_time = localStorage["mmbot_time"];
		if (localStorage["mmbot_skin"] == "day") {
			document.body.classList.toggle("daymode");
			updateThemeColor();
		}
	} catch (e) {

	}
	
	var secondary_charts = {price:"p0",pl:"rpl"};
	
	
	

	var chartlist = {};
	var indicator = document.getElementById("indicator");
	var eventMap = new WeakMap();
		
	var redraw = function() {};

	var selector = document.getElementById("selchart");
	var home = document.getElementById("home");
	var opencloselog = document.getElementById("logfileactivate");
	var options = [];
	var lastEvents = document.getElementById("lastevents")
	var selMode = 0;
	var curExport = null;
	var cats = {}
	var interval = 3;
	var intervals = [
		["h",3600],
		["d",3600*24],
		["w",3600*24*7],
		["m",30*3600*24],
		["y",365*3600*24]
	];
	function fillCats() {
		var c = selector.firstElementChild;
		while (c != null) {
			if (c.value && !c.value.startsWith("+")) {
				cats[c.value] = c.innerText;
			}
			c = c.nextElementSibling;
		}
	}
	fillCats();

	var infoMap = {};
	var ometers = {};
	
	function setField(root, name, value, classes) {
		var info = root.querySelectorAll("[data-name="+name+"]");
		Array.prototype.forEach.call(info,function(x) {
			if (typeof value == "number") {
				value = adjNum(value, x.dataset.decimals);
			}
			x.innerText = value;
			if (classes) for (var k in classes) {
				x.classList.toggle(k, classes[k]);
			}
		});
	}
	
	function removeFieldBlock(root, name) {
		var info = root.querySelectorAll("[data-name="+name+"]");
		Array.prototype.forEach.call(info,function(x) {
			x.parentElement.parentElement.removeChild(x.parentElement);
		})
	}
	
	function fillInfo(id,curchart, title, link, ranges, emulated, misc) {
		var elem_title = curchart.querySelector("[data-name=title]");
		var elem_title_class = curchart.querySelector("[data-name=title_class]");
		var elem_icon = curchart.querySelector("[data-name=icon]");
		elem_title_class.classList.toggle("emulated",!!emulated);
		if (misc) {
			curchart.classList.toggle("achieve", !!misc.a);
			curchart.classList.toggle("trade_buy", !!misc.t && misc.t > 0);
			curchart.classList.toggle("trade_sell", !!misc.t && misc.t < 0);
		}
		var error_element = curchart.querySelector("[data-name=erroricon]");
		if (error_element) {
			if (misc && misc.error && misc.error.gen) {			
				error_element.classList.add("error");
				error_element.innerText = "";
				var desc = document.createElement("div");
				desc.innerText = misc.error.gen;
				error_element.appendChild(desc);
			} else {
				error_element.classList.remove("error");
			}
		}
		elem_title.innerText = title;		
		if (misc && misc.icon) {elem_icon.src = misc.icon;elem_icon.hidden = false;} else {elem_icon.hidden = true;}
		var info = curchart.querySelector("[data-name=info]");
		if (ranges) {
			info.hidden = false;
			var pricet = adjNum((ranges.last && ranges.last[0])||0).replace(/[0-9]/g,"–");
			var post = adjNum((ranges.pos && ranges.pos[0])||0).replace(/[0-9]/g,"–");
			var pricee = adjNum((ranges.last && ranges.last[0])||0).replace(/[0-9]/g,"×");
			while (info.firstChild) info.removeChild(info.firstChild);
			["buy","last","sell", "pos"].forEach(function(n) {
				var r = ranges[n];
				var er = misc && misc.error && !misc.t && misc.error[n] ;
				var l1, l2, er;
				if (!r) {
					l1 = pricet;
					l2 = post;
				} else {
					l1 = adjNum(r[0]);
					l2 = adjNum(r[1]);
				}
				var elem = document.createElement("div");
				elem.classList.add(n);
				if (er) elem.classList.add("error");
				info.appendChild(elem);					
				var e = document.createElement("div")
				e.classList.add("price");
				e.innerText = l1;
				elem.appendChild(e);
				e = document.createElement("div")
				e.classList.add("size");
				e.innerText = l2;
				elem.appendChild(e);				
				if (r && r.ometer) {
					var k = ometers[id];
					if (!k) {
						k = ometers[id] = "50%";
					}
					var p = document.createElement("div");
					p.setAttribute("class","ometer");
					p.style.left = k
					e.appendChild(p);
					setTimeout(function() {
						k = ometers[id] = r.ometer+"%";
						p.style.left = k;
					},100);
				}
				if (er) {
					var popup = document.createElement("div")
					popup.innerText = er;
					popup.classList.add("error_popup");
					elem.appendChild(popup);
				}
			});
		} else {
			info.hidden = true;
		}		
		if (link && !eventMap.get(elem_title)) {
			var hndl = function() {
				if (link.startsWith("!")) {
					location.hash = "#"+encodeURIComponent(link.substr(1));
				}
				else if (link.startsWith("_")) {
					var l = link.substr(1);
					if (infoMap[l])
						location.hash = "#!"+encodeURIComponent(l);
					else 
						location.hash = "#"+encodeURIComponent("+pl");
				}
				else {			
					if (infoMap[link])							
						location.hash = "#!"+encodeURIComponent(link);
					else 
						location.hash = "#"+encodeURIComponent("+norm");
				}
			}
			eventMap.set(elem_title, hndl);
			elem_title.addEventListener("click",hndl);
		}
	}

	function updateOptions(id, title) {
		if (options.indexOf(id) == -1) {
			options.push(id); 
			var opt = document.createElement("option");
			opt.value="!"+id;
			opt.innerText = title;
			selector.appendChild(opt);
		}
	}
	
	function createChart(id, template) {
		var curchart = chartlist[id];
		if (curchart === undefined) {
			var t = document.getElementById(template);
			var c = t.content;
			var curchart = document.importNode(c,true).firstElementChild;
			chartlist[id] = curchart;
		}
		if (!curchart.isConnected) {
			document.getElementById("chartarea").appendChild(curchart);						
		}				
		curchart.hidden = false;
		return curchart;
	}
	
	function appendChart(id,  info, data, fld,  orders, ranges, misc) {
		var curchart = createChart(id, "chart");
			try {
				if (!hasChart(data,fld)) {
					curchart.hidden = true;
					return;
				}			
				setTimeout(function() {
					var elem_chart = curchart.querySelector("[data-name=chart]");			
					drawChart(elem_chart, data, fld, orders,  secondary_charts[fld]);
					fillInfo(id,curchart, info.title,id,  ranges, info.emulated);
				},0);
			} catch (e) {
				curchart.hidden = true;
			}
	}

	function appendList(id, info, ranges, misc) {
		var curchart = createChart(id, "summary");
		fillInfo(id,curchart,  info.title,id, ranges, info.emulated, misc);
		var ext =curchart.getElementsByClassName("extended")[0];
		ext.hidden = true;
		var ext =curchart.getElementsByClassName("daystats")[0];
		ext.hidden = true;
	}

	
	function appendSummary(id, info, data, ranges, misc, extra) {
	//	try {
			var curchart = createChart(id, "summary");
			var ext =curchart.getElementsByClassName("daystats")[0];
			ext.hidden = false;
			fillInfo(id,curchart,  info.title,id, ranges, info.emulated, misc);
			var start = Date.now() - 24*60*60*1000;
			var sum = data.reduce(function(a,b,idx) {
				if (b.time >= start) {
					a.trades ++;
					a.price += b.priceDiff;
					a.pos  += b.achg;
					a.pl += b.plDiff;
					a.norm += b.normDiff;
					a.vol += b.vol;
					a.bal += b.achg*b.price;
				}
				return a;
			},{trades:0,price:0, pos:0,norm:0,pl:0,vol:0, bal:0});
			setField(curchart,"assets", info.asset);
			setField(curchart,"curc", info.currency);
			setField(curchart,"pric", info.price_symb);
			setField(curchart,"interval",intervals[interval][0]);
			sum.avg = sum.bal / sum.pos;
			for (var n in sum) {
				if (isNaN(sum[n]))
					removeFieldBlock(curchart,n);
				else
					setField(curchart, n, sum[n], {
						pos:sum[n]>0,
						neg:sum[n]<0
					});
			}
			if (misc) {				
				if (data.length) {
					var lastItem = data[data.length-1];
					var last_norm = lastItem.norm;
					var last_nacum = lastItem.nacum;
					var normdiff = data.reduce(function(a,b) {return a + b.normch},0);
					var pldiff = lastItem.pl - data[0].pl;
					var it = intervals[interval][1]*1000;
					var lt = data[data.length-1];					
					var rate;
//					misc.avgt = last_norm/misc.mt;
					misc.avgh = lt.norm/misc.tt*it;
					misc.avgha = lt.nacum*it/misc.tt;
					misc.avghpl = it*lt.pl/misc.tt;
					misc.avgh_pp = misc.avgh/misc.bt*100;
					misc.avgha_pp = misc.avgha/misc.ba*100;
					misc.avghpl_pp = misc.avghpl/misc.bt*100;
					misc.rpnl_pp = misc.rpnl/misc.bt*100;
					misc.upnl_pp = misc.upnl/misc.bt*100;
					misc.p0 = lastItem.p0 == undefined?"---":lastItem.p0
					misc.pnorm = last_norm;
					misc.pnormp = 100*last_norm/misc.mv;
					if (pldiff > 0) {
						if (normdiff > pldiff) rate = "A";
						else if (normdiff > 0 && normdiff < pldiff) rate = "B";
						else rate = "C"
					} else if (normdiff > 0) {
						if (normdiff > -pldiff) rate = "B";
						else rate = "D";
					} else {
						rate = "E";
					}
					if (misc.be !== undefined) misc.be_pp=misc.be/misc.bt*100;
					setField(curchart,"rate",rate,{["rate"+rate]:true,rate:true});
					curchart.classList.toggle("disable_na_p",isNaN(misc.avgha));
					curchart.classList.toggle("has_budget_extra",misc.be!==undefined);
					
				} else {
					misc.pnorm = 0;
					misc.pnormp = 0;

				}

				for (var n in misc)
					setField(curchart, n, misc[n], {
						pos:misc[n]>0,
						neg:misc[n]<0
					});
			}
			var ext =curchart.getElementsByClassName("extended")[0];
			ext.hidden = !extra;
			
			
		//} catch (e) {}
	}
	
	function setMode(mode) {
		if (selMode != mode) {
			for(var k in chartlist) {
				var ch = chartlist[k];
				if (ch.isConnected) ch.parentElement.removeChild(ch);
			}
			selMode = mode;							
		}
	}

	
	var table_rows = new WeakMap();
	var tmp = [];
	
	function updateLastEvents(chart, ident) {
		curExport = chart;
		tmp = chart;
		var c = chart.slice(0);	
		c.reverse();		
		var table = lastEvents.querySelector("x-table");
		if (table == null) {
			table = document.createElement("x-table");
			lastEvents.appendChild(table);
			var hdr = document.getElementById("lastevents_hdr");
			hdr = document.importNode(hdr.content,true);
			table.appendChild(hdr);
			var export_csv = table.querySelector("[data-name=export_csv]");
			export_csv.addEventListener("click", function() {
				var s = createCSV(curExport, infoMap);
				var blob = new Blob(
					    [ s ],
					    {
					        type : "text/csv;charset=utf-8"
					    });
				 var downloadUrl = URL.createObjectURL( blob );
				 var a = document.createElement("A");
				 a.setAttribute("href", downloadUrl);
				 a.setAttribute("download",ident+".csv");
				 a.setAttribute("target","_blank");
				 document.body.appendChild(a);
				 a.click();
				 document.body.removeChild(a);
				 URL.revokeObjectURL(downloadUrl);
			})
		}
		var p = table.firstElementChild.nextElementSibling;
		while (p) {
			var q = p;
			p = p.nextSibling;
            if (q.classList.contains("animout")) {
			    q.parentElement.removeChild(q);	
            }
		}
		

		var delay=0;
		var skips = false;
		var newitems = [];

		function hasRow( p,r) {
			if (p) {
				var item = table_rows.get(p);
				if (item) {
					if (item.ident == r.ident && item.time == r.time) 
						return true;
				}				
			}
			return false;
		}



		var p = table.firstElementChild.nextElementSibling;

		c.forEach(function(r) {
			while (p && p.classList.contains("animout")) {
				p = p.nextSibling;
			}
			if (hasRow(p,r)) {
				p = p.nextSibling;
				skips = true;
				return;
			} else if (p) {
				var q = p.nextSibling;
				if (q && hasRow(q,r)) {
					skips = true;
					p = q;
					return;
				}
			}
			delay++;
		});
		if (delay > 8) delay = 8;

		var p = table.firstElementChild.nextElementSibling;;
		
		c.forEach(function(r) {
			if (hasRow(p,r)) {
				p = p.nextSibling;
				return;
			} else if (p) {
				var q = p.nextSibling;
				if (q && hasRow(q, r)) {
					p.classList.add("animout");							
					p.firstElementChild.style.animationDelay="0";
					p.firstElementChild.classList.remove("anim");
					p = q.nextSibling;
					return;
				}
			}
			var trbgr = document.createElement("x-trbgr");
			table.insertBefore(trbgr, p);
			var tr = document.createElement("x-tr");
			if (skips && delay >= 0) {
				tr.classList.add("anim");
				tr.style.animationDelay=delay+"s";
			}
			newitems.push(tr);
			delay--;
			trbgr.appendChild(tr);
			table_rows.set(trbgr, {ident:r.ident, time:r.time});
			var dr = new Date(r.time);
			
			if (r.donation) {
				var don = document.getElementById("donation_item").content;
				don = document.importNode(don,true);
				tr.appendChild(don);
			} else {
				var data = [				
					dr.toLocaleString("default",{"day":"numeric","month":"2-digit"}),
					dr.toLocaleString("default",{"hour":"2-digit","minute":"2-digit","second":"2-digit","hour12":false}),
					r.ident?infoMap[r.ident].title:"",
					r.alert?
					    (r.alert.side<0?"↘|":r.alert.side>0?"↗|":"!")
					    :(r.achg < 0?"↘":r.achg?"↗":"!"),
					r.achg?Math.abs(r.achg):((r.alert && r.alert.reason && alert_texts[r.alert.reason] && alert_texts[r.alert.reason][1]) || "alert"),
					r.price,
					r.plDiff,
					r.normch,
				]
				tr.classList.toggle("sell", r.achg<0);
				tr.classList.toggle("buy", r.achg>0);
				tr.classList.toggle("manual", r.man);
				data.forEach(function(z) {
					var td = document.createElement("x-td");
					if (typeof z == "number") {
						td.innerText = adjNum(z);
						td.classList.add(z<0?"neg":"pos");
					} else {
						td.innerText = z;
					}
					tr.appendChild(td);
				});
			
			
				var ident_element = tr.querySelector("x-td:nth-child(3)");
				var first_element = tr.querySelector("x-td:first-child");
				
				ident_element.addEventListener("click", function() {
					location.hash = "!"+encodeURIComponent(r.ident);
				});
				
				var trade_id_elem = document.createElement("div");
				trade_id_elem.innerText =  (r.alert && alert_texts[r.alert.reason] && (alert_texts[r.alert.reason][1]+": "+alert_texts[r.alert.reason][0])) || r.id;
				first_element.appendChild(trade_id_elem);
			}
			
			
		})		
		while (p != null) {
			var nx = p.nextSibling;
			table.removeChild(p);
			p = nx;		
		}
	}
	
	function updateLastEventsAll(chart) {
		var sumchart = [];
		for (var z in chart) {
			sumchart = sumchart.concat(chart[z].slice(-20));			
		}
		sumchart.sort(function(a,b){
			return a.time - b.time; 
		});
		updateLastEvents(sumchart.slice(-20),"summary");
	}
	
	function calcApp(itm) {
		var t = 365.0*24.0*60.0*60.0*1000.0*100.0;
		var v = itm.invst_n*t/itm.invst_v;
		if (isNaN(v) || !isFinite(v)) return 0;
		else return v;
	}
	
	function safediff(a,b) {
		if (a === undefined) return b;
		if (b === undefined) return a;
		return a-b;
	}

	function adjChartData(data, ident) {	
		var lastNorm = 0;	
		var lastPL = 0;
		var lastPrice = 0;
		var lastNacum = 0;
		var lastPLn = 0;
		var lastRPL = 0;
		data.forEach(function(r) {
			r.ident = ident;
			r.normDiff = safediff(r.norm , lastNorm);
			r.nacumDiff = safediff(r.nacum , lastNacum)
			r.plDiff = safediff(r.pl , lastPL);
			r.rplDiff = safediff(r.rpl , lastRPL);
			r.priceDiff = safediff(r.price , lastPrice);
			r.plnDiff = safediff(r.pln , lastPLn)
			r.vol = r.volume;
			r.app = calcApp(r);
			lastNorm += r.normDiff;
			lastPL += r.plDiff;
			lastPrice += r.priceDiff;
			lastNacum += r.nacumDiff;
			lastPLn += r.plnDiff;
			lastRPL = r.rpl;
		});
	}
	
	function calculate_sums(charts, infoMap) {
		var sums = {};
		charts = Object.keys(charts).reduce(function(a,k) {
			var ch = charts[k];
			if (Array.isArray(ch) && ch.length > 3) a[k] = ch;
			return a;
		},{})
		var maxDate = Date.now();
		var minDate = Object.values(charts).reduce(function(a,b) {
			return a>b[0].time?b[0].time:a;
		},maxDate);
		var midDate = (2*minDate+maxDate)/3;
		var firstDate = Object.values(charts).reduce(function(a,b) {
			return (b[0].time < midDate && a<b[0].time)?b[0].time:a;
		},minDate);
		
		for (var k in charts) {
			sums[infoMap[k].currency] = [];			
		}
		for (var k in charts) {
			sums[infoMap[k].currency] = sums[infoMap[k].currency].concat(charts[k]); 
		}
		for (var k in sums) {
			sums[k].sort(function(a,b){
				return a.time - b.time; 
			})
			sums[k] = sums[k].reduce(function(s,r){				
				if (s.length) {
					var x = {
							invst_n: s[s.length-1].invst_n+r.invst_n_diff,
							invst_v: s[s.length-1].invst_v+r.invst_v_diff,							
					}
					s.push({
						time:r.time,
						pl:s[s.length-1].pl+r.plDiff,
						norm:s[s.length-1].norm+r.normDiff,
						pln:s[s.length-1].pln+r.plnDiff,
						normDiff:r.normDiff,
						plDiff:r.plDiff,
						rplDiff:r.rplDiff,
						plnDiff:r.plnDiff,
						vol: r.volume,
						rel: s[s.length-1].rel+ r.relDiff,
						relDiff: r.relDiff,
						app: calcApp(x)
					});
				} else {
					s.push({
						time:r.time,
						pl:r.plDiff,
						pln:r.plnDiff,
						plDiff:r.plDiff,
						rplDiff:r.rplDiff,
						norm:r.normDiff,
						normDiff:r.normDiff,
						vol: r.volume,
						rel: r.relDiff,
						relDiff: r.relDiff,
						app: calcApp(r)
					})
				}				
				return s;
			},[]);
		}
		Object.keys(sums).forEach(function(k){
			sums[k] = sums[k].filter(function(a) {
				return a.time >= firstDate;
			});
			if (sums[k].length == 0) delete sums[k];
		});
		return sums;
	}
	
	function calculate_pnls(charts, infoMap, prices, sums) {
		var pnl = {};
		var org = {};
		for (var k in sums) {
			org[k] = pnl[k] = sums[k][sums[k].length-1].pl;
							
		}
		for (var k in charts) {
			var p = prices[k];
			if (p) {
				var ch = charts[k];
				if (ch.length>3) {
					var lt = ch[ch.length-1];
					var price = lt.price;
					var last_price = prices[k];
					var pos = lt.pos;
					var profit = infoMap[k].inverted
					        ?(1/price-1/last_price)*pos
					        :(last_price - price)*pos;
					pnl[infoMap[k].currency]+= profit;
				}
			}
		}		
		var retpnl = {};
		for (var k in pnl) {
			retpnl[k] = [{
				pl:pnl[k],
				class:"last",
				label:"("+(org[k]<pnl[k]?"+":"")+adjNum(pnl[k]-org[k])+") = ",				
			}];
		}
		return retpnl;
	} 
	
	function notifyTrades(trades) {
		trades.sort(function(a,b){return b.time - a.time;});
		if (trades[0].time) {
			last_ntf_time = trades[0].time+1;
		}

		  var text = trades.reduce(function(txt, itm){
			var ln = (itm.size>0?"𝗕𝗨𝗬 ":itm.size<0?"𝗦𝗘𝗟𝗟":"𝗔𝗟𝗥𝗧")
					+" "+(itm.size?adjNum(Math.abs(itm.size)):"")+" "+itm.title
					+" 𝗮𝘁 "+adjNum(itm.price);
			txt = txt + ln + "\n";
			return txt;
		  },"");

		  var ntf_perm = "Notification" in window?Notification.permission:"denied";
		  var hide_bell = true;
		  if (ntf_perm == "granted") {
  			 svcreg.then(function(svcreg) {
		 		if (svcreg.showNotification) {
		 			svcreg.showNotification("Executed trades",{
						  body:text,
  						  badge: "res/ntfbadge.png",
  						  icon: "res/icon64.png",
  						  tag:"trade",
  						  renotify: true
  					 });  			 			
		 		} else {
				  var notification = new Notification("MMBot",{
					  body:text,
					  badge: "res/ntfbadge.png",
					  icon: "res/icon64.png"
				  });
				  setTimeout(notification.close.bind(notification), 15000);
		 		}
  			 });  
		  } else if (ntf_perm == "default") {
			  hide_bell = false;
		  }
		  
		 var ntf = document.getElementById("notify");
		 ntf.classList.add("shown");
		 var inner = ntf.querySelector("div");
		 inner.innerText="";
		 var bell = ntf.querySelector("img");
		 bell.hidden = hide_bell;
		 var box = document.createDocumentFragment();
		 text.split('\n').forEach(function(x){
			 var d = document.createElement("div");
			 d.appendChild(document.createTextNode(x));
			 box.appendChild(d);
		 });
		 inner.appendChild(box);
		 setTimeout(function()  {
			ntf.classList.remove("shown");
		 },10000);
	}
	
	notifyTradesFn = notifyTrades;

	function hasChart(chart, item) {
		return (chart.length != 0) && chart.find(function(x) {
			return item in x
		}) !== undefined;
	}

    function setIndikator(mode, msg) {
    	indicator.classList.toggle("online", mode == 0);
    	indicator.classList.toggle("fetching", mode == 1);
    	if (msg) indicator.setAttribute("title", msg); else indicator.removeAttribute("title");
    	void indicator.offsetWidth;
    }

    
    function initSSE() {
    	setIndikator(1);
        initSSE.handle = new EventSource("api/data", {withCredentials:true});
        initSSE.handle.onmessage = function(ev) {
        	updateFromSSE(JSON.parse(ev.data));
        }
        initSSE.handle.onerror = function(e) {        	
            setIndikator(-1,"Connection lost - reconnecting");
            fetch("api/user")
                    .then(function(req){return req.json()})
                    .then(function(x){
                    	if (!x.viewer) location.href="api/login?redir="+encodeURIComponent(location.href);
                    });
            updateFromSSE(null);
            initSSE.handle.close();
            setTimeout(initSSE, 5000);
        }
    }

    var updateFromSSE = (function(){
        var stats ={};        
        var rf = false;
		var msgcnt = 0;		
		var tm = null;

        return function(msg) {
        	if (msg === null) {
        		clearInterval(tm);
        		tm = null;
        		return;
        	}
        	if (!tm) {
        		tm = setInterval(function(){
			    if (msgcnt == 0) {
				    initSSE.handle.onerror();
			    } else {
				    msgcnt = 0;
			    }        		
		        },60000);
        	}

			msgcnt++;
			if (typeof msg == "string") {
				if ((msg == "update" && !rf) || msg == "end_refresh") {
		        	setIndikator(1);
					rf = false;				
	            	try {
	            		stats.charts = {};
	            		for (var z in stats.charts_by_id) {
	            			stats.charts[z] = [];
	            			for(var zz in stats.charts_by_id[z]) {
	            				stats.charts[z].push(stats.charts_by_id[z][zz]);
	            			}
	            			stats.charts[z].sort(function(a,b){
	            				return a.time-b.time;
	            			});
	            		}
	            	    update(stats)
	                    logo.hidden = true;
	                    setIndikator(0);
	            	} catch (e) {
	            		setIndikator(-1, e.toString());   
	            		console.error(e);
	            	}
				} else if (msg == "refresh") {
					stats = {
						info:{},
						charts_by_id:{},
						misc:{},
						prices:{},
						log:[],
						orders:[]
					};
					rf = true;
		        	setIndikator(1);
				}
			} else switch(msg.type) {
                case "config": Object.assign(stats, msg.data);break;
                case "performance": stats.performance = msg.data;break;
                case "version": stats.version = msg.data;break;
                case "info": stats.info[msg.symbol] = msg.data;break;
                case "trade": if (!stats.charts_by_id[msg.symbol]) stats.charts_by_id[msg.symbol] = {};
                                  stats.charts_by_id[msg.symbol][msg.id] = msg.data;
                                  break;
                case "misc": stats.misc[msg.symbol] = Object.assign(stats.misc[msg.symbol] || {},msg.data);break;
                case "price":  stats.prices[msg.symbol] = msg.data;break;
				case "error": if (!stats.misc[msg.symbol]) stats.misc[msg.symbol] = {};
				                  stats.misc[msg.symbol].error = msg.data;
                case "news": stats.news = msg.data.count;break
                case "order": var opos = stats.orders.findIndex(function(x){
                	return x.symb == msg.symbol && x.dir == msg.dir;
                    });
                    if (opos == -1) {
						if (msg.data.price) { 
							stats.orders.push({
		                    	symb: msg.symbol,
		                    	dir: msg.dir,
		                    	price: msg.data.price,
		                    	size: msg.data.size
							});					
                    	} 
					} else {	
                    	var x = stats.orders[opos];
						if (msg.data.price) {
                    		x.price = msg.data.price;
                    		x.size = msg.data.size;
						} else {
							stats.orders.splice(opos,1);
						}
                    };
                    break;
               case "log": stats.log.push(msg.data);
                           if (stats.log.length>100) stats.log.shift();
                           break;
               	                   

            }

        }

    })();



	function update(stats) {
	
			
			source_data = stats;
			var orders = {};
			var ranges = {};
			
			infoMap = stats.info;			
			var charts = stats["charts"];

			for (var n in infoMap) {
				ranges[n] = [];
				orders[n] = [];
				if (!charts[n]) charts[n] = [];

			}
			
			document.getElementById("logfile").innerText = " > "+ stats["log"].join("\r\n > ");
			document.getElementById("newsbox").hidden = !stats["news"];
			document.getElementById("msgcnt").textContent = stats["news"] || "0";

			
			var newTrades = [];
			for (var n in infoMap) {
				var ch = charts[n];				
				orders[n] = [];
				ranges[n] = {};
				adjChartData(charts[n], n);
				updateOptions(n, infoMap[n].title);
				var nt = ch.reduce(function(nt, x) {
					if (x.time >= last_ntf_time) {
						nt.push({
							price:x.price,
							size:x.achg,
							asymb:infoMap[n].asset,
							csymb:infoMap[n].currency,
							title:infoMap[n].title,
							time:x.time
						});
					}
					return nt;
				},[]);
				Array.prototype.push.apply(newTrades,nt);
			}

			if (newTrades.length > 0) {
				notifyTrades(newTrades);
			}

					
			(stats.orders || []).forEach(function(o) {
				if (!ranges[o.symb]) return;
				var s = orders[o.symb] || [];
				var sz = o.size;
				var ch =  charts[o.symb];
				var inv = infoMap[o.symb].inverted;
				var last;
				if (!ch || !ch.length) {
					last = {pl:0,price:o.price,pos:0,norm:0};
				} else {
					 last = ch[ch.length-1];
				}
				var dir = o.dir < 0?"sell":"buy";
				var norm = o.dir < 0?stats.misc[o.symb].cur_norm_sell:stats.misc[o.symb].cur_norm_buy;				
				var gain = ((inv?1.0/o.price:o.price) - (inv?1.0/last.price:last.price))* (inv?-1:1)*last.pos;
				var newpl = last.pl + gain;
				var newpos = last.pos + sz;
				var newnorm = last.norm + norm;
				s.push({
					price: o.price,
					achg: sz,
					pl: newpl,
					pos: newpos,
					label: "",
					gain:gain,
					class: dir,
					norm: newnorm
				})
				if (Math.abs(o.dir)<2) {
				    ranges[o.symb][dir] = [o.price,o.size];
				}
			}) 
			
			for (var sm in stats.prices) if (infoMap[sm]) {
				var s = orders[sm];
				if (!s) continue;
				var ch =  charts[sm];
				var inv = infoMap[sm].inverted;
				var last;
				if (!ch || !ch.length) {
					last = {pl:0,price:stats.prices[sm],pos:0,app:0,norm:0,nacum:0};
				} else {
					 last = ch[ch.length-1];
				}
				var gain = ((inv?1.0/stats.prices[sm]:stats.prices[sm])- (inv?1.0/last.price:last.price))* (inv?-1:1)*last.pos;
				s.push({
					price: stats.prices[sm],
					pl: last.pl + gain,
					label: "",
					class: "last",
				})
				ranges[sm]["last"] = [stats.prices[sm],""];
				ranges[sm].pos = [last.pos,infoMap[sm].asset];
				var rpos;
				if (ranges[sm].buy !== undefined) {
					if (ranges[sm].sell !== undefined) {
						rpos = (ranges[sm].last[0] - ranges[sm].buy[0])/(ranges[sm].sell[0] - ranges[sm].buy[0])
					} else {
						rpos = 1;
					}
				} else if (ranges[sm].sell !== undefined) {
					rpos = 0;
				} else {
					rpos = 0.5;
				}
				ranges[sm].last.ometer = adjNum(rpos*100);
				stats.misc[sm].icon = infoMap[sm].brokerIcon;
			
			}

			var sums = calculate_sums(charts,infoMap);
			var plns = calculate_pnls(charts,infoMap, stats.prices, sums);
			
			localStorage["mmbot_time"] = Date.now();

			
			drawChart = initChart(stats.interval);
			redraw = function() {

                var traders = Object.keys(charts).map(function(k) {
                    return {
                    	k:k,
                    	info: infoMap[k],
                    	chart: charts[k],
                    	ranges: ranges[k],
                    	misc: stats.misc[k],
                    }
                });
                traders.sort(function(a,b){
                	return a.info.order - b.info.order;
                });


				chart_padding.hidden = false;
				
				var fld = location.hash;		
				if (!fld) {
					fld = localStorage["home"];
				}
				if (fld) {
					fld = decodeURIComponent(fld.substr(1));
				}
				else {
				    fld = "+summary";
				}
				
				selector.value = fld;

				setMode(location.hash);
				if (fld == "+summary") {
					traders.forEach(function(t) {
						appendSummary("_"+t.k,t.info, t.chart, t.ranges, t.misc);
					});
					for (var k in sums) {
						appendSummary("_"+k,{"title":k,"asset":"","currency":k}, sums[k]);
					}
					updateLastEventsAll(charts);
					
				} else if (fld == "+list") {
					traders.forEach(function(t) {
						appendList("_"+t.k,t.info, t.ranges, t.misc);
					});
					updateLastEventsAll(charts);
				} else if (fld == "+dpr") {
					appendDailyPerformance(stats.performance);					
					chart_padding.hidden = true;
				} else if (fld.startsWith("!")) {
					var pair = fld.substr(1);
					appendSummary("_"+pair,infoMap[pair], charts[pair], ranges[pair], stats.misc[pair],true);
					for (var k in cats) {						
						appendChart("!"+k+"_"+pair, {title:cats[k]}, charts[pair], k, orders[pair], stats.misc[k]);
						
					}
					setTimeout(function(pair) {
						updateLastEvents(charts[pair],pair);
					}.bind(this,pair),1);
					
				} else if (fld.startsWith("+")) {
					fld = fld.substr(1);
					for (var k in sums) {
						appendChart(k,{"title":k}, sums[k], fld, plns[k]);
					}
					updateLastEventsAll(charts);			
				} else {
					traders.forEach(function(t) {
						appendChart(t.k,t.info, t.chart, fld, orders[t.k],t.ranges, t.misc);
					});
					updateLastEventsAll(charts);
				}
				if (lastField) {
					selector.value = lastField;
					lastField = null;
					redraw();
				}
				
				document.getElementById("chartarea")
					.appendChild(chart_padding);
				
			}
			
			
			redraw();
			document.getElementById("version").innerText = stats.version; 
			


	}
	


	function appendDailyPerformance(data) {
		document.getElementById("lastevents").innerText="";
		var table = document.createElement("table");
		table.setAttribute("class","perfmod");
		var hdr = document.createElement("tr");
		data.hdr.forEach(function(x) {
			var h = document.createElement("th");
			h.innerText = x;
			hdr.appendChild(h);
		})
		table.appendChild(hdr);

		function row(data, cls, symbol){
			var first = !symbol;
			var hr = document.createElement("tr");
			if (cls) hr.setAttribute("class",cls);
			if (symbol) {
				var th = document.createElement("td");
				hr.appendChild(th);
				th.innerText=symbol;
			}
			data.forEach(function(c) {
				var h = document.createElement("td");
				if (first && typeof(c) == "number") {
					first = false;
					h.innerText = (new Date(c*1000)).toLocaleDateString();
				} else {
					h.innerText = adjNum(c);
					h.classList.toggle("pos", c>0);
					h.classList.toggle("neg", c<0);
				}
				hr.appendChild(h);
			});
			table.appendChild(hr);			
		}

		
		data.rows.forEach(function(r) {
			row(r, null, null);
		})
		row(data.sums, "sep bold","∑");
/*		row(data.avg, "ital","⌀");*/
		var curchart = createChart("perfrep", "perfrep");
		curchart .innerText = "";
		curchart .appendChild(table);
	}
	
	
	window.addEventListener("hashchange", function() {
		table_rows = new WeakMap();
		redraw();
	}) 
	
	selector.addEventListener("change",function(){
		location.hash = "#"+encodeURIComponent(selector.value);
	});
	var home_tap_cntr = 0;
	var home_tap_prev;
	home.addEventListener("click",function() {
		if (home_tap_prev && !location.hash) {
			home_tap_cntr++;
			if (home_tap_cntr > 2) {
				localStorage["home"] = home_tap_prev;
				location.hash = home_tap_prev;
			}
		} else {
			home_tap_prev = location.hash;
			location.hash = "#";
			home_tap_cntr = 0;
		}		
	})
	
	
	var logo = document.getElementById("logo");
	
    logo.hidden = false;
    initSSE();
	
	opencloselog.addEventListener("click",function() {
		var v = document.getElementById("logfile");
		v.hidden = !v.hidden;
	})
	
	document.getElementById("notify").addEventListener("click", function() {
		if (Notification && Notification.permission == "default") {
			 Notification.requestPermission(function (permission ) {
		      if (permission === "granted") {
		 			svcreg.then(function(svcreg) {
		 				svcreg.showNotification("Notifications are enabled",{		 			
						  badge: "res/ntfbadge.png",
						  icon: "res/icon64.png"
					 });
		 			});
		      }				
			 });
		}
	})
	

	changeinterval = function() {
		interval = (interval+1)%intervals.length;
		redraw();
	}
}


function createCSV(chart, infoMap) {
	"use strict";
	
	function makeRow(row) {
		var s = JSON.stringify(row);
		return s.substr(1,s.length-2);
	}
	
	var rows = chart.map(function(rw){
		return makeRow([
			(new Date(rw.time)).toLocaleString("en-GB"),
			infoMap[rw.ident].title,
			rw.price,
			rw.achg,
			rw.volume,
			infoMap[rw.ident].asset,
			infoMap[rw.ident].currency
		]);
	})
	
	rows.unshift(makeRow([
			"date",
			"pair",
			"price",
			"size",
			"value",
			"currency",
			"asset",
		]));
	return rows.join("\r\n");
}


function donate() {
	var w = document.getElementById("donate_window");
	w.classList.toggle("shown");
	if (!donate.inited) {
		donate.inited = true;
		var col = w.querySelectorAll("[data-link]");
		Array.prototype.forEach.call(col,function(x) {
			var content = x.innerText;
			x.innerHTML="<a href=\""+x.dataset.link+":"+content+"\">"+content+"</a>";
		});
	}
}
function close_donate() {	
	var w = document.getElementById("donate_window");
	w.classList.remove("shown");
}

function change_skin() {
	document.body.classList.toggle("daymode");
	updateThemeColor();
	try {
		localStorage["mmbot_skin"] = 
			document.body.classList.contains("daymode")?"day":"night";
	} catch (e) {}
}

function updateThemeColor() {
	var metaThemeColor = document.querySelector("meta[name=theme-color]");
	if (!metaThemeColor) {
		metaThemeColor = document.createElement("meta");
		metaThemeColor.setAttribute("name","theme-color");
		document.head.appendChild(metaThemeColor);
	}
	if (document.body.classList.contains("daymode")) {
		metaThemeColor.setAttribute("content", "#cdcdcd");
	} else {
		metaThemeColor.setAttribute("content", "#202020");
	}
}

function testNotify() {
	try {
	notifyTradesFn([{
		time:0,
		size:0,
		price:0,
		asymb:"TEST_A",
		csymb:"TEST_C",
		title:"Test notify"
	},
{
		time:1,
		size:1,
		price:1,
		asymb:"TEST_A",
		csymb:"TEST_C",
		title:"Test notify"

	},
	{
		time:2,
		size:-1,
		price:2,
		asymb:"TEST_A",
		csymb:"TEST_C",
		title:"Test notify"
	}


	]);
	} catch (e) {
		alert(e.toString());
	}
}


