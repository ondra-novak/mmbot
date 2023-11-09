"use strict";


function beginOfDay(dt) {
	return new Date(dt.getFullYear(), dt.getMonth(), dt.getDate());
}

 
function fetch_json(file, opt) {
	return fetch(file, opt).then(function(req) {
		if (req.status>299 || req.status < 200) throw req;
		return req.json();
	});
}

function invPrice(v, inv) {
	return inv?1/v:v;
}
function invSize(v, inv) {
	return (inv?-1:1)*v;
}

function adjNum(n, decimals) {
	if (typeof n != "number") return n;	
	if (isNaN(n)) return "---";
	if (!isFinite(n)) { 
		if (s < 0) return "-∞";
		else return "∞";
	}
	if (decimals !== undefined) return n.toFixed(decimals);
	var an = Math.abs(n);
	if (an >= 100000) return n.toFixed(0);
	else if (an >= 100) return n.toFixed(2);
	else if (an >= 1) return n.toFixed(4);
	else if (an > 0.0001) return n.toFixed(6);
	else {
		var s = (n*1000000).toFixed(3);
		if (s == "0.000") return s;
		return s+"µ";
	}
}

function adjNumN(n) {
	if (typeof n != "number") return n;
	var an = Math.abs(n);
	if (an >= 100000) return n.toFixed(0);
	else if (an >= 100) return n.toFixed(2);
	else if (an >= 1) return n.toFixed(4);
	else if (an > 0.0001) return n.toFixed(6);
	else {
		if (an === 0) return an;
		else {
			var s = n.toFixed(10);	
			while (s[s.length-1] == '0') {
				s = s.substr(0,s.length-1);
			}
			return s;
		}
	}
}

function adjNumShort(n) {
	if (typeof n != "number") return n;	
	if (isNaN(n)) return "---";
	if (!isFinite(n)) { 
		if (s < 0) return "-∞";
		else return "∞";
	}
	var an = Math.abs(n);
	if (an >= 10000000) return (n/1000000).toFixed(2).replace('.',"M");
	else if (an >= 10000) return (n/1000).toFixed(2).replace('.',"k");
	else if (an > 0.0001) {
		var s = n.toFixed(6);
		while (s[s.length-1] == '0') s = s.substr(0,s.length-1);
		if (s && s[s.length-1] == '.') s = s.substr(0,s.length-1);
		return s;
	}
	else {
		var s = (n*1000000).toFixed(3);
		if (s == "0.000") return s;
		return s+"µ";
	}
}

function new_svg_el(name, attrs, childOf) {
	var elem = document.createElementNS("http://www.w3.org/2000/svg", name);
	if (attrs) {for (var i in attrs) {
		elem.setAttributeNS(null,i, attrs[i]);
	}}
	if (childOf) childOf.appendChild(elem);
	return elem;
}

function pow2(x) {
	return x*x;
}

function calculateBacktest(params) {

var data = params.data;
var ea = params.external_assets;
var ga = params.sliding_pos;
var fb = params.fade;
var mlt = params.multiplicator || 1;
var mos = params.min_order_size || 0;
var inv = params.invert || false;
var et = params.expected_trend || 0;
var pos = (inv?-1:1)*params.start_pos || 0;
var mxs = params.max_order_size || 0;
var max_pos = params.max_pos || 0;

if (data.length == 0) return {
	pl:0,
	pln: 0,
	pos: 0,
	mdd:0,
	maxpos:0,
	eq:0,
	chart:[]
};
var pp = inv?1/data[0].price:data[0].price;
var eq = pp*pow2((ea+pos)/ea);
var pl = 0;
var mdd = 0;
var mpos = 0;
var norm = 0;
var tframe = ga*3600000;
var tm = data[0].time;
var newchart = [];
var curet = 0;

data.forEach(function(x) {
	var p = inv?1/x.price:x.price;
    var dr = Math.sign(p - pp);
	var pldiff = pos * (p - pp);
	var tmdiff = x.time - tm;
	if (tmdiff > Math.abs(tframe)) tmdiff = Math.abs(tframe);
	var neq = pldiff*Math.sign(tframe)>0?eq + (p - eq) * (tmdiff/Math.abs(tframe)):eq;
	if (Math.abs(pos) > mpos) mpos = Math.abs(pos);			
	var nxpos = ea*Math.sqrt(eq*(1+curet)/p)-ea;
	var dpos = nxpos - pos ;
	var mult = (fb?(fb - Math.abs(nxpos))/fb:1);
	if (mult < 0.000001) mult = 0.000001
	if (pos * dpos < 0) {
		mult = 1;
	}
	curet = curet + (et - curet)*0.05;
	dpos = dpos * mult * mlt;
	if (mxs && dpos * dr  < -mxs) {
		dpos = -mxs * dr
	} 
	if (dpos * dr >= -mos) {
		if (mos <= 0) return;
		dpos = -mos * dr
	}
	if (params.step) dpos = Math.floor(dpos / params.step)*params.step;
	pos = pos + dpos;
	if (max_pos && Math.abs(pos) > max_pos) {
		pos = max_pos * Math.sign(pos);
		neq = p*pow2((ea+pos)/ea); 
	}
	pp = p;
	eq = neq;
	tm = x.time;
	pl = pl+pldiff;
	norm = pl+pos*(eq-Math.sqrt(p*eq));
	if (pl < mdd) mdd = pl;
	newchart.push({
		price:x.price,
		pl:pl,
		pos:pos,
		pln:norm,
		np:inv?1/eq:eq,
		time:x.time,
		achg:(inv?-1:1)*dpos
	});
});
if (inv) eq = 1/eq;

return {
	pl:pl,
	pln: norm,
	pos: (inv?-1:1)*pos,
	mdd:mdd,
	maxpos:mpos,
	eq:eq,
	chart:newchart
};
}


function initChart(chart_interval, ratio, base_interval, objret) {

	if (!ratio) ratio = 3;
	if (!base_interval) base_interval=1200000;




 return function (elem, chart, fld, lines, fld2) {
	"use strict";

	chart = chart.filter(function(x) {return fld in x});

	elem.innerText = "";
	if (chart.length == 0) return;
	
	var now = new Date(chart[chart.length-1].time);
	var bday = beginOfDay(now);		
	var skiphours = Math.floor((now - bday)/(base_interval));
			
	
	var daystep = 24*3600000/base_interval;
	var step = 2*864000000/chart_interval;
	var activewidth=step*chart_interval/base_interval;
	var dattextstep = Math.ceil(120/(daystep*step));
	var activeheight = (activewidth/ratio)|0;
	var minmax = chart.concat(lines?lines:[]).reduce(function(c,x) {
		var v = x[fld];
		if (isFinite(v)) {
			if (c.min > v) c.min = v;
			if (c.max < v) c.max = v;
		}
		return c;
	},{min:chart[0][fld],max:chart[0][fld]});
	minmax.sz = minmax.max - minmax.min;
	minmax.min =  minmax.min - minmax.sz*0.01;
	minmax.max =  minmax.max + minmax.sz*0.10;
	if (minmax.min == minmax.max) {minmax.min = -10; minmax.max = 10;}
	var priceStep = activeheight/(minmax.max-minmax.min);
	var axis = 2;
	var label = 20;
	var rowstep = Math.pow(10,Math.floor(Math.log10((minmax.max-minmax.min)/3)));
	var rowbeg= Math.floor(minmax.min/rowstep);
	var rowend= Math.floor(minmax.max/rowstep);	

	var svg = new_svg_el("svg",{viewBox:"0 0 "+(activewidth+axis+1)+" "+(activeheight+axis+label+1)},elem);
	var pathstr = "";	
	new_svg_el("line",{x1:axis, y1:0,x2:axis,y2:activeheight+axis,class:"lineaxis"},svg);
	new_svg_el("line",{x1:0, y1:activeheight+1,x2:activewidth+axis,y2:activeheight+1,class:"lineaxis"},svg);
	var cnt = chart.length;
	function map_y(p) {
		if (isFinite(p)) {
		    return activeheight-(p-minmax.min)*priceStep;
		} else {
			return activeheight-(minmax.min)*priceStep;;
		}
	}
	function map_x(x) {
		if (isFinite(x)) {
		    return x*step+axis;
		} else {
			return axis;
		}
	}

	if (!isFinite(rowbeg) || !isFinite(rowend)) return;

    var grp = new_svg_el("g",{},svg);
	
	for (var i = rowbeg; i <=rowend; i++) {
		var v = i*rowstep;
		var maj = Math.abs(v)<rowstep/2;
		var y = map_y(v);
		if (maj) {
		  new_svg_el("line",{x1:0,y1:y,x2:activewidth+axis,y2:y,class:"majoraxe"},svg);
		} else {
            pathstr += "M 0 "+y+"h "+(activewidth+axis)+" ";
        }
		new_svg_el("text",{x:axis+2,y:y,class:"textaxis"},svg).appendChild(document.createTextNode(adjNum(i*rowstep)));
	}
	var xtmpos = activewidth/step-skiphours;
	while (xtmpos > 0) {
		var xtm = map_x(xtmpos);
		pathstr += "M "+xtm+" 0 v "+activeheight+" ";
		xtmpos-=daystep;
	}
    new_svg_el("path",{d:pathstr,class:"minoraxe"}, grp);
    
    pathstr = "";

	xtmpos = activewidth/step-skiphours;
	while (xtmpos > 0) {
		var xtm = map_x(xtmpos);
		new_svg_el("text",{x:xtm,y:activeheight,class:"textaxisx"},svg).appendChild(document.createTextNode(bday.toLocaleDateString()));
		bday.setDate(bday.getDate()-dattextstep);
		pathstr += "M " + xtm + " " +  (activeheight-5) + " v -5 ";		
		xtmpos-=daystep*dattextstep;
	}
    new_svg_el("path",{d:pathstr,class:"majoraxe"}, svg);

	var tmstart=(now/base_interval-activewidth/step)
    pathstr = "";
	var nxt = false;
	for (var i = 0; i <cnt; i++) {
        var v = chart[i][fld];
        if (v === undefined) {
            nxt = false;            
        } else {
    		if (nxt) pathstr = pathstr + "L "; else pathstr = pathstr + "M "
    		nxt = true;
    		var x = map_x(chart[i].time/base_interval-tmstart);
    		var y = map_y(v);
    		pathstr = pathstr + x + " " + y + " ";    		
    	}
	}
	new_svg_el("path",{d:pathstr,class:"stdline"},svg);
	
	var args = Array.prototype.slice.call(arguments,4);
	args.forEach(function(fld2) {
		if (fld2) {
            var pathstr = "";
            var nxt = false;
			for (var i = 0; i <cnt; i++) {
                var v = chart[i][fld2];
                if (v === undefined) {
                    nxt = false;            
                } else {
                    if (nxt) pathstr = pathstr + "L "; else pathstr = pathstr + "M "
                    nxt = true;
                    var x = map_x(chart[i].time/base_interval-tmstart);
                    var y = map_y(v);
                    pathstr = pathstr + x + " " + y + " ";
                }
			}
            new_svg_el("path",{d:pathstr,class:"stdline2"},svg);
		}
	})
	
	
	var markers = [["sell",-1],["buy",1]]; 
    markers.forEach(function(m){
        var marker = "marker "+m[0];
        var side = m[1];
        var pathstr = "";
    	for (var i = 0; i <cnt; i++) if (chart[i].achg * side > 0) {
    		var x1 = map_x(chart[i].time/base_interval-tmstart);
    		var y1 = map_y(chart[i][fld]);
    		var man = chart[i].man;
    		if (man) {
                pathstr +="M " + (x1-5) +" " + (y1-5) +" l 10 10 " 
                       +"M " + (x1+5) +" " + (y1-5) +" l -10 10 "
    		} else {
                pathstr += "M " + (x1-4) + " " + (y1-4) + " h 8 v 8 h -8 v -8 ";    			
    		}    		
    	}
    	new_svg_el("path",{d:pathstr, class:marker},svg);
	});
	
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
	if (objret) {
	return {
		map_x: function(x) {
			var w1 = elem.getBoundingClientRect().width;
			var w2 = activewidth+axis;			
			return (((w2/w1*x - axis)/step)+tmstart)*base_interval;
		},
		map_y: function(y) {
			var h1 = elem.getBoundingClientRect().height;
			var h2 = activeheight+axis+label;
			return ((activeheight - h2/h1*y)/priceStep)+minmax.min;
		},
		point_x: function(time) {
			var w1 = elem.getBoundingClientRect().width;
			var w2 = activewidth+axis;			
			return map_x(time/base_interval-tmstart)*w1/w2;
		},
		point_y: function(val) {
			var h1 = elem.getBoundingClientRect().height;
			var h2 = activeheight+axis+label;
			return map_y(val)*h1/h2;
		},
		box: function() {
			return elem.getBoundingClientRect();
		},
		msin: function(clientX, clientY) {
			var b = elem.getBoundingClientRect();
			return clientX >= b.left && clientX <= b.right &&
			        clientY >= b.top && clientY <= b.bottom;
		},
		symb: function() {
			return fld;
		},
		aspect: function(x) {
			svg.setAttribute("preserveAspectRatio",x);
		}
		
	}} else {
		return;
	}
	
};
}
function mergeArrays(array1, array2, cmp, fn) {
	var pos1 = 0;
	var pos2 = 0;
	var cnt1 = array1.length;
	var cnt2 = array2.length;
	var last1 = null;
	var last2 = null;
	var res = [];
	while (pos1 < cnt1 && pos2 < cnt2) {
		var c = cmp(array1[pos1], array2[pos2]);
		if (c < 0) {
			last1 = array1[pos1];
			res.push(fn(0,last1, last2, array2[pos2]));			
			pos1++;
		} else if (c>0) {
			last2 = array2[pos2];
			res.push(fn(1,last2, last1, array1[pos1]));
			pos2++;
		} else {
			last1 = array1[pos1];
			last2 = array2[pos2];
			res.push(fn(0,last1, last2, last2));
			pos1++;
			pos2++;			
		}
	}
	while (pos1 < cnt1) {
		last1 = array1[pos1];
		res.push(fn(0,last1, last2, null));
		pos1++;
	}
	while (pos2 < cnt2) {
		last2 = array2[pos2];
		res.push(fn(1,last2, last1, null));
		pos2++;
	}
	return res;
}

function interpolate(beg, end, cur, a, b) {
	if (beg == end) return (a+b)/2;
	var n = (cur - beg)/(end - beg);
	return a+(b-a)*n;
}

function copyFileToTextArea(file, textarea) {
    var reader = new FileReader();
    reader.onload = function() {
        textarea.value = reader.result;
    }
    reader.readAsText(file);
}


function formBuilder(format) {
    var files = [];
	var items = format.map(function(itm) {
		var el;
		var lb = {
	        	tag:"span",
	        	text: itm.label || ""
	        };
		
		switch (itm.type) {
		case "string": el ={tag:"input",
							attrs: {
								type:"text",
								value:itm.default || ""
							}};break;
		case "number": el ={tag:"input",
				attrs: {
					type:"number",
					step:"any",
					value:itm.default
				}};break;
        case "file": el = {
                    tag: "span",
                    content:[{
                            tag:"input",
                            attrs: {
                                "data-name":itm.name+"_file",
                                "type":"file",
                                "onchange":"copyFileToTextArea(this.files[0], this.nextElementSibling);"
                            }
                        },{
                            tag:"textarea",
                            attrs:{
                                "data-name":itm.name+"_content",
                                "hidden":"hidden"
                            }
                            
                        }]
                    };            
                break;
		case "textarea": el ={tag:"textarea",
				text:itm.default || "",
				attrs: {
					rows:itm.rows || "5",
					
				}};break;
		case "enum": el = {tag:"select",
						   attrs:{},
						   content: Object.keys(itm.options).map(function(k){
							   var attrs = {};
							   if (k == itm.default) attrs.selected = "selected";
							   attrs.value = k;
							   return {tag:"option",
								   attrs: attrs,
								   text: itm.options[k]

							   }
						   })};
						   break;
					
		case "label":
			return {
				tag:"p",
				text:itm.label,
				attrs: {
					style: "text-align: justify; margin: 1em 2em",					
				}
			};
		case "rotext":
			el ={tag:"span",
				 text:itm.default || "",
				 attrs:{},
			};break;
		case "link":
			el ={tag:"a",
				 text:itm.default || "",
				 attrs:{
				 	href:itm.href,
				 	target:"bybit_window"
				 },
			};break;
		case "hr":
			return {
				tag:"hr",
			};
		case "header":
			return {
				tag:"h3",
				text:itm.label
			};
		default:
			el = {tag:"span",text:"unknown: "+itm.type};
			break;
		}
		if (el.attrs && itm.attrs) Object.assign(el.attrs, itm.attrs);
		if (el.attrs && itm.name) el.attrs["data-name"]=itm.name;
		return {
			tag:"label",
			content:[lb, el]
		}
	});
	var w = TemplateJS.View.fromTemplate({tag:"x-form",content:items});
	function dialogRules() {
		var d = w.readData();
		format.forEach(function(itm){
			if (itm.showif) {
				var show = false;
				for (var k in itm.showif) {
					show = show || (itm.showif[k] && itm.showif[k].indexOf(d[k]) != -1);
				}
				w.forEachElement(itm.name, function(x) {
					var p = x.parentNode;
					p.hidden = !show;
				});
			}
		})
	}
	w.setData(format.reduce(function(data,itm) {
		if (itm.name) {
			data[itm.name] = {"!change": dialogRules};
		} 
		return data;
	},{}));
	dialogRules();
	return w;
}

function adjNumBuySell(x,d) {
	return {
		"value":adjNum(x,d),
		"classList":{
			"buy":x>0,
			"sell":x<0}
	};
}

function doDownlaodFile(data,name,content_type) {
	var blob = new Blob(
		    [ data ],
		    {
		        type : content_type
		    });
	 var downloadUrl = URL.createObjectURL( blob );
	 var a = document.createElement("A");
	 a.setAttribute("href", downloadUrl);
	 a.setAttribute("download",name);
	 a.setAttribute("target","_blank");
	 document.body.appendChild(a);
	 a.click();
	 document.body.removeChild(a);
	 URL.revokeObjectURL(downloadUrl);
}

function compareObjects(a, b) {
	if (typeof a != typeof b) return false;
	var t = typeof a;
	if (t == "object") {
		if (a === null) return b === null;
		if (Array.isArray(a)) {
			if (!Array.isArray(b)) return false;
			if (a.length != b.length) return false;
			var l = a.length;
			for (var i = 0; i < l; i++) {
				if (!compareObjects(a[i],b[i])) return false;
			}
			return true;
		}
		if (Array.isArray(b)) return false;
		for (var n in a) {
			if ((!b.hasOwnProperty(n) && b[n]!==undefined) || !compareObjects(a[n],b[n])) return false;
		}
		for (var n in b) {
			if ((!a.hasOwnProperty(n)) && a[n]!==undefined) return false;
		}
		return true;
	}
	if (t == "number") {
		var sum = Math.abs(a)+Math.abs(b);
		return Math.abs(a-b)<=sum*1e-8;
	}
	return a === b;	
}

function createDiff(a,b) {
	var t = typeof b;
	if (t == "object") {
		if (b === null) return a === null?undefined:b
		if (Array.isArray(b)) {
			if (!Array.isArray(a)) return b;
			if (a.length != b.length) return b;
			var l = a.length;
			for (var i = 0; i < l; i++) {
				if (!compareObjects(a[i],b[i])) return b;
			}
			return undefined;
		}
		if (typeof a !="object" || Array.isArray(a)) a = {};
		var out = {};
		var anyout = false;
		var empty = true;
		for (var n in b) {
			empty = false;
			var va = a[n];
			var vb = b[n];
			var df = createDiff(va,vb);
			if (df !== undefined) {
				out[n] =df;
				anyout = true;
			}			
		} 
		for (var n in a) {			
			if (!b.hasOwnProperty(n)) {
				out[n] = {}; //empty object means delete command
				anyout = true;
			}
		}
		if (anyout) return out;
		else if (empty) return {"~~~":{}};  //means - erase "~~~" however it also creates empty object
		return undefined;
	}
	return compareObjects(a,b)?undefined:b;	
};

function XYChart(width, height) {
	this.width = width;
	this.height = height;
	this.elem = new_svg_el("svg",{viewBox:"0 0 "+width+" "+height});
	this.range = [0,0,width,height]; 
	this.mapx = function(x) {return x;}	
	this.mapy = function(x) {return y;}	
};

XYChart.prototype.initRange = function(data) {
	var range = data.reduce(function(a,b){
		if (a[0]>b[0]) a[0] = b[0];
		if (a[1]>b[1]) a[1] = b[1];
		if (a[2]<b[0]) a[2] = b[0];
		if (a[3]<b[1]) a[3] = b[1];
		a[4]+=b[1];
		a[5]++;
		return a;
	},[data[0][0],data[0][1],data[0][0],data[0][1],data[0][1],0]);
	var avg = range[4]/range[5];
	var width = range[2]-range[0];
	var height = range[3]-range[1];
	if (width == 0) width = this.width;
	if (height == 0) height = avg;
	if (height == 0) height = this.height;
	range[0]-=width*0.05;
	range[2]+=width*0.05;
	range[1]-=height*0.05;
	range[3]+=height*0.05;
	var elem_width = this.width;
	var elem_height = this.height;
	width = range[2]-range[0];
	height = range[3]-range[1];
	this.mapx = function(x) {
		return (x-range[0])/width * elem_width; 
	};
	this.mapy = function(y) {
		return elem_height - (y-range[1])/height * elem_height; 
	};
	this.range = range;
};

XYChart.prototype.drawLines = function(data, cls) {
	data.reduce(function(a,b){
		new_svg_el("line",{
			x1:this.mapx(a[0]),
			y1:this.mapy(a[1]),
			x2:this.mapx(b[0]),
			y2:this.mapy(b[1])
			,class:cls
		},this.elem);
		return b;
	}.bind(this))
};

XYChart.prototype.drawArea = function(data, cls) {
	data.reduce(function(a,b){
		new_svg_el("polygon",{
			points:this.mapx(a[0])+","+this.mapy(a[1])+" "+
			      this.mapx(b[0])+","+this.mapy(b[1])+" "+
			      this.mapx(b[0])+","+this.mapy(0)+" "+
			      this.mapx(a[0])+","+this.mapy(0)+" "
			,class:cls
		},this.elem);
		return b;
	}.bind(this))
};

XYChart.prototype.calcStep = function(min, max) {
	var dist = (max - min);
	var mdist = dist/4;
	var step = Math.pow(10,Math.floor(Math.log10(mdist)));
	var cnt = Math.round(dist/step);
	if (cnt>8) {
		cnt = Math.round(dist/(step*2));
		if (cnt>8) {
			return step*5;
		} else {
			return step*2;
		}
	} else {
		return step;
	}
	
};

XYChart.prototype.drawAxes = function(cls) {
	var stepx = this.calcStep(this.range[0],this.range[2]);
	var stepy = this.calcStep(this.range[1],this.range[3]);
	var minx = Math.ceil(this.range[0]/stepx)*stepx;
	var miny = Math.ceil(this.range[1]/stepy)*stepy;
	var maxx = Math.ceil(this.range[2]/stepx)*stepx;
	var maxy = Math.ceil(this.range[3]/stepy)*stepy;
	for (var x = minx; x<maxx;x+=stepx) {
		new_svg_el("line",{
			x1:this.mapx(x),
			y1:0,
			x2:this.mapx(x),
			y2:this.height,
			class:cls+" vertical line"
		},this.elem);
		new_svg_el("text",{
			x:this.mapx(x),
			y:this.height,
			class:cls+" text bottom"
		},this.elem).textContent = adjNum(x)
	}	
	for (var y = miny; y<maxy;y+=stepy) {
		new_svg_el("line",{
			x1:0,
			y1:this.mapy(y),
			x2:this.width,
			y2:this.mapy(y),
			class:cls+" horizontal line"
		},this.elem);
		new_svg_el("text",{
			x:0,
			y:this.mapy(y),
			class:cls+" text left"
		},this.elem).textContent = adjNum(y)
	}	
};

XYChart.prototype.drawVLine = function(x, cls, text) {
	new_svg_el("line",{
		x1:this.mapx(x),
		y1:0,
		x2:this.mapx(x),
		y2:this.height,
		class:cls
	},this.elem);
	if (text) {
		new_svg_el("text",{
			x:this.mapx(x),
			y:0,
			class:cls
		},this.elem).textContent = text;
	}
};

XYChart.prototype.drawPoint = function(x,y, cls) {
	new_svg_el("circle",{
		cx:this.mapx(x),
		cy:this.mapy(y),
		r:2.0,
		class:cls
	},this.elem);
};
