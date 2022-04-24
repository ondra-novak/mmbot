"use strict";

function create_form(formdesc, langcat, category, dialog) {
	var fbld = new FormBuilder(formdesc,theApp.langobj,langcat, category);
	return fbld.create_form(dialog);
}

function load_template(id) {
	var form = TemplateJS.View.fromTemplate(id);
	theApp.langobj.translate_node(form.getRoot(), id);
	load_icons(form.getRoot());
	return form;
}

function dialogBox(formdesc, langcat, title, validate) {
	return new Promise((ok,cancel)=>{
		var dlg = create_form(formdesc, langcat, "", title);
		dlg.setDefaultAction(async ()=>{
			var data = dlg.readData();
			if (!validate || await validate(data,dlg)) { 
				dlg.close();
				ok(dlg.readData());
			}
		},"ok");
		dlg.setCancelAction(()=>{
			dlg.close();
			cancel();
		},"cancel");
		dlg.openModal();
	});		
}


function ui_login() {
	return new Promise((ok,cancel)=>{	
		var dlg = create_form([
			{name:"user",type:"string",attrs:{"autocomplete":"username"}},
			{name:"password",type:"password",attrs:{"autocomplete":"current-password"}},
			{name:"logerror",type:"errmsg"},
			{name:"login",type:"button",bottom:true},
			{name:"cancel",type:"button",bottom:true}]
							  ,"login","","login_dlg");			
		dlg.openModal();
		dlg.setDefaultAction(async()=>{
			var data = dlg.readData();
			try {
			  var unfo = await post_json("/user",{"user":data.user,"password":data.password,"cookie":"permanent"});
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
		},"login");
		dlg.setCancelAction(()=>{
			dlg.close();
			cancel();
		},"cancel")
	});
		
}

function chooselang_dlg(languages) {
	return new Promise((ok,cancel)=>{
		let dlg = create_form(languages.map(x=>{
			return {
				"bottom":true,
				"type":"imgbutton",
				"src":x.icon,
				"name":x.id,				
			}}),"choose_lang","","Choose language");				
		languages.forEach(x=>{
			let l = x.id;
			dlg.setItemEvent(l,"click",async ()=>{				
				dlg.close();				
				ok(l);						
			});	
		});
		dlg.setCancelAction(()=>{
			dlg.close();
			cancel();
		})
		dlg.openModal();
	});
	}


async function dialog_change_pwd(cb) {
	var chdata = await dialogBox([
		{name:"old_password",type:"password","event":"input"},
		{name:"not_match",type:"errmsg"},
		{name:"new_password",type:"password","event":"input"},
		{name:"retype_password",type:"password","event":"input"},
		{name:"ok",type:"button",bottom:true,"enableif":{
				"old_password":{"!=":""},
				"new_password":{"!=":""},
				"retype_password":{"==":{"$name":"new_password"}, "!=":{"$name":"old_password"}},
			}
		},
		{name:"cancel",type:"button",bottom:true}
		],"change_password","Change password",cb);
		return chdata;
}

function load_icons(root) {
	Array.prototype.forEach.call(root.querySelectorAll("[data-icon]"),el=>{
		let id = el.dataset.icon;
		let img = TemplateJS.loadTemplate(id);
		el.insertBefore(img,el.firstChild);
		delete el.dataset.icon;
	});
}

function page_options_users(app) {

		var userlist;
		 const acl_to_list = (acls)=> {
			return acls.map(x=>{return {"":{"value":app.langobj.get_text("user_dlg",x,x),".dataset.strid":"user_dlg."+x}}});				
		};
				
	
		const acl_buttons = (def) =>{
			return app.form_defs.acls.map(x=>{
				return {name:x,type:"checkbox",default:x!="must_change_pwd" && def}
			});
		};
		
		const new_user_dlg=()=>{
			var fdef = acl_buttons(true);
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
				var r = {uid:d.user,pwd:d.pwd,acl:app.form_defs.acls.filter(x=>d[x])};	
				d.config_view  = d.config_view || d.config_edit;
				if (!userlist.find(x=>x.uid == r.uid)) { 
					userlist.push(r);
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
			var fdef = acl_buttons(false);
			fdef = [{name:"user",type:"string",event:"input","disableif":{}},
					{name:"set_pwd",type:"checkbox",default:!!user.pwd},
				    {name:"pwd",type:"string",showif:{set_pwd:true},default:Math.random().toString(36).slice(2),event:"input"},
					{name:"ok",type:"button",bottom:true,"disableif":{pwd:"",set_pwd:true}},
					{name:"delete",type:"button",bottom:true},
					{name:"cancel",type:"button",bottom:true},
					{name:"acls",type:"label"}
			].concat(fdef);
			var data = {
				user:user.uid,
				pwd:user.pwd,
			};
			user.acl.forEach(x=>data[x]=true);
			var dlg = create_form(fdef,"user_dlg","","edit_user");
			dlg.setData(data);
			dlg.openModal();
			dlg.setItemEvent("delete","click",()=>{
				userlist = userlist.filter(x=>x.user != user.user);
				dlg.close();			
				refresh();				
			});
			dlg.setDefaultAction(()=>{
				var d = dlg.readData();
				d.config_view  = d.config_view || d.config_edit;
				var r = {uid:d.user,acl:app.form_defs.acls.filter(x=>d[x])};
				if (d.set_pwd) r.pwd = d.pwd;
				Object.assign(user, r);
				dlg.close();			
				refresh();
			},"ok");
			dlg.setCancelAction(()=>{
				dlg.close();
			},"cancel");			
		}

		const edit_public_dlg=(cur_acl)=>{
			var cacl = cur_acl.reduce((a,b)=>{a[b]=true;return a;},{});
			var fdef = acl_buttons();
			fdef = [
				{name:"acls",type:"label"},
				{name:"viewer",type:"checkbox",default:!!cacl.viewer},
			 {name:"reports",type:"checkbox",default:!!cacl.reports},
			 {name:"config_view",type:"checkbox",default:!!cacl.config_view},
			 {name:"ok",type:"button",bottom:true},
			 {name:"cancel",type:"button",bottom:true}];
			
			var dlg = create_form(fdef,"user_dlg","","edit_public_access");
			dlg.openModal();
			dlg.setDefaultAction(()=>{
				var pub = userlist.find(x=>!!x.public);
				if (!pub) {
					pub = {public:true}; userlist.push(pub);
				}
				var acl = dlg.readData();
				pub.acl = app.form_defs.acls.filter(x=>acl[x]);
				dlg.close();			
				refresh();				
			},"ok");
			dlg.setCancelAction(()=>{
				dlg.close();
			},"cancel");
		};	
		const refresh = () => {			
			userlist = theApp.config.users;
			var pacl_src = (userlist.find(x=>!!x.public) || {acl:[]}).acl;
			var pacl = acl_to_list(pacl_src);
			var fdata = {
				public_user:{classList:{accdis:pacl.length==0},"!click":()=>edit_public_dlg(pacl_src)},
				public_acl: pacl,
				add_user: {"!click":new_user_dlg},
				users:userlist.filter(x=>!x.public).map(x=>{
					var pacl = acl_to_list(x.acl);
					return {user:x.uid, acl:pacl ,"":{classList:{accdis:pacl.length==0},"!click":()=>edit_user_dlg(x)}};
				})
			};
			page.setData(fdata);
			load_icons(page.getRoot());
		};
		var page = load_template("page_users");
		app.set_active_page(page,refresh);				
					
	}
