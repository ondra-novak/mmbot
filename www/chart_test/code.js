


function applyDiff(src, df) {
	if (Array.isArray(df) || df === null || typeof df != "object") return df;
	if (Array.isArray(src) || src === null || typeof src != "object") src = {};
	else src = Object.assign({},src);
	var empty = true;
	
	for (var x in df) {
		empty = false;
		var z = src[x];
		z = applyDiff(z, df[x]);
		if (z === undefined) {
			delete src[x];
		} else {
			src[x] = z;
		}
	}	
	return empty?undefined:src;	
}

function readStream(id, cb) {
	var curobj = {};
	var estr = new EventSource("../api/run/"+id);
	estr.onmessage = function(ev) {
		var js = JSON.parse(ev.data);
		if  (js.diff === false) cb(js);
		else {
			curobj = applyDiff(curobj, js);
			cb(curobj);
		}
	}	
	estr.onerror = function(ev) {
		cb(null);
	}
	
}

var tc;

function chartTest(data) {
	tc = new TimeChart({
		width:800,
		height:400,
		padding:20,
		xaxis: {
			step:1440,
			width: 120,
			line: "axis-x",
			text: "axis-x-text",
			label_step:2,
			label_cb: x=>(new Date(x*60000)).toLocaleDateString(),			
		},
		yaxis: {
			zero_line: "axis-zero",
			line:"axis-y",
			text:"axis-y-text",
			label_cb: x=>adjNum(x),			
		}
	});

	tc.add_curve({
		length: Math.floor(data.length/60),
		source: x=>{
			const idx = x * 60;
			const o = data[idx];
			const c = data[idx+59]
			const l = Math.min.apply(this,data.slice(idx,idx+60));
			const h = Math.max.apply(this,data.slice(idx,idx+60));
			return [idx,[o,h,l,c]];
		},
		autoscale:true,
		ordered:true,
		class:"curve",
		type:"ohlc"		
	});
	

	tc.draw(0);
	document.body.appendChild(tc.svg);
	tc.svg.setAttribute("draggable",true);
	tc.svg.addEventListener("click",(ev)=>{
		console.log(tc.map_pixel_to_xy(ev.clientX, ev.clientY));
	});

	tc.install_scroll_handlers();
	
		

}

async function start() {

	var data = await (fetch("data/btcusd_data.json").then(x=>x.json()));
	var trader = await (fetch("data/trader.json").then(x=>x.json()));
	chartTest(data);
/*	
	var upload_res = await (fetch("../api/backtest/data",{method:"POST",body:JSON.stringify(data)}).then(x=>x.json()));
	var id = upload_res.id;
	trader.source = {id:id};
	var backtest_init = await (fetch("../api/backtest?stream=true",{method:"POST",body:JSON.stringify(trader)}).then(x=>x.json()));
	var stream_id = backtest_init.id;
	
	readStream(stream_id,x=>{
		console.log(x);
	})
	*/
}