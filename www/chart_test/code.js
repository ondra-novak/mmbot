

class BTDownloader {

	constructor() {
		this.data = [];
		this.evsrc = null;
	}	
	
	
	fetch(id, cb) {
		if (this.evsrc) this.evsrc.close();
		if (this.cb) this.cb(null);
		this.cb = cb;
		var curobj = {};
		this.data = [];
		this.evsrc = new EventSource("../api/run/"+id);
		this.evsrc.onmessage = msg=>{
			if (this.cb) {
				var js = JSON.parse(msg.data);
				if  (js.diff === false) {
					if (js.event == "progress") this.cb(["progress", js.progress]);
					else if (js.event == "done") {
						this.cb(["done",this.data]);
						this.cb = null;								
					}
				}
				else {
					curobj = BTDownloader.applyDiff(curobj, js);
					if (curobj.trades) {
						let t = curobj.trades;
						let env = Object.assign({}, curobj);
						delete env.trades;
						t.forEach(tr=>{
							tr.ctx = env;
							this.data.push(tr);
						});
					}				
				}
			}
		}	
		this.evsrc.onerror = () => {
			if (this.cb) this.cb(null);
			this.evsrc.close();
		}
	}
	
	abort() {
		if (this.evsrc) {
			this.evsrc.close();
			this.evsrc = null;
		}
		if (this.cb) {
			this.cb(null);
			this.cb = null;
		}
	}

	
	
	
static applyDiff(src, df) {
	if (Array.isArray(df) || df === null || typeof df != "object") return df;
	if (Array.isArray(src) || src === null || typeof src != "object") src = {};
	else src = Object.assign({},src);
	var empty = true;
	
	for (var x in df) {
		empty = false;
		var z = src[x];
		z = BTDownloader.applyDiff(z, df[x]);
		if (z === undefined) {
			delete src[x];
		} else {
			src[x] = z;
		}
	}	
	return empty?undefined:src;	
}
	
	
}




var tc1;

function chartTest(data, sres) {
	const tmofs = Date.now()/60000;
	const cfg = {
		width:800,
		height:400,
		padding:20,
		xaxis: {
			step:1440,
			width: 25,
			line: "axis-x",
			text: "axis-x-text",
			label_step:4,
			label_cb: x=>(new Date(x*60000)).toLocaleDateString(),			
		},
		yaxis: {
			zero_line: "axis-zero",
			line:"axis-y",
			text:"axis-y-text",
			label_cb: x=>adjNum(x),			
		}
	}
	tc1 = new TimeChart(cfg);
	

	tc1.add_curve({
		length: Math.floor(data.length/240),
		source: x=>{
			const idx = x * 240;
			const o = data[idx];
			const c = data[idx+239]
			const l = Math.min.apply(this,data.slice(idx,idx+240));
			const h = Math.max.apply(this,data.slice(idx,idx+240));
			return [idx+tmofs,[o,h,l,c]];
		},
		autoscale:true,
		ordered:true,
		class:"curve",
		type:"ohlc"		
	});
	
	tc1.add_curve({
		length: sres.length,
		source: x=>[sres[x].time/60000,sres[x].price,sres[x].size>0?"A":"V",sres[x].size>0?"buy":"sell",1],
		autoscale:true,
		class:"btcurve",
		ordered:true
	});


	tc1.draw(0);
	document.body.appendChild(tc1.svg);

	tc1.install_scroll_handlers();
	
		

}

async function start() {

	var data = await (fetch("data/btcusd_data.json").then(x=>x.json()));
	var trader = await (fetch("data/trader.json").then(x=>x.json()));
	let sres = {
		
	}

	
	var upload_res = await (fetch("../api/backtest/data",{method:"POST",body:JSON.stringify(data)}).then(x=>x.json()));
	var id = upload_res.id;
	trader.source = {id:id};
	var backtest_init = await (fetch("../api/backtest?stream=true",{method:"POST",body:JSON.stringify(trader)}).then(x=>x.json()));
	var stream_id = backtest_init.id;
	let bt = new BTDownloader();
	bt.fetch(stream_id,ev=>{
		if (ev[0] == "progress") console.log("progress", ev[1]);
		else if (ev[0] == "done") {
			chartTest(data, ev[1]);
		}
	});

}