function beginOfDay(dt) {
	return new Date(dt.getFullYear(), dt.getMonth(), dt.getDate());
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

function pow2(x) {
	return x*x;
}

function setElNumber(el, numb) {
	let pos = false;
	let neg = false;
	let zero = false;
	let na = false;
	if (!isNaN(numb)) {
		pos = numb > 0;
		neg = numb < 0;
		zero = numb == 0;
	} else {
		na = true;
	}
	el.classList.toggle("pos",pos);
	el.classList.toggle("neg",neg);
	el.classList.toggle("zero",zero);
	el.classList.toggle("na",na);
	el.classList.add("colornumber");
	el.textContent = adjNum(numb);
}

function binarySearch(array, predicate) {
	let l = 0;
	let h = array.length;
	while (l<h) {
		let m = Math.floor((l+h)/2);
		let dir = predicate(array[m]);
		if (dir > 0) {
			h = m;
		} else if (dir < 0) {
			l = m+1;
		} else {
			return array[m];
		}
	}
	return l;
}
