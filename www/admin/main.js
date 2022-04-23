"use strict";

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
		this.lang_list = this.lang.get_list();
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
			return lst.map(x=>{return {"":{"value":this.langobj.get_text("user_dlg",x,x),".dataset.strid":"user_dlg."+x}}});				
		};
				
		
		
		const acl_buttons = () =>{
			return this.form_defs.acls.map(x=>{
				return {name:x,type:"checkbox",default:x!="must_change_pwd"}
			});
		};
		
		const new_user_dlg=()=>{
			var fdef = acl_buttons();
			fdef = [{name:"user",type:"string",event:"input"},
			 {name:"err_exists",type:"errmsg"},
			 {name:"pwd",type:"string",default:Math.random().toString(36).slice(2),event:"input"},
			 {name:"ok",type:"button",bottom:true,"enableif":{user:{"!=":""},pwd:{"!=":""}}},
			 {name:"cancel",type:"button",bottom:true},
			 {name:"acls",type:"label"}
			].concat(fdef);
			
			
			var dlg = create_form(fdef,"user_dlg","","create_user");
			dlg.openModal();
			dlg.setDefaultAction(()=>{
				var d = dlg.readData();
				var r = {user:d.user,pwd:d.pwd,acl:d};
				delete d.user; delete d.pwd;
				if (!this.config.users.find(x=>x.user == r.user)) { 
					this.config.users.push(r);
					dlg.close();			
					refresh();
				} else {
					dlg.mark("err_exists");
				}
			},"ok");
			dlg.setCancelAction(()=>{
				dlg.close();
			},"cancel");
		};

		const edit_user_dlg=(user)=>{
			var fdef = acl_buttons();
			fdef = [{name:"user",type:"string",event:"input","disableif":{}},
					{name:"set_pwd",type:"checkbox",default:!!user.pwd},
				    {name:"pwd",type:"string",showif:{set_pwd:true},default:Math.random().toString(36).slice(2),event:"input"},
					{name:"ok",type:"button",bottom:true,"disableif":{pwd:"",set_pwd:true}},
					{name:"delete",type:"button",bottom:true},
					{name:"cancel",type:"button",bottom:true},
					{name:"acls",type:"label"}
			].concat(fdef);
			var data = Object.assign({
				user:user.user,
				pwd:user.pwd,
			},user.acl);
			var dlg = create_form(fdef,"user_dlg","","edit_user");
			dlg.setData(data);
			dlg.openModal();
			dlg.setItemEvent("delete","click",()=>{
				this.config.users = this.config.users.filter(x=>x.user != user.user);
				dlg.close();			
				refresh();				
			});
			dlg.setDefaultAction(()=>{
				var d = dlg.readData();
				var r = {user:d.user,acl:d};
				if (d.set_pwd) r.pwd = d.pwd;
				delete d.user; delete d.pwd; delete d.set_pwd;
				delete user.pwd;
				Object.assign(user, r);
				dlg.close();			
				refresh();
			},"ok");
			dlg.setCancelAction(()=>{
				dlg.close();
			},"cancel");			
		}

		const edit_public_dlg=(cur_acl)=>{
			var fdef = acl_buttons();
			fdef = [
				{name:"acls",type:"label"},
				{name:"viewer",type:"checkbox",default:!!cur_acl.viewer},
			 {name:"reports",type:"checkbox",default:!!cur_acl.reports},
			 {name:"config_view",type:"checkbox",default:!!cur_acl.config_view},
			 {name:"ok",type:"button",bottom:true,"enableif":{username:{"!=":""},password:{"!=":""}}},
			 {name:"cancel",type:"button",bottom:true}];
			
			var dlg = create_form(fdef,"user_dlg","","edit_public_access");
			dlg.openModal();
			dlg.setDefaultAction(()=>{
				var pub = this.config.users.find(x=>!!x.public);
				if (!pub) {
					pub = {public:true}; this.config.users.push(pub);
				}
				pub.acl = dlg.readData();
				dlg.close();			
				refresh();				
			},"ok");
			dlg.setCancelAction(()=>{
				dlg.close();
			},"cancel");
		};	
		var page = load_template("page_users");
		this.page_root.setData({"workspace":page});
		if (!this.config.users) this.config.users = [];
		const refresh = () => {
			var users = this.config.users;
			var pacl_src = (users.find(x=>!!x.public) || {acl:{}}).acl;
			var pacl = acl_to_list(pacl_src);
			var fdata = {
				public_user:{classList:{accdis:pacl.length==0},"!click":()=>edit_public_dlg(pacl_src)},
				public_acl: pacl,
				add_user: {"!click":new_user_dlg},
				users:users.filter(x=>!x.public).map(x=>{
					var pacl = acl_to_list(x.acl);
					return {user:x.user, acl:pacl ,"":{classList:{accdis:pacl.length==0},"!click":()=>edit_user_dlg(x)}};
				})
			};
			page.setData(fdata);
			load_icons(page.getRoot());
		};
		refresh();

		
					
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

	
	
	
	
	
	
	
