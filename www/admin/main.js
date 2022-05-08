"use strict";

async function formTest() {
	var def = await (await fetch("/api/admin/forms")).json();
	var form = create_form(def.trader, "trader", "init");
	form.open();
}

class App {
	
    lang = new Lang();
	page_root = null;
	page_refresh = null;	
	
	constructor() {
		this.langobj = new Lang();
		
		
	}
	
	async start() {
		this.lang_list = app_config.available_languages;
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
			var l = chooselang_dlg(await this.lang_list);
			l.then(l=>{
				localStorage["lang"] = l;
				this.langobj.load(l);
			});
		} else {
			return this.langobj.load(lang);
		}
	}

	async chooselang_dlg() {
		var l = await chooselang_dlg(await this.lang_list);
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
				}},
				save:{"!click":this.save_config.bind(this)}
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
			location.href="?";
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
			
		} else {
			this.orig_config = this.config = {};
			let dlg = load_template("not_authorized");
			dlg.openModal();
			dlg.setDefaultAction(()=>{this.login();dlg.close();},"ok");
			dlg.setCancelAction(()=>{dlg.close();},"cancel");
		}
		this.page_root.showItem("save", this.cur_user.config_edit);
	}
	
	async login() {
		this.cur_user = await ui_login();
		await this.update_ui();
		if (this.page_refresh) this.page_refresh();
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

	toggle_saving(b) {		
		this.saving_in_progess = b;
		this.page_root.setData({save:{classList:{processing:b===true,done:b===false}}})
	}

	async save_config() {
		try {
			if (this.saving_in_progess) return;
			this.toggle_saving(true);
			var cfgdiff = createDiff(this.orig_config,this.config);
			var fincfg = await post_json("/config", cfgdiff);
			this.orig_config = fincfg;
			this.config = JSON.parse(JSON.stringify(fincfg));
			
			this.toggle_saving(false);
		} catch (e) {
			this.toggle_saving(null);
			errorDialog(e);
			return;
		}
		if (this.page_refresh) this.page_refresh();
	}	
		
	async set_active_page(page, refresh_fn) {
		this.page_root.setData({"workspace":page});
		this.page_refresh = refresh_fn;
		if (refresh_fn) return await refresh_fn();
		else return null;
	}
		
	
	page_options() {
		var page = load_template("options_grid");
		this.set_active_page(page,()=>{
			const me = this.cur_user;
			var data={
				users:{".hidden":!me.users},
				apikeys:{".hidden":!me.api_key},
				walletview:{".hidden":!me.config_view},
				misc:{".hidden":!me.config_view},
				exchanges:{".hidden":!me.wallet_view},
				archive:{".hidden":!me.config_view},
				global_stop:{".hidden":!me.config_edit},
				reload_brokers:{".hidden":!me.config_edit},
				messages:{".hidden":!me.config_view || !me.exists},
				utilization:{".hidden":!me.config_view}
			};
			page.setData(data);
			
		});
		this.page_refresh();
	}
	page_options_users() {
		if (!this.config.users) this.config.users = {};
		page_options_users(this, this.config.users);					
	}
	page_options_wallet() {
		page_options_wallet.call(this);
	}
	
	unknown_page() {
		this.page_root.setData({"workspace":load_template("unknown_page")});
	}
	page_options_apikeys() {
		page_options_apikeys.call(this);
	} 
	
		
}

var theApp = new App();

function start() {
	theApp.start();
}

async function on_login(usr) {
	await theApp.on_login(usr);
}

	
	
	
	
	
	
	
