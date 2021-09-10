//@namespace mmbot
//@require sumprofit.js

class ProfitPanel {
	
	constructor(src, walletId) {
		this._src = src;
		this._walletId = walletId;
		const pnl_line=120;
		const norm_line=150;
		const asset_line1=185;
		const asset_line2=210;
		const col1=150;
		const col2=290;
		const col3=445;
		const col4=600;
		const colm=5;
		const acol1l=110;
		const acol1a=115;
		const acol1p=320;
		const acol2l=390;
		const acol2a=395;
		const acol2p=600;
	
		let svg = new mmbot.SVG();
		svg.create(600,215);
		svg.push_group("brokerimg");
			this.brokerImg = svg.image(0,10,64,64,"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAADAAAAAwCAQAAAD9CzEMAAADO0lEQVRYw9WXTUhUURTHz6OJ3EiNMJCRxGyGcuFiQBcuWgzhooWLFgVGLlzMwkXhwkXQwkXgQmghSBupheBqQBKEIGgREkhhkkwmkRaTzVSohGn51a/FPN+c++a9efPVwru679xzz7nn63/OEznuy/Im0yk35ZR8lCfWuzprpJkM5pqup/hJW+g8t0kywqr9fa0+4p8D0OuipgAYroeCZfYIedDbAOisTXjE/XIz6AC1KdjVAoAsPwzKODBRi4L7R9dpYM7JoIhzHqrJBhpodPa9RpI6MWEBaKtWwYbhji0gSowZoMeh9uRziTgJEiSIVaLAMJ91YJoZ9oHLBleaFsO+iTKhAkQsq6BAmhyO11Z7QYG8ty4xLhdEROSinBcRkZPWQTUWJOiiS9MJA7Oue6+AkuKPQvhTThNyXtIkIjfkhIv3log8dTmgnRWJ0mc9CrLgMZC09/2Gl0cdnhwQDrLeT4GR5SpRYyqRPQQREmEnT/cCGc26pjGTDFPMsKrO0wUbHdoAkGQe3Ejg3QcAWn1Ohzzfn0/ZfWAdSAe5Kemngrvg4/+Y48y1csptGICXNChaK7/BLDjjTgcAv8qt6G7nRYvMk7X3ezSXuJNguVJc1WuLq/9l3CBGawGsfdJiWj1kkZbKFIwVANynvR6tv85uo/QdfX0JQAfbOI3aAgcUrcemxcsRP1vcbtxVT8qvWogGiU8pz3ogJXvApM/dwTKQKV9uQIo5oM912g8GhDSS0eEl5a++kNeNRIAlEbo91YddlfNAuxL4UHETVdMTZNT3ouPMVKV1MAPc80hejbnXAciSJafnPlZ0fpXsEEVz6hKoAWe0mMO2/jDg/yAPc5IWkT/yUBZEROST9YJNOaOGg2b5am+/WDrQxghRyoooWxqWRNg0I0MnObbd07eOX6AeInLHHlPeWiM8kytyzsoW8XyXN1ZcWfDNOlsdBCaBMc8hM2GEfqp6lNXDQdwBun7FkYNS/SNIwWf9WvvPYdDVpw9r6xPoVBWho6jSa2tRjPrhrAjbwHzt3S5jhtUZDwAOAgawMlXkIf2QEaKEiDDEDpQxG1WgIs4u7lXv8YAww6TJscJYYB87tusf8bF7YIB0L5gAAAAASUVORK5CYII=");
		svg.pop();
		svg.push_group("symbolname");
			this.symbolName = svg.text_space(64,46,"SYMBOL",136);
		svg.pop();
		svg.push_group("walletname");
			this.walletInfo = svg.text_space(64,70,"brokerId walletId",136);
		svg.pop();
		svg.push_viewport(200,0,145,84);
		    svg.push_group("frame");svg.rect(0,0,"100%","100%");svg.pop();
		    this.pnlchart = svg.push_group("chart");svg.pop();
			svg.push_group("chartlabels");
				svg.text(2,"100%","PnL");
			svg.pop();
		svg.pop();
		svg.push_viewport(350,0,145,84);
			svg.push_group("frame");svg.rect(0,0,"100%","100%");svg.pop();
			this.normchart = svg.push_group("chart");svg.pop();
			svg.push_group("chartlabels");
				svg.text(2,"100%","Norm.profit");
			svg.pop();
		svg.pop();
		svg.push_group("budgetextra");
			svg.text(550,25,"Budget");
			svg.text(550,42,"extra");		
			this.budgetExtra = svg.text(550,70,"+9999999");
		svg.pop();
		svg.push_group("statslabelsbg");
			const height = 10;
			[[0,col1],[col1+colm-2,col2],[col2+colm-2,col3],[col3+colm-2,col4]].forEach(x=>{
				svg.gradrect(x[0],pnl_line-height, x[1]-x[0],height+3);
				svg.gradrect(x[0],norm_line-height, x[1]-x[0],height+3);
			});
		svg.pop();
		svg.push_group("statslabelsbig");
			svg.text(2,pnl_line,"PnL:");
			svg.text(2,norm_line,"Norm:");
			svg.text(2,asset_line1,"Posit:");
		svg.pop();
		svg.push_group("statslabels");
			svg.text(col1+colm,pnl_line,"last:");
			svg.text(col1+colm,norm_line,"last:");
			svg.text(col2+colm,pnl_line,"24h:");
			svg.text(col2+colm,norm_line,"24h:");
			svg.text(col3+colm,pnl_line,"range:");
			svg.text(col3+colm,norm_line,"range:");
		svg.pop();
		svg.push_group("stats");
			this.pnl_total=svg.text(col1,pnl_line,"+9999999");
			this.norm_total=svg.text(col1,norm_line,"+9999999");
			this.pnl_last=svg.text(col2,pnl_line,"+9999999");
			this.norm_last=svg.text(col2,norm_line,"+9999999");
			this.pnl_24h=svg.text(col3,pnl_line,"+9999999");
			this.norm_24h=svg.text(col3,norm_line,"+9999999");
			this.pnl_range=svg.text(col4,pnl_line,"+9999999");
			this.norm_range=svg.text(col4,norm_line,"+9999999");
		svg.pop();
		svg.push_group("assetslabels");
			this.assets=[{},{},{},{}];
			this.assets[0].name=svg.text(acol1l,asset_line1,"ASSET1");
			this.assets[1].name=svg.text(acol2l,asset_line1,"ASSET2");
			this.assets[2].name=svg.text(acol1l,asset_line2,"ASSET3");
			this.assets[3].name=svg.text(acol2l,asset_line2,"ASSET4");
		svg.pop();
		svg.push_group("assets");
			this.assets[0].pos=svg.text(acol1a,asset_line1,"+9999POS");
			this.assets[1].pos=svg.text(acol2a,asset_line1,"+9999POS");
			this.assets[2].pos=svg.text(acol1a,asset_line2,"+9999POS");
			this.assets[3].pos=svg.text(acol2a,asset_line2,"+9999POS");
		svg.pop();
		svg.push_group("prices");
			this.assets[0].price=svg.text(acol1p,asset_line1,"+99PRICE");
			this.assets[1].price=svg.text(acol2p,asset_line1,"+99PRICE");
			this.assets[2].price=svg.text(acol1p,asset_line2,"+99PRICE");
			this.assets[3].price=svg.text(acol2p,asset_line2,"+99PRICE");
		svg.pop();
		this._root = svg.get();
		this.positions=[];
		for (let i = 0; i<4; i++) this.positions.push({
			id:"",
			name:"",
			price:"",
			pos:"",
			prev_pos:"",
			time:0
		});
		this.posmap={};
	}
	
	appendTo(element) {
		element.appendChild(this._root);
	}
	
	getWalletID() {
		return this._walletId;
	}
	
	update() {
		const wid = this._src.getwdef(this._walletId);
		if (!wid.info) return;
		this.symbolName.textContent = wid.info.symbol;
		this.brokerImg.setAttribute("href",wid.info.icon);
		this.walletInfo.textContent = wid.info.broker+" "+wid.info.wallet;
		let recent24 = this._src.get_recent(this._walletId,24*60*60*1000,true);
		setElNumber(this.norm_24h,recent24.norm);
		setElNumber(this.pnl_24h,recent24.pnl);
		let last = this._src.get_recent(this._walletId,60*1000,false);
		setElNumber(this.norm_last,last.norm);
		setElNumber(this.pnl_last,last.pnl);
		let total = this._src.get_total(this._walletId);
		setElNumber(this.norm_total,total.norm);
		setElNumber(this.pnl_total,total.pnl);
		let range = this._src.get_range(this._walletId);
		setElNumber(this.norm_range,range.norm);
		setElNumber(this.pnl_range,range.pnl);
		setElNumber(this.budgetExtra,wid.info.be);
		const s = wid.serie.slice(-25);
		this.drawChart(this.pnlchart, s.map(x=>x.pnl));
		this.drawChart(this.normchart, s.map(x=>x.norm));

		let recsymb = this._src.get_recent_symbols(this._walletId);
		const dsrc = this._src.get_source();
		recsymb.forEach(x=>{
			let itm = this.positions.find(z=>z.id == x[1]);
			if (!itm) {
				itm = this.positions.reduce((a,x)=>{
					return a.time<=x.time?a:x;
				},{time:x[0]});
				itm.id = x[1];
			}
			if (itm) {
				 const  trd = dsrc.traders[x[1]];
				if (trd) {
					itm.name = trd.info.asset;
					itm.pos = trd.misc.ba;
					itm.prev_pos = trd.prev_misc.ba;
					itm.price = trd.price;
					itm.time = x[0];					
				}
			}
		});


		this.positions.forEach((x,idx)=>{
			let cntr = this.assets[idx]; 
			let recent = (Date.now() - x.time)<60000; 
			cntr.name.textContent = x.name;
			cntr.pos.textContent = adjNum(x.pos);
			cntr.price.textContent = adjNum(x.price);
			cntr.pos.classList.toggle("flashing",x.pos != x.prev_pos && recent);
			cntr.pos.classList.toggle("sell",x.pos < x.prev_pos && recent);
			cntr.pos.classList.toggle("buy",x.pos > x.prev_pos  && recent);			
		});
	}	
	
	drawChart(chart, series) {
		let svg = new mmbot.SVG(chart);
		svg.clear();
		if (series.length) {
			let minmax = series.reduce((a,x)=>{return {
				min:a.min<x?a.min:x,
				max:a.max>x?a.max:x 
			}},{min:series[0],max:series[0]});
			let mx = 140/(series.length-1);
			let my = 80/(minmax.max-minmax.min);
			let points = series.map((v,idx)=>{
				return [(idx * mx+2),
				        (81-(v - minmax.min)*my)];
			});
			svg.polyline(points);
		}
	}
	
	
};

class ProfitPanelControl {
	constructor(source) {
		this._src = source;
		let panels = this._panels = new Map;
		
		function ondata(x) {
			let p = panels.get(x.wid);
			if (!p) {
				p = new ProfitPanel(source, x.wid);
				panels.set(x.wid, p);
				if (this._appendToElem) p.appendTo(this._appendToElem);
			}
			p.update();			
			x.next.then(ondata.bind(this));
		}
		
		this._src.listen().then(ondata.bind(this));
	};
	
	appendTo(elem) {
		for(let c in this._panels) {
			c.appendTo(elem);
		}
		this._appendToElem = elem; 
	}
};

mmbot.ProfitPanel = ProfitPanel;
mmbot.ProfitPanelControl = ProfitPanelControl;
