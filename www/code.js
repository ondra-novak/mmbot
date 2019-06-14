function beginOfDay(dt) {
	return new Date(dt.getFullYear(), dt.getMonth(), dt.getDate());
}


function fetch_json(file) {
	"use strict";

	return fetch(file).then(function(req) {
		return req.json();
	});
	
}


function app_start(){
	
	"use strict";
	try {
		var lastField = localStorage["markettrader_lastfield"];
	} catch (e) {

	}

	var chartlist = {};
	var indicator = document.getElementById("indicator");
	var eventMap = new WeakMap();
		
	var redraw = function() {};

	var selector = document.getElementById("selchart");
	var outage = document.getElementById("outage");
	var home = document.getElementById("home");
	var options = [];
	var lastEvents = document.getElementById("lastevents")
	var selMode = 0;
	var chart_interval=1000;
	var curExport = null;
	var cats = {}
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
	
	function fillInfo(curchart, title, link, ranges, emulated) {
		var elem_title = curchart.querySelector("[data-name=title]");
		elem_title.classList.toggle("emulated",!!emulated);
		elem_title.innerText = title;
		var info = curchart.querySelector("[data-name=info]");
		if (ranges) {
			info.hidden = false;
			while (info.firstChild) info.removeChild(info.firstChild);
			["buy","last","sell", "pos"].forEach(function(n) {
				if (ranges[n]) {
					var r = ranges[n];
					var elem = document.createElement("div");
					elem.classList.add(n);
					info.appendChild(elem);					
					var e = document.createElement("div")
					e.classList.add("price");
					e.innerText = adjNum(r[0]);
					elem.appendChild(e);
					e = document.createElement("div")
					e.classList.add("size");
					e.innerText = adjNum(r[1]);
					elem.appendChild(e);
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
	
	function appendChart(id,  info, data, fld,  orders, ranges) {
		try {
			var curchart = createChart(id, "chart");
			var elem_chart = curchart.querySelector("[data-name=chart]");
			drawChart(elem_chart, data, fld, orders);
			fillInfo(curchart, info.title,id,  ranges, info.emulated);
		} catch (e) {}
	}
	
	function appendSummary(id, info, data, ranges) {
	//	try {
			var curchart = createChart(id, "summary");
			fillInfo(curchart,  info.title,id, ranges, info.emulated);
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
				 a.click();
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
			
			
			var data = [
				dr.toLocaleDateString(),
				dr.toLocaleTimeString(),
				r.ident?infoMap[r.ident].title:"",
				r.achg < 0?"↘":"↗",
				Math.abs(r.achg),
				r.price,
				r.volume,
				r.normDiff
			]
			tr.classList.toggle("sell", r.achg<0);
			tr.classList.toggle("buy", r.achg>0);
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
			
			var hold_tm = null;
			
			function hold_handler() {
				if (!hold_tm) {
					hold_tm = setTimeout(function() {
						hold_tm = null;
						window.prompt("Please copy trade-id", r.ident + " " + r.id);						
					},2000);							
				}
			}
			tr.addEventListener("mousedown", hold_handler);
			tr.addEventListener("touchstart", hold_handler);
			tr.addEventListener("touchend", function() {
				if (hold_tm) {
					clearTimeout(hold_tm);
					hold_tm = null;
				}				
			})
			tr.addEventListener("click", function() {
				if (hold_tm) {
					clearTimeout(hold_tm);
					hold_tm = null;
				}
				location.hash = "!"+encodeURIComponent(r.ident);
			});
			
			
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
	
	function adjChartData(data, ident) {
		var lastNorm = 0;
		var lastPL = 0;
		var lastPos = 0;
		var lastPrice = 0;
		var lastRel = 0;
		data.forEach(function(r) {
			r.ident = ident;
			r.normDiff = r.norm - lastNorm;
			r.plDiff = r.pl - lastPL;
			r.priceDiff = r.price - lastPrice;
			r.vol = Math.abs(r.achg*r.price);
			r.relDiff = r.rel - lastRel;
			lastNorm = r.norm;
			lastPL = r.pl;
			lastPos = r.pos;
			lastPrice = r.price;
			lastRel = r.rel;
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
				var vol = Math.abs(r.achg * r.price);
				if (s.length) {
					s.push({
						time:r.time,
						pl:s[s.length-1].pl+r.plDiff,
						norm:s[s.length-1].norm+r.normDiff,
						normDiff:r.normDiff,
						plDiff:r.plDiff,
						vol: vol,
						rel: s[s.length-1].rel+ r.relDiff,
						relDiff: r.relDiff
					});
				} else {
					s.push({
						time:r.time,
						pl:r.plDiff,
						plDiff:r.plDiff,
						norm:r.normDiff,
						normDiff:r.normDiff,
						vol: vol,
						rel: r.relDiff,
						relDiff: r.relDiff
					})
				}
				return s;
			},[]);
		}
		return sums;

	
	}
	
	function update() {
		
		indicator.classList.remove("online");
		indicator.classList.add("fetching");
		return fetch_json("report.json?r="+Date.now()).then((stats)=>{

			
			var orders = {};
			var ranges = {};

			infoMap = stats["info"];

			
			var charts = stats["charts"];
			for (var n in charts) {
				orders[n] = [];
				ranges[n] = {};
				adjChartData(charts[n], n);
				updateOptions(n, infoMap[n].title);
			}
			
			stats.orders.forEach(function(o) {
				var s = orders[o.symb];
				var sz = o.size;
				var ch =  charts[o.symb];
				var last;
				if (!ch || !ch.length) {
					last = {pl:0,price:o.price,pos:0};
				} else {
					 last = ch[ch.length-1];
				}
				var dir = o.dir < 0?"sell":"buy";
				var newpl = last.pl + (o.price - last.price)* last.pos;
				var newpos = last.pos + sz;
				s.push({
					price: o.price,
					achg: sz,
					pl: newpl,
					pos: newpos,
					label: "",
					gain:(o.price - last.price)* last.pos,
					class: dir,
				})
				ranges[o.symb][dir] = [o.price,o.size];
				ranges[o.symb].pos = [last.pos,infoMap[o.symb].asset];
			}) 
			
			for (var sm in stats.prices) {
				var s = orders[sm];
				var ch =  charts[sm];
				var last;
				if (!ch || !ch.length) {
					last = {pl:0,price:stats.prices[sm],pos:0};
				} else {
					 last = ch[ch.length-1];
				}
				if (!s) orders[o.symb] = s = [];				
				s.push({
					price: stats.prices[sm],
					pl: last.pl + (stats.prices[sm]- last.price)* last.pos,
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
						appendSummary("_"+k,infoMap[k], charts[k], ranges[k]);
					}
					for (var k in sums) {
						appendSummary("_"+k,{"title":k,"asset":"","currency":k}, sums[k]);
					}
					updateLastEventsAll(charts);
				} else if (fld.startsWith("!")) {
					setMode(1);
					var pair = fld.substr(1);
					for (var k in cats) {
						appendChart("!"+k, {title:cats[k]}, charts[pair], k, orders[pair]);
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
						appendChart(k,infoMap[k], charts[k], fld,  orders[k],ranges[k]);
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

		},function() {
			indicator.classList.remove("fetching");
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

	
	function adjNum(n) {
		if (typeof n != "number") return n;		
		var an = Math.abs(n);
		if (an > 9999) return n.toFixed(0);
		else if (an > 1) return n.toFixed(2);
		else if (an > 0.0001) return n.toFixed(6);
		else {
			var s = n.toFixed(8);
			if (s == "0.00000000") return "0.00";
			return s;
		}
	}

	function drawChart(elem, chart, fld, lines) {
		var now = new Date();
		var bday = beginOfDay(now);		
		var skiphours = Math.floor((now - bday)/(base_interval));
				
		elem.innerText = "";
		
		var step = 2;
		var daystep = 24*3600000/base_interval;
		var activewidth=step*chart_interval/base_interval;
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
			bday.setDate(bday.getDate()-2);
			xtmpos-=daystep*2;
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
			new_svg_el("circle",{cx:x1,cy:y1,r:4,class:"marker "+(chart[i].achg<0?"sell":"buy")},svg);
		}
		
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