//@namespace mmbot
//@require sumprofit.js

class ProfitPanel {
	
	constructor(src, walletId) {
		this.src = src;
		this.walletId = walletId;
		
		var 
	
		var svg = new mmbot.SVG();
		svg.create(600,300);
		this.brokerImg = svg.image(0,0,64,64,"placeholder.png");
		
	
	}
	
	
	
};

mmbot.ProfitPanel = ProfitPanel;
