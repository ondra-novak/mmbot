"use strict";

function create_form(formdesc, langcat, category, dialog) {
	var fbld = new FormBuilder(formdesc,langobj,langcat, category);
	return fbld.create_form(dialog);
}

function load_template(id) {
	var form = TemplateJS.View.fromTemplate(id);
	langobj.translate_node(form.getRoot(), id);
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
			  var unfo = await post_json("../api/user",{"user":data.user,"password":data.password,"cookie":"permanent"});
		      if (unfo.exists) {
					dlg.close();
					ok(unfo); 					
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

