//@style style.css
//@require ../res/template.js
//@require ../res/ui.js
//@style ../res/ui.css
//@head header.html
//@require common.js
//@require svg.js
//@require stream.js
//@require profitpanel.js
//@namespace mmbot
//@html main.html




document.body.onload=function() {
	let app = mmbot.app = new App();
	app.uitest();
	window.thisApp = app;
	app.waitForInit().then(()=>{
		app._profitpanels.appendTo(document.getElementById("main_panel"));
	});
};


class App {
	constructor() {
		
		this._source = new mmbot.DataStream();
		
		
	}
	
	async waitForInit() {
		let ev = await this._source.listen();
		while (ev.type != "refresh" || ev.state) {
			ev = await ev.next;
		}
		console.log("Init!");
		this._sumprofit = new mmbot.Sumprofit();
		this._sumprofit.set_source(this._source);
		this._profitpanels = new mmbot.ProfitPanelControl(this._sumprofit);
	}
	
	
	uitest() {
		
		
		
	}
	
};