"use strict";
async function ui_fetch(url, opts) {
	
	const req = await fetch(url, opts);
    if (req.status == 401) {
        return ui_login().then(() => { return ui_fetch(url, opts); });
    } else if (req.status >= 200 && req.status < 300){		
        return await req.json();
    } else {
		throw {"io_error":true, "req":req};
	}
}	

async function errorDialog(exc) {
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


