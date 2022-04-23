"use strict";
async function ui_fetch(url, opts) {

	if (url.startsWith("/")) url = app_config.api_base+url;
	
	const req = await fetch(url, opts);
    if (req.status >= 401 && req.status <=403) {
		var rep = await error_forbidden(req);
		if (rep) {
			return ui_fetch(url, opts);			
		} else {
			throw  {"io_error":true, "req":req, dialog_shown:true};
		}		
    } else if (req.status >= 200 && req.status < 300){		
        return await req.json();
    } else {
		throw {"io_error":true, "req":req};
	}
}	

function error_forbidden(req) {
	return new Promise((ok)=>{
		var dlg = create_form([
			{type:"label",name:"fetcherror"},
			{type:"label",name:"status_"+req.status,label:req.status+" "+req.statusText},
			{type:"button",bottom:true,name:"login"},
			{type:"button",bottom:true,name:"ok"}],"gendlg","","io_error");		
		dlg.openModal();
		dlg.setDefaultAction(async ()=>{
			try {
				dlg.close();
				await ui_login();
				ok(true);
			} catch (e) {
				ok(false);
			}
		},"login");
		dlg.setCancelAction(()=>{
			dlg.close();
			ok(false);
		},"ok");
	});
}

async function errorDialog(exc) {
	if (exc.dialog_shown) return;
	if (exc.io_error) {
	  var ainfo ;
	  try {
		ainfo = await exc.req.json();
		ainfo = ainfo.error.message;
	  } catch (e) {		  
	  };		
		console.log(ainfo);
      await dialogBox([
			{type:"label",name:"fetcherror"},
			{type:"label",name:"status_"+exc.req.status,label:exc.req.status+" "+exc.req.statusText},
		    {type:"label",label:ainfo || ""},
			{type:"button",bottom:true,name:"ok"}],"gendlg","io_error");		
	} else {
		console.error(exc);
	  var txt = exc.toString();
	  if (txt == "[object Object]") txt = JSON.stringify(exc);
      await dialogBox([
			{type:"label",name:"unknownerror"},
			{type:"label",name:"unknown",label:txt},
			{type:"button",bottom:true,name:"ok"}],"gendlg","io_error");		
	}
	return exc; 	
}


function post_json(url, json) {
	return ui_fetch(url, {
		"method":"POST",
		"body":JSON.stringify(json),		
	});
} 

function put_json(url, json) {
	return ui_fetch(url, {
		"method":"PUT",
		"body":JSON.stringify(json),		
	});
} 

async function base64_arraybuffer(data) {
    const base64url = await new Promise((r) => {
        const reader = new FileReader()
        reader.onload = () => r(reader.result)
        reader.readAsDataURL(new Blob([data]))
    })
    return base64url.split(",", 2)[1]
}


