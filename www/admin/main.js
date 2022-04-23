"use strict";
var avail_langs = [
	["en","data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAEgAAAAwBAMAAABAsiHYAAAAMFBMVEUAG2oAIWvKACrIEzBPWYHOT1bRYGnXfICdnq7bm6Djqava19vz1dTy3uH5+Pj9//yRXFQ4AAABZ0lEQVQ4y+2VIU9DMRDH/1+BZAJdEgSSy0jATU/OokCj0XyBZ/AI8FN4FBqHnR0T4wPAOPra3r3rrSQTBMUlL3vt+9/11+7uijtmftxT4weiMx7G080lnWP8wvx185Nof31Pp++ID/PHpC0aPb1SjIIYLS64bIu61QFFHuQfxapECSh+RA6oWFaUgSIM9G3iRSP1h8ZcelGnJLCDSmScYcNakcWAG4uo8oSLLKKKAY6xiOrdwO02i9y5wJ1bFrkTRrHp5iL08bNoFUJaXb6ysyRy9ouia2czorGfA+1g/yLa7TD/+A+WpIvvbyHMVfQcDheadCV9Y+p+zuhK0jem7i0drSV9SyHIbCkE8bGF0PXx6WQxlFS/Oh3PTUmZKS1OdSsiG3woc4MFNx4ahvGEi2xaz8AAx2ibmDrD7bZqh4IBd25VYxV/WKCtFl1IYIG2m30OAQvUuDYSDCxQ4wJKWLBArausx/oG0moFWcbnpY4AAAAASUVORK5CYII="],
	["cs","data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAEgAAAAwBAMAAABAsiHYAAAAMFBMVEXXAAAAQn8CRXwYRX3NGSfXFxo5QnqtKkp5OGg6XYxBX4qLmrOOnLXK0drv9Pb9//ye4wQMAAAAzElEQVQ4y43SzQ3CMAyG4ewBA3AtlhA/547gGTJBN8gajMAKGYEVOkJGAJVWIXUSf/7Oz+GVZRc/eO4+GxAdkwHRy4KGtwHRbTYgmPVD9LQgkLUi0q+1ITokA1KzMhqiAWlZf6RkFaifVSKKFtTL2iF6JAPq/JZA7SyJmlkSNbMq1MqqkXf1pLmOXE2iy8QYBcbIM0atIImaQRIFxsgzRp2gHeoFlegcGCPPGJ1GjJSgjLSgjDxjpAZtSA9aEQhakWeMUNCCYNCCAjb8BbPXr46aiiSEAAAAAElFTkSuQmCC"]
];

async function formTest() {
	var def = await (await fetch("/api/admin/forms")).json();
	var form = create_form(def.trader, "trader", "init");
	form.open();
}

class App {
	
    lang = new Lang();
	page_root = null;	
	
	constructor() {
		this.langobj = new Lang();
		
		
	}
	
	async start() {
		var r = await Promise.all([
			ui_fetch("/forms"),
			(async ()=>{
				await this.chooselang();
				this.page_root = load_template("root");
				document.body.appendChild(this.page_root.root);
				await this.on_login();
				await this.update_ui();
			})()]);
		this.form_defs=r[0];
		window.addEventListener("hashchange",this.router.bind(this));		
		this.router();
	}


	async chooselang() {
		const lang = localStorage["lang"];
		if (!lang) {
			var l = await chooselang_dlg(avail_langs);
			localStorage["lang"] = l;
			this.langobj.load(l);
		} else {
			return this.langobj.load(lang);
		}
	}

	async chooselang_dlg() {
		var l = await chooselang_dlg(avail_langs);
		localStorage["lang"] = l;
		this.chooselang();
	}
	
	async on_login(usr) {
		try {
			if (!usr) usr = await ui_fetch("/user");
			var data = {
				uname: usr.user,
				lang:{"!click":this.chooselang_dlg.bind(this)},
				pwd:{".hidden":!usr.exists,
					 "!click":this.change_pwd.bind(this)},
				logout:{".hidden":!usr.exists && !usr.jwt,
					   "!click":this.logout.bind(this)},
				login:{".hidden":usr.exists,
					   "!click":this.login.bind(this)},
				usermenu:{classList:{
					exists:usr.exists,
					jwt:usr.jwt
				}}
			}
			this.cur_user = usr;
			this.page_root.setData(data);
		} catch (e) {
			errorDialog(e);		
		}
	}

	async change_pwd() {
		try {
			await dialog_change_pwd(async (data,dlg)=>{
				try {
					await post_json("/user/set_password", {
						old:data.old_password,
						new:data.new_password,
					})
					return true;
				} catch (e) {
					if (e.io_error && e.req.status == 409) {
						dlg.mark("not_match");						
					} else {
						errorDialog(e);
					}
					return false;
				}				
			});
			await dialogBox([{type:"label",name:"pwd_changed"},
							{type:"button",bottom:true,name:"ok"}],
							"pwd_changed","pwd_changed");

		} catch(e) {}
	}

	async logout() {
		await dialogBox([
			{name:"ask_logout",type:"label"},
			{name:"ok",type:"button",bottom:true},
			{name:"cancel",type:"button",bottom:true}
		],"logout_dlg","Confirm logout");
		try {
			await ui_fetch("/user",{method:"DELETE"});
			this.on_login({"user":"","exists":false});
		}catch (e) {
			errorDialog(e);
		}
	}
	
	async update_ui() {
		if (this.cur_user.config_view) {
			try {
				this.orig_config = await ui_fetch("/config")
				this.config = JSON.parse(JSON.stringify(this.orig_config));
				if (!this.config.session_hash) {
					var array = new Uint8Array(24);
					crypto.getRandomValues(array);
					this.config.session_hash = await base64_arraybuffer(array);
				}
			} catch (e) {
				errorDialog(e);
			}
			
		}
	}
	
	async login() {
		this.cur_user = await ui_login();
		await update_ui();
	}
	
	router() {
		let h = location.hash;
		if (h) {
			h = h.substring(1);
			let req = h.split("&").reduce((a,b)=>{
				let kv = b.split("=").map(decodeURIComponent);
				a[kv[0]] = kv[1];return a;
			},{});

			var fn;			
			if (req.menu=="options") {
				fn = this["page_options"+(req.submenu?("_"+req.submenu):"")] 				
			}
			if (fn) {
				fn.call(this);
			} else {
				this.unknown_page();
			}
		}
	}
	route(req) {
		let h = "#"+Object.keys(req).map(x=>encodeURIComponent(x)+"="+encodeURIComponent(req[x]))
					  .join("&");
		location.hash = h;
	}
	
	
	
	page_options() {
		var page = load_template("options_grid");
		this.page_root.setData({"workspace":page});									 		
	}
	page_options_users() {
		
		 const acl_to_list = (acls)=> {
			var lst = Object.keys(acls).filter(x=>acls[x]);
			return lst.map(x=>{return {"":this.langobj.get_text("acls",x,x)};});				
		}
		
		var page = load_template("page_users");
		this.page_root.setData({"workspace":page});
		var users = this.config["users"] || [];
		var pacl = (users.find(x=>!!x.public) || {public:true, acl:{
				viewer:true,reports:true,config_view:true,
				config_edit:true,backtest:true,users:true,
				wallet_view:true,manual_trading:true,api_key:true,
				must_change_pwd:true

		}}).acl;
		var fdata = {
			public_acl: acl_to_list(pacl)
		};
		page.setData(fdata);

		
					
	}
	unknown_page() {
		this.page_root.setData({"workspace":load_template("unknown_page")});
	}
		
}

var theApp = new App();

function start() {
	theApp.start();
}

async function on_login(usr) {
	await theApp.on_login(usr);
}

	
	
	
	
	
	
	
