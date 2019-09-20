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
	else if (an >= 1) return n.toFixed(2);
	else if (an > 0.0001) return n.toFixed(6);
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
