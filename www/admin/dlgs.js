"use strict";

function create_form(formdesc, langcat, category, dialog) {
	var fbld = new FormBuilder(formdesc,theApp.langobj,langcat, category);
	return fbld.create_form(dialog);
}

function load_template(id) {
	var form = TemplateJS.View.fromTemplate(id);
	theApp.langobj.translate_node(form.getRoot(), id);
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
				"src":x[1],
				"name":x[0]				
			}}),"choose_lang","","Choose language");				
		languages.forEach(x=>{
			let l = x[0];
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