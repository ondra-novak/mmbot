function beginOfDay(dt) {
	return new Date(dt.getFullYear(), dt.getMonth(), dt.getDate());
}

 
function fetch_json(file) {
	"use strict";

	return fetch(file).then(function(req) {
		return req.json();
	});
	
}

var changeinterval=null;

function adjNum(n) {
	"use strict";
	if (typeof n != "number") return n;
	if (!isFinite(n)) { 
		if (s < 0) return "-∞";
		else return "∞";
	}
	var an = Math.abs(n);
	if (an >= 100000) return n.toFixed(0);
	else if (an >= 1) return n.toFixed(2);
	else if (an > 0.0001) return n.toFixed(6);
	else {
		var s = (n*1000000).toFixed(3);
		if (s == "0.000") return s;
		return s+"µ";
	}
}

function app_start(){

	"use strict";

	
	if (navigator.serviceWorker) {
		navigator.serviceWorker.register('sw.js', {	
			scope: '.'
		});
	}
	
	var lastField="";
	var next_donate_time = 0;
	var donation_repeat = (10*24*60*60*1000);
	var last_ntf_time=Date.now();
	
	try {
		lastField = localStorage["markettrader_lastfield"];
		next_donate_time = parseInt(localStorage["next_donate_time"]);
	} catch (e) {

	}
	if (isNaN(next_donate_time) || next_donate_time < Date.now()) {
		next_donate_time = Date.now() + donation_repeat;
		localStorage["next_donate_time"] = next_donate_time; 	
	}
	
	var donate_time = next_donate_time - donation_repeat;
	var show_donate = !localStorage["donation_hidden"]
	
	

	var chartlist = {};
	var indicator = document.getElementById("indicator");
	var eventMap = new WeakMap();
		
	var redraw = function() {};

	var selector = document.getElementById("selchart");
	var outage = document.getElementById("outage");
	var home = document.getElementById("home");
	var opencloselog = document.getElementById("logfileactivate");
	var options = [];
	var lastEvents = document.getElementById("lastevents")
	var selMode = 0;
	var chart_interval=1000;
	var curExport = null;
	var cats = {}
	var interval = 0;
	var intervals = [
		["h",3600],
		["d",3600*24],
		["w",3600*24*7],
		["m",30*3600*24],
		["y",365*3600*24]
	];
	var timediff = null;
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
	
	function setField(root, name, value, classes) {
		var info = root.querySelectorAll("[data-name="+name+"]");
		Array.prototype.forEach.call(info,function(x) {
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
	
	function fillInfo(curchart, title, link, ranges, emulated, misc) {
		var elem_title = curchart.querySelector("[data-name=title]");
		elem_title.classList.toggle("emulated",!!emulated);
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
		var info = curchart.querySelector("[data-name=info]");
		if (ranges) {
			info.hidden = false;
			var pricet = adjNum((ranges.last && ranges.last[0])||0).replace(/[0-9]/g,"–");
			var post = adjNum((ranges.pos && ranges.pos[0])||0).replace(/[0-9]/g,"–");
			var pricee = adjNum((ranges.last && ranges.last[0])||0).replace(/[0-9]/g,"×");
			while (info.firstChild) info.removeChild(info.firstChild);
			["buy","last","sell", "pos"].forEach(function(n) {
				var r = ranges[n];
				var er = misc && misc.error && misc.error[n];
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
		return curchart;
	}
	
	function appendChart(id,  info, data, fld,  orders, ranges, misc) {
		try {
			var curchart = createChart(id, "chart");
			var elem_chart = curchart.querySelector("[data-name=chart]");
			drawChart(elem_chart, data, fld, orders);
			fillInfo(curchart, info.title,id,  ranges, info.emulated);
		} catch (e) {}
	}

	function appendList(id, info, ranges, misc) {
		var curchart = createChart(id, "summary");
		fillInfo(curchart,  info.title,id, ranges, info.emulated, misc);
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
			fillInfo(curchart,  info.title,id, ranges, info.emulated, misc);
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
					setField(curchart, n, n=="trades"?sum[n]:adjNum(sum[n]), {
						pos:sum[n]>0,
						neg:sum[n]<0
					});
			}
			if (misc) {
				var last_norm = data.length?data[data.length-1].norm:0;
				var last_nacum = data.length?data[data.length-1].nacum:0;
				misc.pnorm = last_norm;
				misc.pnormp = 100*last_norm/misc.mv;
				if (data.length) {
					var it = intervals[interval][1]*1000;
					var lt = data[data.length-1];
					misc.avgt = last_norm/misc.mt;
					misc.avgh = lt.invst_n*it;
					misc.avgha = lt.invst_n*it*lt.nacum/lt.norm;
					misc.avghpl = lt.invst_n*it*lt.pl/lt.norm;
				}

				for (var n in misc)
					setField(curchart, n, adjNum(misc[n]), {
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

		c.forEach(function(r) {
			if (hasRow(p,r)) {
				p = p.nextSibling;
				skips = true;
				return;
			}
			delay++;
		});
		if (delay > 8) delay = 8;

		var p = table.firstElementChild.nextElementSibling;
		
		c.forEach(function(r) {
			if (hasRow(p,r)) {
				p = p.nextSibling;
				return;
			}
			var trbgr = document.createElement("x-trbgr");
			table.insertBefore(trbgr, p);
			var tr = document.createElement("x-tr");
			if (skips && delay >= 0) {
				tr.classList = "anim"
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
					dr.toLocaleString("default",{"hour":"2-digit","minute":"2-digit","second":"2-digit"}),
					r.ident?infoMap[r.ident].title:"",
					r.achg < 0?"↘":"↗",
					Math.abs(r.achg),
					r.price,
					r.volume,
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
				trade_id_elem.innerText =  r.ident + " " + r.id;
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
		if (donate_time &&  show_donate) {
			sumchart.push({
				time: donate_time,
				donation: true
			});
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
	
	function adjChartData(data, ident) {	
		var lastNorm = 0;	
		var lastPL = 0;
		var lastPos = 0;
		var lastPrice = 0;
		var lastRel = 0;
		var lastNacum = 0;
		var lastInvstV = 0;
		var lastInvstN = 0;
		data.forEach(function(r) {
			r.ident = ident;
			r.normDiff = r.norm - lastNorm;
			r.nacumDiff = r.nacum - lastNacum;
			r.plDiff = r.pl - lastPL;
			r.priceDiff = r.price - lastPrice;
			r.vol = r.volume;
			r.relDiff = r.rel - lastRel;
			r.app = calcApp(r);
			r.invst_v_diff = r.invst_v - lastInvstV;
			r.invst_n_diff = r.invst_n - lastInvstN;
			lastNorm = r.norm;
			lastPL = r.pl;
			lastPos = r.pos;
			lastPrice = r.price;
			lastRel = r.rel;
			lastInvstV = r.invst_v;
			lastInvstN = r.invst_n;
			lastNacum = r.nacum;
		});
	}
	
	function calculate_sums(charts, infoMap) {
		var sums = {};
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
						normDiff:r.normDiff,
						plDiff:r.plDiff,
						vol: r.volume,
						rel: s[s.length-1].rel+ r.relDiff,
						relDiff: r.relDiff,
						invst_n:x.invst_n,
						invst_v:x.invst_v,
						app: calcApp(x)
					});
				} else {
					s.push({
						time:r.time,
						pl:r.plDiff,
						plDiff:r.plDiff,
						norm:r.normDiff,
						normDiff:r.normDiff,
						vol: r.volume,
						rel: r.relDiff,
						relDiff: r.relDiff,
						invst_n: r.invst_n,
						invst_v: r.invst_v,
						app: calcApp(r)
					})
				}				
				return s;
			},[]);
		}
		return sums;

	
	}
	
	function notifyTrades(trades) {
		console.log("Notify trades:", trades);
		last_ntf_time = Date.now();

		 if ("Notification" in window && Notification.permission !== "denied") {
		 	 var ntp = null;			
  			 if (Notification.permission === "granted") {
  			 	ntp = Promise.resolve(true);
  			 } else {
  			 	ntp = new Promise(function(ok) {
  					Notification.requestPermission(function (permission) {
  						ok (permission === "granted");
  					});
  			 	});
  			 }
			  var text = trades.reduce(function(txt, itm){
				var ln = (itm.size>0?"↗↗↗":itm.size<0?"↘↘↘":"!!!")
						+" "+adjNum(Math.abs(itm.size))+" "+itm.asymb
						+" @ "+adjNum(itm.price)+ " " + itm.csymb;
				txt = txt + ln + "\n";
				return txt;
			  },"");
  			 ntp.then(function(r) {
  			 	if (r) {
  					  var notification = new Notification("MMBot",{
  						  body:text,
  						  icon: "res/icon64.png"
  					  });
  					  setTimeout(notification.close.bind(notification), 15000);		  					  	 	
  			 	}
  			 });  
  			 var ntf = document.getElementById("notify");
  			 ntf.classList.add("shown");
  			 ntf.innerHTML =  text.replace("\n","<br>");
  			 setTimeout(function()  {
  			 	ntf.classList.remove("shown");
  			 },10000);
		 }
	}

	function update() {
		
		indicator.classList.remove("online");
		indicator.classList.add("fetching");
		return fetch_json("report.json?r="+Date.now()).then((stats)=>{

			
			var orders = {};
			var ranges = {};
			
			infoMap = stats.info;			
			
			document.getElementById("logfile").innerText = " > "+ stats["log"].join("\r\n > ");

			
			var charts = stats["charts"];
			var newTrades = [];
			for (var n in charts) {
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
				var s = orders[o.symb];
				var sz = o.size;
				var ch =  charts[o.symb];
				var inv = infoMap[o.symb].inverted;
				var last;
				if (!ch || !ch.length) {
					last = {pl:0,price:o.price,pos:0};
				} else {
					 last = ch[ch.length-1];
				}
				var dir = o.dir < 0?"sell":"buy";
				var gain = ((inv?1.0/o.price:o.price) - (inv?1.0/last.price:last.price))* (inv?-1:1)*last.pos;
				var newpl = last.pl + gain;
				var newpos = last.pos + sz;
				s.push({
					price: o.price,
					achg: sz,
					pl: newpl,
					pos: newpos,
					label: "",
					gain:gain,
					class: dir,
				})
				ranges[o.symb][dir] = [o.price,o.size];
				ranges[o.symb].pos = [last.pos,infoMap[o.symb].asset];
			}) 
			
			for (var sm in stats.prices) {
				var s = orders[sm];
				var ch =  charts[sm];
				var inv = infoMap[sm].inverted;
				var last;
				if (!ch || !ch.length) {
					last = {pl:0,price:stats.prices[sm],pos:0,app:0,norm:0,nacum:0};
				} else {
					 last = ch[ch.length-1];
				}
				if (!s) orders[o.symb] = s = [];				
				var gain = ((inv?1.0/stats.prices[sm]:stats.prices[sm])- (inv?1.0/last.price:last.price))* (inv?-1:1)*last.pos;
				s.push({
					price: stats.prices[sm],
					pl: last.pl + gain,
					label: "",
					class: "last",
				})
				ranges[sm]["last"] = [stats.prices[sm],""];
			
			}

			var sums = calculate_sums(charts,infoMap);
			
			localStorage["mmbot_time"] = Date.now();
			
			chart_interval = stats.interval;
			redraw = function() {

				var fld = location.hash;
				if (fld) fld = decodeURIComponent(fld.substr(1));
				else fld = "+summary";
				
				selector.value = fld;

				if (fld == "+summary") {
					setMode(4);
					for (var k in charts) {
						appendSummary("_"+k,infoMap[k], charts[k], ranges[k], stats.misc[k]);
					}
					for (var k in sums) {
						appendSummary("_"+k,{"title":k,"asset":"","currency":k}, sums[k]);
					}
					updateLastEventsAll(charts);
					
				} else if (fld == "+list") {
					setMode(5);
					for (var k in charts) {
						appendList("_"+k,infoMap[k], ranges[k], stats.misc[k]);
					}
					updateLastEventsAll(charts);
				} else if (fld.startsWith("!")) {
					setMode(1);
					var pair = fld.substr(1);
					appendSummary("_"+k,infoMap[pair], charts[pair], ranges[pair], stats.misc[pair],true);
					for (var k in cats) {
						appendChart("!"+k, {title:cats[k]}, charts[pair], k, orders[pair], stats.misc[k]);
					}
					updateLastEvents(charts[pair],pair);
					
				} else if (fld.startsWith("+")) {
					setMode(2);
					fld = fld.substr(1);
					for (var k in sums) {
						appendChart(k,{"title":k}, sums[k], fld);
					}
					updateLastEventsAll(charts);			
				} else {
					setMode(3);
					for (var k in charts) {
						appendChart(k,infoMap[k], charts[k], fld,  orders[k],ranges[k], stats.misc[k]);
					}
					updateLastEventsAll(charts);
				}
				if (lastField) {
					selector.value = lastField;
					lastField = null;
					redraw();
				}
				
			}
			
			
			redraw();

			indicator.classList.remove("fetching");
			indicator.classList.add("online");
			var now = Date.now();
			var df;
			if (timediff === null) {
				df = timediff = now -stats.time;
			} else {
				var reqtime = now - timediff;
				df = reqtime - stats.time;
				if (df < 0) timediff = now -stats.time;
			}
			outage.classList.toggle("detected",df > 100000); 

		},function(e) {
			indicator.classList.remove("fetching");
			console.error(e);
		});
	}
	
	var base_interval=1200000;

	function new_svg_el(name, attrs, childOf) {
		var elem = document.createElementNS("http://www.w3.org/2000/svg", name);
		if (attrs) {for (var i in attrs) {
			elem.setAttributeNS(null,i, attrs[i]);
		}}
		if (childOf) childOf.appendChild(elem);
		return elem;
	}

	

	function drawChart(elem, chart, fld, lines) {
		var now = new Date();
		var bday = beginOfDay(now);		
		var skiphours = Math.floor((now - bday)/(base_interval));
				
		elem.innerText = "";
		
		var daystep = 24*3600000/base_interval;
		var step = 2*864000000/chart_interval;
		var activewidth=step*chart_interval/base_interval;
		var dattextstep = Math.ceil(120/(daystep*step));
		var activeheight = (activewidth/3)|0;
		var minmax = chart.concat(lines?lines:[]).reduce(function(c,x) {
			if (c.min > x[fld]) c.min = x[fld];
			if (c.max < x[fld]) c.max = x[fld];
			return c;
		},{min:chart[0][fld],max:chart[0][fld]});
		minmax.sz = minmax.max - minmax.min;
		minmax.min =  minmax.min - minmax.sz*0.05;
		minmax.max =  minmax.max + minmax.sz*0.05;
		var priceStep = activeheight/(minmax.max-minmax.min);
		var axis = 2;
		var label = 20;
		var rowstep = Math.pow(10,Math.floor(Math.log10((minmax.max-minmax.min)/3)));
		var rowbeg= Math.floor(minmax.min/rowstep);
		var rowend= Math.floor(minmax.max/rowstep);

		var svg = new_svg_el("svg",{viewBox:"0 0 "+(activewidth+axis)+" "+(activeheight+axis+label)},elem);
		new_svg_el("line",{x1:axis, y1:0,x2:axis,y2:activeheight+axis,class:"lineaxis"},svg);
		new_svg_el("line",{x1:0, y1:activeheight+1,x2:activewidth+axis,y2:activeheight+1,class:"lineaxis"},svg);
		var cnt = chart.length;
		function map_y(p) {
			return activeheight-(p-minmax.min)*priceStep;
		}
		function map_x(x) {
			return x*step+axis;
		}

		if (!isFinite(rowbeg) || !isFinite(rowend)) return;
		
		for (var i = rowbeg; i <=rowend; i++) {
			var v = i*rowstep;
			var maj = Math.abs(v)<rowstep/2;
			var y = map_y(v);
			new_svg_el("line",{x1:0,y1:y,x2:activewidth+axis,y2:y,class:maj?"majoraxe":"minoraxe"},svg);
			new_svg_el("text",{x:axis+2,y:y,class:"textaxis"},svg).appendChild(document.createTextNode(adjNum(i*rowstep)));
		}
		
		var xtmpos = activewidth/step-skiphours;
		while (xtmpos > 0) {
			var xtm = map_x(xtmpos);
			new_svg_el("line",{x1:xtm,y1:0,x2:xtm,y2:activeheight,class:"minoraxe"},svg);
			xtmpos-=daystep;
		}
		xtmpos = activewidth/step-skiphours;
		while (xtmpos > 0) {
			var xtm = map_x(xtmpos);
			new_svg_el("text",{x:xtm,y:activeheight,class:"textaxisx"},svg).appendChild(document.createTextNode(bday.toLocaleDateString()));
			bday.setDate(bday.getDate()-dattextstep);
			new_svg_el("line",{x1:xtm,y1:activeheight-5,x2:xtm,y2:activeheight,class:"majoraxe"},svg);
			xtmpos-=daystep*dattextstep;
		}
		var tmstart=(now/base_interval-activewidth/step)
		for (var i = 0; i <cnt-1; i++) {
			var pos = "stdline";
			var x1 = map_x(chart[i].time/base_interval-tmstart);
			var x2 = map_x(chart[i+1].time/base_interval-tmstart);
			var y1 = map_y(chart[i][fld]);
			var y2 = map_y(chart[i+1][fld]);
			new_svg_el("line",{x1:x1,y1:y1,x2:x2,y2:y2,class:pos},svg);
		}
		for (var i = 0; i <cnt; i++) if (chart[i].achg) {
			var x1 = map_x(chart[i].time/base_interval-tmstart);
			var y1 = map_y(chart[i][fld]);
			var man = chart[i].man;
			var marker = "marker "+(chart[i].achg<0?"sell":"buy") 
			if (man) {
				new_svg_el("line",{x1:x1-5,y1:y1-5,x2:x1+5,y2:y1+5,class:marker},svg);
				new_svg_el("line",{x1:x1+5,y1:y1-5,x2:x1-5,y2:y1+5,class:marker},svg);
			} else {				
				new_svg_el("circle",{cx:x1,cy:y1,r:4,class:marker},svg);
			}
		}
		
		if (lines === undefined) {
			lines=[];
		}
		var l = {};
		l[fld] = chart[chart.length-1][fld];
		lines.unshift(l);
		l.label="";
		l.class="current"
		
		if (Array.isArray(lines)) {
			lines.forEach(function(x) {
				var v = x[fld];
				if (v) {
					var y = map_y(v);
					new_svg_el("line",{x1:0,y1:y,x2:activewidth+axis,y2:y,class:"orderline "+x.class},svg);
					new_svg_el("text",{x:axis,y:y,class:"textorderline "+x.class},svg).appendChild(document.createTextNode(x.label + " "+adjNum(v)));
				}
			});
		}
		
	}
	
	
	window.addEventListener("hashchange", function() {
		table_rows = new WeakMap();
		redraw();
	}) 
	
	selector.addEventListener("change",function(){
		location.hash = "#"+encodeURIComponent(selector.value);
	});
	home.addEventListener("click",function() {
		location.hash = "#";
	})
	
	
	var logo = document.getElementById("logo");
	
	function removeLogo() {
		update().then(function() {
			logo.parentNode.removeChild(logo);
			setInterval(update,60000);
		});
	}
	
	if (Date.now() - (localStorage["mmbot_time"] || 0) < 300*1000) {
		removeLogo();
	}  else {
		logo.hidden = false;
		setTimeout(removeLogo, 3000);
	}
	
	opencloselog.addEventListener("click",function() {
		var v = document.getElementById("logfile");
		v.hidden = !v.hidden;
	})
	
	init_calculator();

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

function pow2(x) {
	return x*x;
}

function init_calculator() {
	"use strict";
	
	
	var calc = document.getElementById("calculator");
	var menu = calc.querySelector("menu");
	var menu_items = menu.querySelectorAll("li");
	var menu_fn = function() {
		Array.prototype.forEach.call(menu_items, function(x) {
			x.classList.remove("selected");
			document.getElementById(x.dataset.name).hidden = true;
		});
		this.classList.add("selected");
		document.getElementById(this.dataset.name).hidden = false;
	}
	Array.prototype.forEach.call(menu_items, function(x) {
		x.addEventListener("click",menu_fn);
	});
	
	function update_ranges(f,min,max,boost) {
		f.querySelector(".range_max").innerText = adjNum(max);
		f.querySelector(".range_min").innerText = adjNum(min);
		f.querySelector(".boost").innerText = adjNum(boost);				
	}
	
	var order_form = document.getElementById("form_order");
	var order_form_calc = function() {
		var p = parseFloat(order_form.p.value);
		var a = parseFloat(order_form.a.value);
		var n = parseFloat(order_form.n.value);
		
		var sz = a*(Math.sqrt(p/n) - 1);
		var calcvol = a*(Math.sqrt(p*n)-p);
		var actvol = -sz * n;
		var extra = actvol - calcvol;
		var accumsz = sz + extra/n;
		
		var otype = order_form.querySelector(".order_type");
		otype.classList.toggle("buy", sz > 0);
		otype.classList.toggle("sell", sz < 0);
		
		order_form.querySelector(".order_size.acc0").innerText = adjNum(Math.abs(sz));
		order_form.querySelector(".order_size.acc100").innerText = adjNum(Math.abs(accumsz));
		order_form.querySelector(".norm_profit").innerText = adjNum(extra);
		order_form.querySelector(".cash_flow").innerText = adjNum(actvol);
	}
	Array.prototype.forEach.call(order_form.elements,function(x){
		x.addEventListener("input", order_form_calc);
	});
	
	var range_form = document.getElementById("form_range_exchange");
	var range_form_calc = function() {
		var aa = parseFloat(this.aa.value);
		var ea = parseFloat(this.ea.value);
		var am = parseFloat(this.am.value);
		var p = parseFloat(this.p.value);
		var ab = aa+ea;
		var value = ab * p;
		var max_price = pow2((ab * Math.sqrt(p))/ea);
		var S = value - am;
		var min_price = S<=0?0:pow2(S/(ab*Math.sqrt(p)));
		var boost = ab/aa;
		
		update_ranges(this, min_price, max_price, boost)
	}.bind(range_form);
	Array.prototype.forEach.call(range_form.elements,function(x){
		x.addEventListener("input", range_form_calc);
	});

	var range_form_margin = document.getElementById("form_range_margin");
	var range_form_margin_calc = function() {
		var aa = parseFloat(this.aa.value);
		var ea = parseFloat(this.ea.value);
		var am = parseFloat(this.am.value);
		var p = parseFloat(this.p.value);
		var l = parseFloat(this.l.value);
		var ab = aa+ea;
		var colateral = am* (1 - 1 / l);
		var min_price = (ab*p - 2*Math.sqrt(ab*colateral*p) + colateral)/ab;
		var max_price = (ab*p + 2*Math.sqrt(ab*colateral*p) + colateral)/ab;
		var boost = ab * p / colateral;
		
		update_ranges(this, min_price, max_price, boost)
	}.bind(range_form_margin);
	
	Array.prototype.forEach.call(range_form_margin.elements,function(x){
		x.addEventListener("input", range_form_margin_calc);
	});

	var form_range_futures = document.getElementById("form_range_futures");
	var form_range_futures_calc = function() {
		var aa = parseFloat(this.aa.value);
		var ea = parseFloat(this.ea.value);
		var am = parseFloat(this.am.value);
		var p = 1.0/parseFloat(this.p.value);
		var l = parseFloat(this.l.value);
		var ab = aa+ea;
		var colateral = am* (1 - 1 / l);
		var max_price = 1.0/((ab*p - 2*Math.sqrt(ab*colateral*p) + colateral)/ab);
		var min_price = 1.0/((ab*p + 2*Math.sqrt(ab*colateral*p) + colateral)/ab);
		var boost = ab * p / colateral;
		
		update_ranges(this, min_price, max_price, boost)
	}.bind(form_range_futures);
	
	Array.prototype.forEach.call(form_range_futures.elements,function(x){
		x.addEventListener("input", form_range_futures_calc);
	});
	
	calc.querySelector(".close_butt").addEventListener("click",function() {
		calc.classList.toggle("fade",true);
		setTimeout(function(){
			calc.hidden = true;			
		},500);		
	});
	
	document.getElementById("calculator_icon").addEventListener("click",function(){
		calc.classList.toggle("fade",true);
		setTimeout(function(){
			calc.hidden = false;			
		},5);
		setTimeout(function(){
			calc.classList.toggle("fade",false);			
		},10);
	});
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
		var hd = document.getElementById("hide_donation");
		hd.addEventListener("change", function() {
			localStorage["donation_hidden"] = hd.checked?hd.value:"";
		});
		hd.checked = localStorage["donation_hidden"] === hd.value;
	}
}
function close_donate() {
	var w = document.getElementById("donate_window");
	w.classList.remove("shown");
}