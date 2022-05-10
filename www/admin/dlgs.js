"use strict";

function create_form(formdesc, langcat, category, dialog) {
	var fbld = new FormBuilder(formdesc, theApp.langobj, langcat, category);
	return fbld.create_form(dialog);
}

function load_template(id) {
	var form = TemplateJS.View.fromTemplate(id);
	theApp.langobj.translate_node(form.getRoot(), id);
	load_icons(form.getRoot());
	return form;
}

function dialogBox(formdesc, langcat, title, validate) {
	var dlg  = create_form(formdesc, langcat, "", title);
	var p = new Promise((ok, cancel) => {
		
		dlg.setDefaultAction(async () => {
			var data = dlg.readData();
			if (!validate || await validate(data, dlg)) {
				dlg.close();
				ok(dlg.readData());
			}
		}, "ok");
		dlg.setCancelAction(() => {
			dlg.close();
			cancel();
		}, "cancel");
		dlg.openModal();
	});
	p.dlg = dlg;
	return p;
}


function ui_login() {
	return new Promise((ok, cancel) => {
		var dlg = create_form([
			{ name: "user", type: "string", attrs: { "autocomplete": "username" } },
			{ name: "password", type: "password", attrs: { "autocomplete": "current-password" } },
			{ name: "logerror", type: "errmsg" },
			{ name: "login", type: "button", bottom: true },
			{ name: "cancel", type: "button", bottom: true }]
			, "login", "", "login_dlg");
		dlg.openModal();
		dlg.setDefaultAction(async () => {
			var data = dlg.readData();
			try {
				var unfo = await post_json("/user", { "user": data.user, "password": data.password, "cookie": "permanent" });
				if (unfo.exists) {
					dlg.close();
					ok(unfo);
					on_login(unfo);
				} else {
					dlg.mark("logerror");
				}
			} catch (e) {
				errorDialog(e);
			}
		}, "login");
		dlg.setCancelAction(() => {
			dlg.close();
			cancel();
		}, "cancel")
	});

}

function chooselang_dlg(languages) {
	return new Promise((ok, cancel) => {
		let dlg = create_form(languages.map(x => {
			return {
				"bottom": true,
				"type": "imgbutton",
				"src": x.icon,
				"name": x.id,
			}
		}), "choose_lang", "", "Choose language");
		languages.forEach(x => {
			let l = x.id;
			dlg.setItemEvent(l, "click", async () => {
				dlg.close();
				ok(l);
			});
		});
		dlg.setCancelAction(() => {
			dlg.close();
			cancel();
		})
		dlg.openModal();
	});
}


async function dialog_change_pwd(cb) {
	var chdata = await dialogBox([
		{ name: "old_password", type: "password", "event": "input" },
		{ name: "not_match", type: "errmsg" },
		{ name: "new_password", type: "password", "event": "input" },
		{ name: "retype_password", type: "password", "event": "input" },
		{
			name: "ok", type: "button", bottom: true, "enableif": {
				"old_password": { "!=": "" },
				"new_password": { "!=": "" },
				"retype_password": { "==": { "$name": "new_password" }, "!=": { "$name": "old_password" } },
			}
		},
		{ name: "cancel", type: "button", bottom: true }
	], "change_password", "Change password", cb);
	return chdata;
}

function load_icons(root) {
	Array.prototype.forEach.call(root.querySelectorAll("[data-icon]"), el => {
		let id = el.dataset.icon;
		let img = TemplateJS.loadTemplate(id);
		el.insertBefore(img, el.firstChild);
		delete el.dataset.icon;
	});
}

function page_options_users(app) {

	var userlist;
	const acl_to_list = (acls) => {
		return acls.map(x => { return { "": { "value": app.langobj.get_text("user_dlg", x, x), ".dataset.strid": "user_dlg." + x } } });
	};


	const acl_buttons = (def) => {
		return app.form_defs.acls.map(x => {
			return { name: x, type: "checkbox", default: x != "must_change_pwd" && def }
		});
	};

	const new_user_dlg = () => {
		var fdef = acl_buttons(true);
		fdef = [{ name: "user", type: "string", event: "input" },
		{ name: "err_exists", type: "errmsg" },
		{ name: "pwd", type: "string", default: Math.random().toString(36).slice(2), event: "input" },
		{ name: "ok", type: "button", bottom: true, "enableif": { user: { "!=": "" }, pwd: { "!=": "" } } },
		{ name: "cancel", type: "button", bottom: true },
		{ name: "acls", type: "label" }
		].concat(fdef);


		var dlg = create_form(fdef, "user_dlg", "", "create_user");
		dlg.openModal();
		dlg.setDefaultAction(() => {
			var d = dlg.readData();
			var uid = d.user;
			var r = { pwd: d.pwd, acl: app.form_defs.acls.filter(x => d[x]) };
			d.config_view = d.config_view || d.config_edit;
			if (!userlist[uid]) {
				userlist[uid] = r;
				dlg.close();
				refresh();
			} else {
				dlg.mark("err_exists");
			}
		}, "ok");
		dlg.setCancelAction(() => {
			dlg.close();
		}, "cancel");
	};

	const edit_user_dlg = (user,name) => {
		var fdef = acl_buttons(false);
		fdef = [{ name: "user", type: "string", event: "input", "disableif": {} },
		{ name: "set_pwd", type: "checkbox", default: !!user.pwd },
		{ name: "pwd", type: "string", showif: { set_pwd: true }, default: Math.random().toString(36).slice(2), event: "input" },
		{ name: "ok", type: "button", bottom: true, "disableif": { pwd: "", set_pwd: true } },
		{ name: "delete", type: "button", bottom: true },
		{ name: "cancel", type: "button", bottom: true },
		{ name: "acls", type: "label" }
		].concat(fdef);
		var data = {
			user: name,
			pwd: user.pwd,
		};
		user.acl.forEach(x => data[x] = true);
		var dlg = create_form(fdef, "user_dlg", "", "edit_user");
		dlg.setData(data);
		dlg.openModal();
		dlg.setItemEvent("delete", "click", () => {
			delete userlist[name];
			dlg.close();
			refresh();
		});
		dlg.setDefaultAction(() => {
			var d = dlg.readData();
			d.config_view = d.config_view || d.config_edit;
			var r = { acl: app.form_defs.acls.filter(x => d[x]) };
			if (d.set_pwd) r.pwd = d.pwd;
			Object.assign(user, r);
			dlg.close();
			refresh();
		}, "ok");
		dlg.setCancelAction(() => {
			dlg.close();
		}, "cancel");
	}

	const edit_public_dlg = (cur_acl) => {
		var cacl = cur_acl.reduce((a, b) => { a[b] = true; return a; }, {});
		var fdef = acl_buttons();
		fdef = [
			{ name: "acls", type: "label" },
			{ name: "viewer", type: "checkbox", default: !!cacl.viewer },
			{ name: "reports", type: "checkbox", default: !!cacl.reports },
			{ name: "config_view", type: "checkbox", default: !!cacl.config_view },
			{ name: "ok", type: "button", bottom: true },
			{ name: "cancel", type: "button", bottom: true }];

		var dlg = create_form(fdef, "user_dlg", "", "edit_public_access");
		dlg.openModal();
		dlg.setDefaultAction(() => {
			var acl = dlg.readData();
			userlist[""] = {
				public:true,
				acl:app.form_defs.acls.filter(x => acl[x])				
			}
			dlg.close();
			refresh();
		}, "ok");
		dlg.setCancelAction(() => {
			dlg.close();
		}, "cancel");
	};
	const refresh = () => {
		userlist = theApp.config.users;
		var pacl_src = (userlist[""] || { acl: [] }).acl;
		var pacl = acl_to_list(pacl_src);
		var fdata = {
			public_user: { classList: { accdis: pacl.length == 0 }, "!click": () => edit_public_dlg(pacl_src) },
			public_acl: pacl,
			add_user: { "!click": new_user_dlg },
			users: Object.keys(userlist).filter(x => !userlist[x].public).map(n => {
				var x = userlist[n];
				var pacl = acl_to_list(x.acl);
				return { user: n, acl: pacl, "": { classList: { accdis: pacl.length == 0 }, "!click": () => edit_user_dlg(x,n) } };
			})
		};
		page.setData(fdata);
		load_icons(page.getRoot());
	};
	var page = load_template("page_users");
	app.set_active_page(page, refresh);

}

function set_api_key_dlg(id, broker_name, link) {
	return new Promise(async (ok, cancel) => {
		let dlg = load_template("okcanceldlg");
		var form;
		dlg.openModal();
		dlg.setDefaultAction(async () => {
			try {
				dlg.unmark();
				dlg.enableItem("ok",false);
				var flds = form.readData();
				await put_json(broker_path(id, "/apikey"), flds)
				dlg.close(); ok();
			} catch (e) {
				if (e.io_error && e.req.status == 409) dlg.mark("invalidkey");
				else errorDialog(e);				
				dlg.enableItem("ok",true);								
			}
		}, "ok");
		dlg.setCancelAction(() => {
			dlg.close(); cancel();
		}, "cancel");
		dlg.setData({ "form": load_template("spinner"), title: broker_name , exchlnk:{href:link}});
		try {
			let def = await ui_fetch(broker_path(id, "/apikey"));
			form = create_form(def, "broker_" + id);
			dlg.setData({ "form": form });
			dlg.enableItem("ok", true);
		} catch (e) {
			dlg.close();
			errorDialog(e);
			cancel(e);
		}
	});
}


async function subaccount_name_dlg() {
	function valid_subaccount_name(n) {
		return (/^[a-zA-Z0-9_]+$/).test(n);
	}
	const r = await dialogBox([
		{ name: "id", type: "string", event: "input" },
		{ name: "ok", type: "button", bottom: true, enableif: { "id": valid_subaccount_name } },
		{ name: "cancel", type: "button", bottom: true }
	],"subaccount_dlg","create_subaccount");
	return r.id;
}

async function delete_api_key_dlg(id, name, subaccount){
	var r = await dialogBox([
		{name:"delask",type:"label"},
		{name:"name",type:"string", default:name, disableif:{}},
		{name:"subaccount",type:"string", default:subaccount, disableif:{}},
		{name:"ok", type:"button", bottom:true},
		{name:"cancel", type:"button", bottom:true},
	],"delete_api_key_dlg",name||"title");
	try {
		await ui_fetch(broker_path(id,"/apikey"),{method:"DELETE"});
	} catch (e) {
		errorDialog(e);
	}
	return r;
}

function page_options_apikeys() {
	const refresh = async () => {
		const brks = (await ui_fetch("/broker")).filter(x => x.trading_enabled && !x.nokeys);
		console.log(brks);
		const lst = brks.map(x => {
			return {
				exchange_img: broker_path(x.name, "/icon.png"),
				exchange_lnk: { href: x.exchangeUrl, target: "_blank" },
				exchange_name: x.exchangeName,
				subaccount_name: x.subaccount,
				"": {
					"!click": async(ev)=> {
						if (ev.target.classList.contains("active")) {
							await set_api_key_dlg(x.name, x.exchangeName, x.exchangeUrl);
							refresh();
						}
					}
				},
				"delete_key":{
					"!click": async()=>{
					        await delete_api_key_dlg(x.name, x.exchangeName,x.subaccount);
							refresh();
					}
				}
			}
		});
		const data = { "spinner":{".hidden":true},
					   "keys": lst ,
					   "add":{".hidden":false,
		                      "!click":async ()=>{
												await broker_select(true);
											   refresh();
											  }}};
		page.setData(data);
		load_icons(page.getRoot());
	};
	var page = load_template("page_apikeys");
	this.set_active_page(page, refresh);
}


function broker_select(new_key) {
	return new Promise(async function(ok, cancel) {

		var form = load_template("broker_select");
		form.openModal();
		form.showItem("spinner", true);
		try {
			let x = await ui_fetch("/broker");			
			form.showItem("spinner", false);
			var lst = s => {
				return x.filter(function(z) { 
					if (!s) return !new_key || !z.trading_enabled; else return z.subaccounts;
					})
					.map(z=> {
						return {
							image: broker_path(z.name, "/icon.png"),
							caption: z.exchangeName + (z.subaccount ? " (" + z.subaccount + ")" : ""),
							"": {
								"!click": async () => {
									let subname="";
									var brkname;
									var p;
									
									if (s) {
										subname="~"+await subaccount_name_dlg();
										brkname = z.name + subname;
									} else {
										brkname = z.name;
									}
									var binfo = await ui_fetch(broker_path(brkname));
									while (!binfo[0].trading_enabled) {
										await set_api_key_dlg(brkname,binfo[0].exchangeName, binfo[0].exchangeUrl);
										binfo = await ui_fetch(broker_path(brkname));
									}
									form.close();
									ok(binfo[0]);
								},
								classList: { "available": z.trading_enabled }
							}
						}
					})
					.sort(function(a, b) { return a.caption.localeCompare(b.caption); })
			};
			form.setData({
				"item": lst(false),
				"subaccount": {
					value: false,
					"!change": function() {
						form.setData({ "item": lst(form.readData(["subaccount"]).subaccount) })
					}
				}
			});
			load_icons(form.getRoot());
			form.setCancelAction(function() {
				form.close();
				cancel();
			}, "cancel");
		} catch (e) {
			form.close();
			errorDialog(e);
			cancel(e);
		}
	});

}

function pair_select(broker) {
	return new Promise(async (ok, cancel)=>{
		var form=load_template("broker_pair");
		form.openModal();
		form.setCancelAction(()=>{dlg.close();cancel();},"cancel");
		form.setDefaultAction(function() {
			form.close();
			var d = form.readData();
			if (!d.pair || !d.name) return;
			var name = d.name.replace(/[^-a-zA-Z0-9_.~]/g,"_");
			ok([broker, d.pair, name, d.swap_mode]);
		},"ok");

        function onSelectPair(pair) {
        	form.setItemValue("pair", p);
        }

		try {
			var m = await ui_fetch(broker_path(broker,"/pairs"));
            function refreshItems() {
            	var cur = form.readData(["sect"]).sect;
            	var path = cur?cur.map(function(z) {
            		return z["pair"];
            	}):[]
            	var sect = [];
            	var p =m;
            	var nxt;
            	for (var i = 0; i < path.length; i++) {
            		if (p[path[i]] === undefined) break;
            		var keys = Object.keys(p); 
            		sect.push ({
            			pair: {
            				"classList":{"noarrow":keys.length < 2},
            				"value":path[i],
            				"!change": refreshItems
            			},
            			item: keys.map(function(k) {
            				return {"":{".value": k, ".label":k,".selected":path[i] == k}}
            			})});
            		p = p[path[i]];
            		if (typeof p != "object") break;
            	}
            	while (typeof p == "object" && Object.keys(p).length == 1) {
            		var k = Object.keys(p)[0];
            		var v = p[k];
            		sect.push({
            			item: [{"":{".value":k, ".label": k, ".selected":true}}],
            			pair:{"classList":{"noarrow":true}}
            		});
            		p = p[k];
            	}
            	if (typeof p == "object") {            		
            		sect.push ({
            			pair: {
            				"classList":{"noarrow":false},
            				"value":"",
            				"!change": refreshItems
            			},
            			item: [{"":{".value":"",".label":"---",".selected":true}}].concat(Object.keys(p).map(function(k) {
            				return {"":{".value": k, ".label":k}} 
            			}))});           		
            	}
            	form.setData({"sect":sect});
            	if (typeof p == "string") {
                    form.setItemValue("pair", p);
            	} else {
                    form.setItemValue("pair", "");
            	}
            	dlgRules();
            }

            refreshItems();

            form.showItem("spinner",false);
		} catch (e) {
			form.close();
			errorDialog(e);
			cancel();
		};
		form.setItemValue("image", broker_path(broker,"/image.png"));
		var last_gen_name="";
		var prev_pair="";
		form.setData({
		    "swap_ui":{".style.visibility":"hidden"},
			"swap_mode":{
				"value":"0",
				"!change":function() {
					form.setData({"swap_mode":this.dataset.value});
				}	
			}
		});
		async function dlgRules() {
			var d = form.readData(["pair","name"]);
			if (d.name == last_gen_name) {
				d.name = last_gen_name = broker+"_"+d.pair;
				form.setItemValue("name", last_gen_name);
			}
			form.enableItem("ok", d.pair != "" && d.name != "");			
            if (prev_pair != d.pair) {
            	prev_pair = d.pair;
            	if (d.pair == "") {
					form.setData({
						"swap_ui":{".style.visibility":"hidden"},
						"swap_mode":"0"
					});
                } else {
					try {
						var x = await ui_fetch(broker_pair_path(broker,d.pair));					
						var data = {};
						data.asset = x.asset_symbol;
						data.currency = x.currency_symbol;	
						data.swap_ui = {".style.visibility":x.leverage?"hidden":"visible"};		
						if (x.leverage) {
							data.swap_mode = 0;
						}
						form.setData(data);
					} catch (e) {
						errorDialog(e);
					}
                }
            	                
            }
		};
		
		
		form.setItemEvent("pair","change",dlgRules);
		form.setItemEvent("name","change",dlgRules);
		dlgRules();
	});
}


function page_options_wallet() {
	
	var wlt_cfg = this.config.wallet;
	const refresh = async ()=>{
		var wlt = await ui_fetch("/wallet");
		var rows = wlt.map(x=>{
			var n = wlt_cfg[x.broker];
			if (n) n = n[x.wallet];
			if (n) n = n[x.symbol];
			if (!n) n = 0;  
			
			return {
				img: broker_path(x.broker,"/icon.png"),
				broker: x.broker,
				wallet: x.wallet,
				symbol: x.symbol,
				allocated: x.allocated.toFixed(6),
				balance: {
					value:(x.balance+n).toFixed(6),
					tabindex:0
				},
				editval: {
					classList:{modified:n!=0},
					"!click":async (ev)=>{
						ev.target.classList.toggle("editing",true);
						try {
							var old = x.balance;
							var nw = await chng_val(old+n, x.balance);
							var r = isNaN(nw)?0:nw-old;
							if (r) {
								var b = wlt_cfg[x.broker];							
								if (!b) b = wlt_cfg[x.broker] = {};
								var w = b[x.wallet];
								if (!w) w = b[x.wallet] = {};
								w[x.symbol] = r;
							} else {
								var b = wlt_cfg[x.broker];
								if (b) {
									var w = b[x.wallet];
									if (w) {
										delete w[x.symbol];
									}
									if (Object.keys(w).length == 0) {
										delete b[x.wallet];
										if (Object.keys(b).length == 0) {
											delete wlt_cfg[x.broker];
										}
									}
								}
							} 
							refresh();
						} catch (e) {
							if (e) console.error(e);
						}					
						ev.target.classList.toggle("editing",false);
					}	
				}			
			};
		});
		page.setData({"allocs":rows});
	};

	const chng_val = async (amount, original) => {
			var p = dialogBox([
				{name:"current",type:"number", default: original, disableif:{}},
				{name:"amount",type:"number", default: amount},
				{name:"reset", type:"button", bottom:true},
				{name:"ok", type:"button", bottom:true},
				{name:"cancel", type:"button", bottom:true},
				],"wallet_change_value","title");
			p.dlg.setItemEvent("reset","click",()=>{
				p.dlg.setData({amount:original});
			})
			return (await p).amount;
	};

	
	var page = load_template("wallet_form");
	this.set_active_page(page, refresh);
	
}

function page_options_misc() {
	const refresh = async ()=>{
		let f = create_form([
			{name:"stat_history",type:"slider","min":1,"max":90, "default": Math.round((this.config.report_interval || 30*86400000)/86400000)},
			{name:"stat_window",type:"checkbox", default: !!this.config.report_window_only}
		],"misc_form");		
		page.setData({"inner":f});
		f.setItemEvent("stat_history","change",()=>{
			const x = f.readData(["stat_history"]);
			this.config.report_interval = x.stat_history*86400000;
		});
		f.setItemEvent("stat_window","change",()=>{
			const x = f.readData(["stat_window"]);
			this.config.report_window_only = x.stat_window;
		})				
	}
	
	var page = load_template("misc_form");
	this.set_active_page(page, refresh);	
}

function page_options_exchanges() {
	var selected_exchange = "";
	const refresh = async ()=>{
		const brks = (await ui_fetch("/broker")).filter(x => x.trading_enabled);
		const update = async () =>{
			var lst = brks.map(x=>{
				return {					
					icon:broker_path(x.name, "/icon.png"),
					ename:x.exchangeName+(x.subaccount?" ("+x.subaccount+")":""),
					"":{
						classList:{selected:x.name == selected_exchange},
						"!click": ()=>{
							selected_exchange = x.name;
							update();
						}
					} 	
				}
			});
			page.setData({"exch": lst});
			if (selected_exchange) {
				const exch = await ui_fetch(broker_path(selected_exchange,"/wallet"));
				const data = Object.keys(exch).map(n=>{
					return {						
					name:n,
					items: Object.keys(exch[n]).map(m=>{
						const v = exch[n][m];
						return {
							value:v.toFixed(6),
							symbol: m
						}
					})
					};
				});
				page.setData({"sections": data});
			}
			
		}
		update();
	}
	var page = load_template("exchanges_form");
	this.set_active_page(page, refresh);	
}