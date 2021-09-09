//@require source.js
//@namespace mmbot

class Sumprofit extends mmbot.Source {
	
	constructor() {
		super();	
	}
	
	clear() {
		this.symbol_map = new Map;
		this.wallet_map = new Map;						
	}
	
	set_source(src) {
		if (this.src) this.src.broadcast({type:"stop",obj:this});
		this.src = src;
		this.src.listen().then(this.run_cycle.bind(this));
		this.clear();
		for (const c in src.traders) {
			this.register_wid(c);
		}
	}
	
	register_wid(s) {
		this.symbol_map.delete(s);
		const nfo = this.src.traders[s].info;
		const wid = nfo.brokerName+"/"+nfo.walletId+"/"+nfo.currency;
		this.symbol_map.set(s, wid);
		return wid;
		
	}
	
	run_cycle(event) {
		if (event.type != "stop" || event.obj == this) {
			
			if (event.type == "info") {
				const wid = this.register_wid(event.symbol);
				this.recalc(wid);				
			}
			if (event.type == "trade") {
				const wid = this.symbol_map.get(event.symbol);
				if (wid) {
					this.recalc(wid);
				}
			}
			
			event.next.then(this.run_cycle.bind(this));
		}
	}
	
	recalc(wid) {
		let wdef = this.wallet_map.get(wid);
		if (!wdef) {
			wdef = {};
			this.wallet_map.set(wid,wdef);
		}
		var trades = [];
		var last_symbol;
		for (const item of this.symbol_map) {
			if (item[1] == wid) {
				var s = item[0];
				last_symbol = s;
				for (const tr of Object.values(this.src.traders[s].trades)) {
					trades.push(tr);
				}
			}
		}
		if (last_symbol) {
			const nfo = this.src.traders[last_symbol].info;			
			wdef.info = {symbol:nfo.currency, wallet: nfo.walletId, broker: nfo.brokerName, icon: nfo.brokerIcon};
		}
		trades.sort(function(a,b){return a.time - b.time;});
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
            return acc;
		},[])
		wdef.serie = sums;

		this.broadcast({"serie":wid});
	}
};

mmbot.Sumprofit = Sumprofit;

