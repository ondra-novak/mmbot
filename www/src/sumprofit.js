//@require source.js
//@require mko.js
//@namespace mmbot

class Sumprofit extends mmbot.Source {
	
	constructor() {
		super();	
	};
	
	clear() {
		this.symbol_map = new Map;
		this.wallet_map = new Map;						
	};
	
	set_source(src) {
		if (this.src) this.src.broadcast({type:"stop",obj:this});
		this.src = src;
		this.src.listen().then(this.run_cycle.bind(this));
		this.clear();
		for (const c in src.traders) {			
			this.recalc(this.register_wid(c));
		}		
	};
	
	register_wid(s) {
		this.symbol_map.delete(s);
		const nfo = this.src.traders[s].info;
		const wid = nfo.brokerName+"/"+nfo.walletId+"/"+nfo.currency;
		this.symbol_map.set(s, wid);
		return wid;
		
	};
	
	run_cycle(event) {
		if (event.type != "stop" || event.obj == this) {
			
			if (event.type == "info") {
				const wid = this.register_wid(event.symbol);
				this.recalc(wid);				
			}
			if (event.type == "trade") {
				const wid = this.register_wid(event.symbol);
				this.recalc(wid);
			}
			if (event.type == "price") {
				const wid = this.register_wid(event.symbol);
				this.recalc_price(wid, event.symbol, this.src.traders[event.symbol].price);				
			}
			
			event.next.then(this.run_cycle.bind(this));
		}
	};

	getwdef(wid) {
		let wdef = this.wallet_map.get(wid);
		if (!wdef) {
			wdef = {};
			wdef.mko = new mmbot.MKO(()=>{
				this.recalc_delayed(wid);
			},500);
			this.wallet_map.set(wid,wdef);
		}
		return wdef;		
	};
		
	
	recalc(wid) {
		let wdef = this.getwdef(wid);
		wdef.mko.event();
	};
	recalc_delayed(wid) {
		let wdef = this.getwdef(wid);
		var trades = [];
		var last_symbol;		
		for (const item of this.symbol_map) {
			if (item[1] == wid) {
				var s = item[0];
				const ltrades = this.src.traders[s].trades;
				if (ltrades) {
    				last_symbol = s;
					for (const tr of Object.values(ltrades)) {
					    trades.push(Object.assign({trader:s},tr));
					}
				}
			}
		}
		if (last_symbol) {
			const nfo = this.src.traders[last_symbol].info;			
			const misc = this.src.traders[last_symbol].misc;			
			wdef.info = {symbol:nfo.currency, wallet: nfo.walletId, broker: nfo.brokerName, icon: nfo.brokerIcon,
			be: misc.be};
		}
		trades.sort(function(a,b){return a.time - b.time;});
		wdef.last={};
		let recent_symbols={};
		let sums = trades.reduce(function(acc, item){
			let len = acc.length;
            var last = len == 0?{pnl:0,norm:0}:acc[len-1];
            acc.push({
            	time: item.time,
            	pnl: last.pnl+item.gain,
            	norm: last.norm+item.normch,
            	_d_pnl: item.gain,
            	_d_norm: item.normch,
            });
			wdef.last[item.trader]=Object.assign({},item);
			recent_symbols[item.trader] = item.time;			
            return acc;
		},[]);
		wdef.serie = sums;
		wdef.slen = sums.length;
		recent_symbols = Object.keys(recent_symbols).map(n=>[recent_symbols[n],n]);
		recent_symbols.sort((a,b)=>b[0]-a[0]);
		wdef.recent_symbols = recent_symbols;

		this.broadcast({wid:wid});
	};
	
	async recalc_price(wid, symbol, price) {
		let wdef = this.getwdef(wid);
		if (wdef.slen == 0) return;
		await wdef.mko.wait();
		let last = wdef.last[symbol];
		if (!last) return;
		last.new_price = price;
		let sum = Object.values(wdef.last).reduce((a,x)=>{
			let last_price = x.price;
			let recent_price = x.new_price || x.price;
			let inv = this.src.traders[symbol].info.inverted;
			let pnl = inv?(x.pos*(1.0/last_price-1.0/recent_price)):(x.pos*(recent_price-last_price));
			return a+pnl; 
		},0);
		let lastItem = wdef.serie[wdef.slen-1];
		let newItem = {
			time:Date.now(),
			pnl:lastItem.pnl+sum,
			norm: lastItem.norm,
			_d_pnl: sum,
			_d_norm: 0
		};
		if (wdef.serie.length>wdef.slen) wdef.serie[wdef.slen] = newItem;
		else wdef.serie.push(newItem);

		this.broadcast({wid:wid});		
	};
	
	get_total(wid) {
		let wdef = this.getwdef(wid);
		let last = wdef.last;
		if (!last) return 0;
		let sum = Object.values(wdef.last).reduce((a,x)=>{
			return {pnl: a.pnl+x.pl,norm: a.norm+x.norm}
		},{pnl:0,norm:0});
		return sum;
	};
	get_recent(wid, interval, rel_to_now) {
		const wdef = this.getwdef(wid);
		const serie = wdef.serie;
		let res = {pnl:0, norm:0};
		if (!wdef.slen) return res;
		const start = (rel_to_now?Date.now():serie[wdef.slen-1].time)-interval;
		const start_pos = binarySearch(serie, x=>x.time-start);
		const b = start_pos?serie[start_pos-1]:res;
		const e = serie[wdef.slen-1];
		res.pnl = e.pnl - b.pnl;
		res.norm = e.norm - b.norm;
		return res;
	};
	get_recent_symbols(wid) {
		const wdef = this.getwdef(wid);
		return wdef.recent_symbols;
	};
	get_source() {
		return this.src
	};
	get_range(wid) {
		const wdef = this.getwdef(wid);
		let res = {pnl:0, norm:0};
		if (wdef.serie.length == 0) return res;
		const l = wdef.serie[wdef.serie.length-1];
		res.norm = l.norm;
		res.pnl = l.pnl;
		return res;
	}
};

mmbot.Sumprofit = Sumprofit;

